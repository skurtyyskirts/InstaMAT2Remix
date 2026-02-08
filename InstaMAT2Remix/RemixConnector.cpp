#include "RemixConnector.h"
#include "PluginInfo.h"
#include "PluginPaths.h"

#include <QApplication>
#include <QAbstractButton>
#include <QAbstractItemView>
#include <QAccessible>
#include <QAccessibleActionInterface>
#include <QAccessibleEditableTextInterface>
#include <QAccessibleTextInterface>
#include <QAccessibleValueInterface>
#include <QClipboard>
#include <QComboBox>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QGuiApplication>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMap>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPointer>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <QProgressDialog>
#include <QSysInfo>
#include <QWindow>
// NOTE: We intentionally avoid linking against QtQuick/QML headers here.
// Including them would add Qt6Quick.dll / Qt6Qml.dll as hard load-time dependencies,
// breaking plugin loading when InstaMAT extracts the DLL to a temp directory.
// New Project automation relies on Qt accessibility (QAccessible) instead.

namespace InstaMAT2Remix {
    namespace {
        static RemixConnector* g_instance = nullptr;

        constexpr const char* kRemixAccept = "application/lightspeed.remix.service+json; version=1.0";
        constexpr const char* kKeyTemplateGraphPath = "TemplateGraphPath";
        constexpr const char* kKeyTemplateMeshInputName = "TemplateMeshInputName";
        constexpr const char* kKeyTemplateAutoRunOnPull = "TemplateAutoRunOnPull";
        constexpr const char* kKeyProjectsFolder = "ProjectsFolder";

        const QSet<QString> kKnownTailNames = {
            "textures", "painterconnector_ingested", "painterconnector-ingested", "ingested",
            "captures", "capture", "assets", "output", "export", "exports"};

        struct PbrSpec {
            QString pbrType;
            QString mdlInput;
            bool sRGB;
            int bits;
        };

        const QList<PbrSpec> kDefaultPbrSpecs = {
            {"albedo", "diffuse_texture", true, 8},
            {"normal", "normalmap_texture", false, 8},
            {"roughness", "reflectionroughness_texture", false, 8},
            {"metallic", "metallic_texture", false, 8},
            {"emissive", "emissive_mask_texture", true, 8},
            {"height", "height_texture", false, 16},
            {"opacity", "opacity_texture", false, 8},
            {"ao", "ao_texture", false, 8},
            {"transmittance", "transmittance_texture", true, 8},
            {"ior", "ior_texture", false, 8},
            {"subsurface", "subsurface_texture", true, 8},
        };

        const QHash<QString, QString> kPbrToIngestValidation = {
            {"albedo", "DIFFUSE"},
            {"normal", "NORMAL_DX"},
            {"height", "HEIGHT"},
            {"roughness", "ROUGHNESS"},
            {"metallic", "METALLIC"},
            {"emissive", "EMISSIVE"},
            {"ao", "AO"},
            {"opacity", "OPACITY"},
            {"transmittance", "TRANSMITTANCE"},
            {"ior", "IOR"},
            {"subsurface", "SUBSURFACE"},
        };

        QString NormalizeSpacesUnderscoresLower(QString s) {
            s = s.toLower();
            s.replace(' ', "");
            s.replace('_', "");
            return s;
        }

        QString DefaultLogFilePath() {
            QString docs = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
            if (docs.isEmpty()) docs = QDir::homePath();
            const QString logDir = QDir(docs).filePath("InstaMAT2Remix/logs");
            QDir().mkpath(logDir);
            return QDir(logDir).filePath("remix_connector.log");
        }

        QString DetectTexconvPath() {
            const QString p = QDir(GetPluginDirPath()).filePath("texconv.exe");
            return QFileInfo::exists(p) ? p : QString();
        }

        QString DetectDefaultTilingMeshPath() {
            const QString base = GetPluginDirPath();

            QStringList candidates;

            // 1) Next to the loaded plugin DLL (ideal).
            candidates << QDir(base).filePath("assets/meshes/plane_tiling.usd");
            candidates << QDir(base).filePath("plane_tiling.usd");

            // 2) Common install locations (InstaMAT may load the DLL from an extracted .IMP location,
            // so assets next to the "real" plugin install folder are still valid).
            const QString docs = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
            if (!docs.isEmpty()) {
                candidates << QDir(docs).filePath("InstaMAT/Plugins/assets/meshes/plane_tiling.usd");
                candidates << QDir(docs).filePath("InstaMAT/Plugins/plane_tiling.usd");
            }

            const QString appdata = QString::fromLocal8Bit(qgetenv("APPDATA"));
            if (!appdata.isEmpty()) {
                candidates << QDir(appdata).filePath("InstaMAT Studio/Plugins/assets/meshes/plane_tiling.usd");
                candidates << QDir(appdata).filePath("InstaMAT Studio/Plugins/plane_tiling.usd");
            }

            // 3) Host EXE directory (fallback).
            const QString exeDir = QCoreApplication::applicationDirPath();
            candidates << QDir(exeDir).filePath("assets/meshes/plane_tiling.usd");
            candidates << QDir(exeDir).filePath("plane_tiling.usd");

            for (const QString& p : candidates) {
                if (QFileInfo::exists(p)) return QDir::cleanPath(p);
            }
            return QString();
        }

        QString NormalizeActionText(const QString& t) {
            QString s = t;
            s.remove('&');
            return s.trimmed();
        }

        bool MenuHasCoreActions(QMenuBar* bar) {
            if (!bar) return false;
            const QStringList needles = {"File", "Edit", "Help"};
            for (QAction* a : bar->actions()) {
                if (!a) continue;
                const QString text = NormalizeActionText(a->text());
                if (needles.contains(text, Qt::CaseInsensitive)) return true;
            }
            return false;
        }

        QMainWindow* FindHostMainWindow() {
            auto widgets = QApplication::topLevelWidgets();
            QMainWindow* bestWithMenus = nullptr;
            QMainWindow* visibleFallback = nullptr;

            for (QWidget* widget : widgets) {
                QMainWindow* win = qobject_cast<QMainWindow*>(widget);
                if (!win || !win->isVisible()) continue;
                if (!visibleFallback) visibleFallback = win;

                QMenuBar* bar = win->findChild<QMenuBar*>();
                if (bar && MenuHasCoreActions(bar)) {
                    bestWithMenus = win;
                    break;
                }
            }

            return bestWithMenus ? bestWithMenus : visibleFallback;
        }

        void CollectMenuActionsRecursive(QMenu* menu, QList<QAction*>& out) {
            if (!menu) return;
            for (QAction* a : menu->actions()) {
                if (!a) continue;
                out.push_back(a);
                if (a->menu()) CollectMenuActionsRecursive(a->menu(), out);
            }
        }

        QAction* FindBestNewProjectAction(QMenuBar* bar) {
            if (!bar) return nullptr;

            QList<QAction*> actions;
            for (QAction* top : bar->actions()) {
                if (!top) continue;
                actions.push_back(top);
                if (top->menu()) CollectMenuActionsRecursive(top->menu(), actions);
            }

            QAction* bestProject = nullptr;
            int bestProjectScore = 0;
            QAction* bestFallback = nullptr;
            int bestFallbackScore = 0;

            for (QAction* a : actions) {
                if (!a) continue;
                const QString t = NormalizeActionText(a->text());
                if (t.isEmpty()) continue;

                const QString lower = t.toLower();
                int score = 0;

                if (lower.contains("asset") && lower.contains("texturing") && (lower.contains("new") || lower.contains("project"))) score = 140;
                else if (lower == "asset texturing") score = 130;
                else if (lower == "new texturing project") score = 120;
                else if (lower == "new project" || lower == "new layering project") score = 100;
                else if (lower.contains("new") && lower.contains("project")) score = 80;
                else if (lower.contains("create") && lower.contains("project")) score = 70;
                else if (lower == "new") score = 20;

                if (a->shortcut().matches(QKeySequence::New) == QKeySequence::ExactMatch) score += 15;

                const bool looksProjectSpecific =
                    lower.contains("project") ||
                    lower.contains("texturing") ||
                    lower.contains("layering") ||
                    lower.contains("asset");

                if (looksProjectSpecific) {
                    if (score > bestProjectScore) {
                        bestProjectScore = score;
                        bestProject = a;
                    }
                } else {
                    if (score > bestFallbackScore) {
                        bestFallbackScore = score;
                        bestFallback = a;
                    }
                }
            }

            // Don't return ultra-weak matches.
            if (bestProjectScore >= 20) return bestProject;
            return (bestFallbackScore >= 20) ? bestFallback : nullptr;
        }

        bool HasProjectWizardSignature(QWidget* root) {
            if (!root) return false;

            // Look for the disabled "Select mesh to create project" or similar button.
            const auto buttons = root->findChildren<QAbstractButton*>();
            for (QAbstractButton* b : buttons) {
                if (!b) continue;
                const QString text = NormalizeActionText(b->text()).toLower();
                if (text.contains("create") && text.contains("project") && text.contains("mesh")) {
                    return true;
                }
            }

            // Look for the mesh placeholder text.
            const auto edits = root->findChildren<QLineEdit*>();
            for (QLineEdit* e : edits) {
                if (!e) continue;
                const QString t = e->text().trimmed().toLower();
                if (t == "no mesh selected" || t.contains("no mesh")) {
                    return true;
                }
            }

            // Some builds use a combo-style mesh picker.
            const auto combos = root->findChildren<QComboBox*>();
            for (QComboBox* c : combos) {
                if (!c) continue;
                const QString t = c->currentText().trimmed().toLower();
                if (t == "no mesh selected" || t.contains("no mesh")) return true;
            }

            // Label-based fallback (newer UIs may not expose the mesh field as a QLineEdit).
            const auto labels = root->findChildren<QLabel*>();
            for (QLabel* l : labels) {
                if (!l) continue;
                const QString t = NormalizeActionText(l->text()).trimmed().toLower();
                if (t.contains("how do you want to get started")) return true;
                if (t.contains("templates") && t.contains("tutorials")) return true;
                if (t.contains("please choose") && t.contains("template")) return true;
            }

            return false;
        }

        QWidget* FindProjectWizardRoot(QMainWindow* win) {
            if (win) {
                if (QWidget* cw = win->centralWidget()) {
                    if (HasProjectWizardSignature(cw)) return cw;
                }
                if (HasProjectWizardSignature(win)) return win;
            }

            const auto widgets = QApplication::topLevelWidgets();
            for (QWidget* w : widgets) {
                if (!w || !w->isVisible()) continue;
                if (HasProjectWizardSignature(w)) return w;
            }
            return nullptr;
        }

        QLineEdit* FindMeshLineEdit(QWidget* root) {
            if (!root) return nullptr;
            const auto edits = root->findChildren<QLineEdit*>();
            for (QLineEdit* e : edits) {
                if (!e) continue;
                const QString t = e->text().trimmed();
                if (t.compare("No Mesh selected", Qt::CaseInsensitive) == 0) return e;
            }
            for (QLineEdit* e : edits) {
                if (!e) continue;
                const QString lower = e->text().trimmed().toLower();
                if (lower.contains("no mesh")) return e;
            }
            // Some versions use placeholder text instead of an initial value.
            for (QLineEdit* e : edits) {
                if (!e) continue;
                const QString ph = e->placeholderText().trimmed().toLower();
                if (ph.isEmpty()) continue;
                if (ph.contains("mesh") && (ph.contains("no") || ph.contains("select"))) return e;
                if (ph == "mesh" || ph.contains("mesh")) return e;
            }
            for (QLineEdit* e : edits) {
                if (!e) continue;
                const QString name = e->objectName().toLower();
                if (name.contains("mesh")) return e;
            }
            return nullptr;
        }

        QComboBox* FindMeshComboBox(QWidget* root) {
            if (!root) return nullptr;
            const auto combos = root->findChildren<QComboBox*>();
            for (QComboBox* c : combos) {
                if (!c) continue;
                const QString t = c->currentText().trimmed();
                if (t.compare("No Mesh selected", Qt::CaseInsensitive) == 0) return c;
            }
            for (QComboBox* c : combos) {
                if (!c) continue;
                const QString lower = c->currentText().trimmed().toLower();
                if (lower.contains("no mesh")) return c;
            }
            for (QComboBox* c : combos) {
                if (!c) continue;
                const QString name = c->objectName().toLower();
                if (name.contains("mesh")) return c;
            }
            return nullptr;
        }

        QLineEdit* FindProjectNameLineEdit(QWidget* root) {
            if (!root) return nullptr;
            const auto edits = root->findChildren<QLineEdit*>();
            for (QLineEdit* e : edits) {
                if (!e) continue;
                const QString t = e->text().trimmed();
                const QString lower = t.toLower();
                // Common defaults across versions / project types.
                if (t.compare("Unnamed Layering Project", Qt::CaseInsensitive) == 0) return e;
                if (t.compare("Unnamed Texturing Project", Qt::CaseInsensitive) == 0) return e;
                if (lower.startsWith("unnamed") && lower.contains("project")) return e;
                if (lower == "unnamed project" || lower == "untitled project" || lower == "untitled") return e;
            }
            for (QLineEdit* e : edits) {
                if (!e) continue;
                const QString name = e->objectName().toLower();
                if (name.contains("project") && name.contains("name")) return e;
            }
            return nullptr;
        }

        QAbstractButton* FindCreateProjectButton(QWidget* root) {
            if (!root) return nullptr;
            const auto buttons = root->findChildren<QAbstractButton*>();
            QAbstractButton* best = nullptr;
            int bestScore = 0;

            for (QAbstractButton* b : buttons) {
                if (!b) continue;
                const QString t = NormalizeActionText(b->text());
                if (t.isEmpty()) continue;
                const QString lower = t.toLower();

                int score = 0;
                if (lower == "create project" || lower == "create") score = 100;
                else if (lower.contains("create") && lower.contains("project")) score = 90;
                else if (lower.contains("select") && lower.contains("mesh") && lower.contains("project")) score = 80;
                else if (lower.contains("create") && lower.contains("mesh")) score = 70;

                if (score > bestScore) {
                    bestScore = score;
                    best = b;
                }
            }

            return (bestScore >= 70) ? best : nullptr;
        }

        bool HasNewProjectChooserSignature(QWidget* root) {
            if (!root) return false;

            // The chooser screen typically lists multiple project types (Asset Texturing, Material Layering, ...).
            // We look for at least 2 known labels to avoid false positives.
            int hits = 0;
            const auto buttons = root->findChildren<QAbstractButton*>();
            for (QAbstractButton* b : buttons) {
                if (!b) continue;
                const QString t = NormalizeActionText(b->text()).toLower();
                if (t == "asset texturing" ||
                    t == "material layering" ||
                    t == "materialize image" ||
                    t == "element graph" ||
                    t == "npass element graph" ||
                    t == "atom graph" ||
                    t == "function graph") {
                    hits++;
                    if (hits >= 2) return true;
                }
            }

            // Many builds use a list view (not buttons) for the project type selector.
            // Scan item views for known entries.
            const auto views = root->findChildren<QAbstractItemView*>();
            for (QAbstractItemView* v : views) {
                if (!v || !v->model()) continue;
                const int rows = v->model()->rowCount(v->rootIndex());
                if (rows <= 0 || rows > 50) continue;

                int viewHits = 0;
                for (int r = 0; r < rows; ++r) {
                    const QModelIndex idx = v->model()->index(r, 0, v->rootIndex());
                    const QString txt = idx.data(Qt::DisplayRole).toString().trimmed().toLower();
                    if (txt == "asset texturing" ||
                        txt == "material layering" ||
                        txt == "materialize image" ||
                        txt == "element graph" ||
                        txt == "npass element graph" ||
                        txt == "atom graph" ||
                        txt == "function graph") {
                        viewHits++;
                        if (viewHits >= 2) return true;
                    }
                }
            }

            // Fallback: look for a "New Project" header label (localized text may vary, so keep loose).
            const auto labels = root->findChildren<QLabel*>();
            for (QLabel* l : labels) {
                if (!l) continue;
                const QString t = l->text().trimmed().toLower();
                if (t == "new project" || (t.contains("new") && t.contains("project"))) {
                    // Require at least one known project type label to reduce false positives.
                    for (QAbstractButton* b : buttons) {
                        if (!b) continue;
                        const QString bt = NormalizeActionText(b->text()).toLower();
                        if (bt == "asset texturing") return true;
                    }
                }
            }

            return false;
        }

        QWidget* FindNewProjectChooserRoot(QMainWindow* win) {
            if (win) {
                if (QWidget* cw = win->centralWidget()) {
                    if (HasNewProjectChooserSignature(cw)) return cw;
                }
                if (HasNewProjectChooserSignature(win)) return win;
            }

            // Check all top-level widgets (including QML windows)
            const auto widgets = QApplication::topLevelWidgets();
            for (QWidget* w : widgets) {
                if (!w || !w->isVisible()) continue;
                
                // Check window title for "New Project" indicators
                QString title = w->windowTitle().trimmed().toLower();
                if (title.contains("new project") || title.contains("project") || title.isEmpty()) {
                    if (HasNewProjectChooserSignature(w)) return w;
                    
                    // For QML-based windows, try to find recognizable text
                    QString accName;
                    if (auto* iface = QAccessible::queryAccessibleInterface(w)) {
                        accName = iface->text(QAccessible::Name).toLower();
                        if (accName.contains("new project") || accName.contains("asset texturing")) {
                            return w;
                        }
                    }
                }
            }
            return nullptr;
        }

        QAbstractButton* FindAssetTexturingButton(QWidget* root) {
            if (!root) return nullptr;
            const auto buttons = root->findChildren<QAbstractButton*>();
            for (QAbstractButton* b : buttons) {
                if (!b) continue;
                const QString t = NormalizeActionText(b->text());
                if (t.compare("Asset Texturing", Qt::CaseInsensitive) == 0) return b;
            }
            for (QAbstractButton* b : buttons) {
                if (!b) continue;
                const QString lower = NormalizeActionText(b->text()).toLower();
                if (lower.contains("asset") && lower.contains("texturing")) return b;
            }
            return nullptr;
        }

        QAbstractItemView* FindProjectTypeItemView(QWidget* root) {
            if (!root) return nullptr;
            const auto views = root->findChildren<QAbstractItemView*>();
            QAbstractItemView* best = nullptr;
            int bestScore = 0;

            for (QAbstractItemView* v : views) {
                if (!v || !v->model()) continue;
                const int rows = v->model()->rowCount(v->rootIndex());
                if (rows <= 0 || rows > 50) continue;

                int hits = 0;
                bool hasAsset = false;
                for (int r = 0; r < rows; ++r) {
                    const QModelIndex idx = v->model()->index(r, 0, v->rootIndex());
                    const QString txt = idx.data(Qt::DisplayRole).toString().trimmed().toLower();
                    if (txt == "asset texturing") hasAsset = true;
                    if (txt == "asset texturing" ||
                        txt == "material layering" ||
                        txt == "materialize image" ||
                        txt == "element graph" ||
                        txt == "npass element graph" ||
                        txt == "atom graph" ||
                        txt == "function graph") {
                        hits++;
                    }
                }

                int score = 0;
                if (hasAsset) score += 50;
                score += hits * 10;
                // Prefer views that are visible/enabled.
                if (v->isVisible()) score += 10;
                if (v->isEnabled()) score += 5;

                if (score > bestScore) {
                    bestScore = score;
                    best = v;
                }
            }

            return (bestScore >= 30) ? best : nullptr;
        }

        bool ActivateItemViewIndex(QAbstractItemView* view, const QModelIndex& idx) {
            if (!view || !idx.isValid()) return false;
            bool invoked = false;
            invoked |= QMetaObject::invokeMethod(view, "activated", Qt::DirectConnection, Q_ARG(QModelIndex, idx));
            invoked |= QMetaObject::invokeMethod(view, "doubleClicked", Qt::DirectConnection, Q_ARG(QModelIndex, idx));
            invoked |= QMetaObject::invokeMethod(view, "clicked", Qt::DirectConnection, Q_ARG(QModelIndex, idx));
            QCoreApplication::processEvents();
            return invoked;
        }

        bool ActivateProjectTypeAssetTexturing(QWidget* chooserRoot, QString* outError) {
            if (outError) outError->clear();
            if (!chooserRoot) {
                if (outError) *outError = "Chooser root is null.";
                return false;
            }

            // 1) Buttons path.
            if (QAbstractButton* assetBtn = FindAssetTexturingButton(chooserRoot)) {
                assetBtn->click();
                QCoreApplication::processEvents();
                return true;
            }

            // 2) ItemView path.
            QAbstractItemView* view = FindProjectTypeItemView(chooserRoot);
            if (!view || !view->model()) {
                if (outError) *outError = "Could not locate project type selector (no button and no item view matched).";
                return false;
            }

            QModelIndex assetIdx;
            const int rows = view->model()->rowCount(view->rootIndex());
            for (int r = 0; r < rows; ++r) {
                const QModelIndex idx = view->model()->index(r, 0, view->rootIndex());
                const QString txt = idx.data(Qt::DisplayRole).toString().trimmed();
                if (txt.compare("Asset Texturing", Qt::CaseInsensitive) == 0) {
                    assetIdx = idx;
                    break;
                }
            }
            if (!assetIdx.isValid()) {
                // Loose fallback.
                for (int r = 0; r < rows; ++r) {
                    const QModelIndex idx = view->model()->index(r, 0, view->rootIndex());
                    const QString lower = idx.data(Qt::DisplayRole).toString().trimmed().toLower();
                    if (lower.contains("asset") && lower.contains("texturing")) {
                        assetIdx = idx;
                        break;
                    }
                }
            }

            if (!assetIdx.isValid()) {
                if (outError) *outError = "Project type selector found, but could not find 'Asset Texturing' entry.";
                return false;
            }

            // Select.
            view->setFocus();
            view->setCurrentIndex(assetIdx);
            if (view->selectionModel()) {
                view->selectionModel()->select(assetIdx, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
            }
            view->scrollTo(assetIdx);
            QCoreApplication::processEvents();

            // Activate via view signals (API-level, no mouse/keyboard injection).
            if (!ActivateItemViewIndex(view, assetIdx)) {
                qInfo().noquote() << "[InstaMAT2Remix] ItemView activation signals not available; relying on selection change.";
            }

            return true;
        }

        QAbstractItemView* FindTemplateItemView(QWidget* root) {
            if (!root) return nullptr;
            const auto views = root->findChildren<QAbstractItemView*>();

            QAbstractItemView* best = nullptr;
            int bestScore = 0;

            for (QAbstractItemView* v : views) {
                if (!v || !v->model()) continue;

                const int rows = v->model()->rowCount(v->rootIndex());
                if (rows <= 0 || rows > 200) continue;

                int score = 0;
                const QString obj = v->objectName().toLower();
                if (obj.contains("template")) score += 40;
                if (obj.contains("tutorial")) score += 25;

                if (v->isVisible()) score += 10;
                if (v->isEnabled()) score += 5;

                // Sample a few entries for "template-like" content.
                const int sample = qMin(rows, 20);
                int nonEmpty = 0;
                for (int r = 0; r < sample; ++r) {
                    const QModelIndex idx = v->model()->index(r, 0, v->rootIndex());
                    const QString txt = idx.data(Qt::DisplayRole).toString().trimmed();
                    if (txt.isEmpty()) continue;
                    nonEmpty++;

                    const QString lower = txt.toLower();
                    if (lower.contains("instamat")) score += 10;
                    if (lower.contains("crate")) score += 10;
                    if (lower.contains("template")) score += 6;
                    if (lower.contains("tutorial")) score += 6;
                }
                if (nonEmpty == 0) continue;

                if (score > bestScore) {
                    bestScore = score;
                    best = v;
                }
            }

            return (bestScore >= 12) ? best : nullptr;
        }

        bool SelectAnyTemplate(QWidget* wizardRoot) {
            QAbstractItemView* view = FindTemplateItemView(wizardRoot);
            if (!view || !view->model()) return false;

            const int rows = view->model()->rowCount(view->rootIndex());
            if (rows <= 0) return false;

            // Prefer a recognizable built-in template if present.
            QModelIndex chosen;
            const int sample = qMin(rows, 50);
            for (int r = 0; r < sample; ++r) {
                const QModelIndex idx = view->model()->index(r, 0, view->rootIndex());
                const QString txt = idx.data(Qt::DisplayRole).toString().trimmed().toLower();
                if (txt.contains("crate") || txt.contains("instamat")) {
                    chosen = idx;
                    break;
                }
            }
            if (!chosen.isValid()) chosen = view->model()->index(0, 0, view->rootIndex());
            if (!chosen.isValid()) return false;

            view->setFocus();
            view->setCurrentIndex(chosen);
            if (view->selectionModel()) {
                view->selectionModel()->select(chosen, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
            }
            view->scrollTo(chosen);
            QCoreApplication::processEvents();

            // Activate template via view signals (API-level, no mouse/keyboard injection).
            if (!ActivateItemViewIndex(view, chosen)) {
                qInfo().noquote() << "[InstaMAT2Remix] Template activation signals not available; relying on selection change.";
            }

            return true;
        }

        QAbstractButton* FindToggleNearLabel(QWidget* root, const QStringList& needlesLower) {
            if (!root) return nullptr;
            const auto labels = root->findChildren<QLabel*>();
            const auto buttons = root->findChildren<QAbstractButton*>();

            QAbstractButton* best = nullptr;
            long long bestDist2 = LLONG_MAX;

            for (QLabel* l : labels) {
                if (!l || !l->isVisible()) continue;
                const QString t = NormalizeActionText(l->text()).toLower();
                if (t.isEmpty()) continue;
                bool ok = true;
                for (const QString& n : needlesLower) {
                    if (!t.contains(n)) {
                        ok = false;
                        break;
                    }
                }
                if (!ok) continue;

                const QPoint lc = l->mapToGlobal(l->rect().center());
                for (QAbstractButton* b : buttons) {
                    if (!b || !b->isVisible() || !b->isEnabled()) continue;
                    if (!b->isCheckable()) continue;

                    const QPoint bc = b->mapToGlobal(b->rect().center());
                    const int dx = bc.x() - lc.x();
                    const int dy = bc.y() - lc.y();

                    // Prefer toggles to the right of the label and on the same row.
                    if (dx < 0) continue;
                    if (qAbs(dy) > 80) continue;

                    const long long d2 = 1LL * dx * dx + 1LL * dy * dy;
                    if (d2 < bestDist2) {
                        bestDist2 = d2;
                        best = b;
                    }
                }
            }
            return best;
        }

        QAbstractButton* FindBakeMeshToggle(QWidget* root) {
            if (!root) return nullptr;

            const auto buttons = root->findChildren<QAbstractButton*>();
            for (QAbstractButton* b : buttons) {
                if (!b) continue;
                const QString t = NormalizeActionText(b->text());
                if (t.isEmpty()) continue;
                const QString lower = t.toLower();
                if (lower.contains("bake") && lower.contains("mesh")) return b;
            }
            // Fallback: objectName heuristics (some styles use unlabeled toggle + separate label)
            for (QAbstractButton* b : buttons) {
                if (!b) continue;
                const QString name = b->objectName().toLower();
                if (name.contains("bake") && name.contains("mesh")) return b;
            }
            // Fallback: find a checkable toggle near a "Bake Mesh" label (common in modern UIs).
            return FindToggleNearLabel(root, {"bake", "mesh"});
        }

        struct AccFindResult {
            QAccessibleInterface* iface = nullptr;
            QString name;
            QString value;
            QString desc;
        };

        QString AccStr(const QString& s) {
            QString out = s;
            out.replace("\n", "\\n");
            out.replace("\r", "");
            return out.trimmed();
        }

        AccFindResult FindAccessibleByText(QAccessibleInterface* root,
                                           const QStringList& needlesLower,
                                           int maxNodes = 12000) {
            AccFindResult res;
            if (!root) return res;

            // BFS over accessibility tree.
            struct Item {
                QAccessibleInterface* iface;
                int depth;
            };
            QVector<Item> queue;
            queue.reserve(256);
            queue.push_back({root, 0});

            int head = 0;
            int visited = 0;
            while (head < queue.size() && visited < maxNodes) {
                Item it = queue[head++];
                if (!it.iface) continue;
                visited++;

                const QString name = it.iface->text(QAccessible::Name);
                const QString value = it.iface->text(QAccessible::Value);
                const QString desc = it.iface->text(QAccessible::Description);

                QString hay = (name + " " + value + " " + desc).toLower();
                bool ok = true;
                for (const QString& n : needlesLower) {
                    if (!hay.contains(n)) {
                        ok = false;
                        break;
                    }
                }
                if (ok) {
                    res.iface = it.iface;
                    res.name = name;
                    res.value = value;
                    res.desc = desc;
                    return res;
                }

                const int childCount = it.iface->childCount();
                if (childCount <= 0) continue;
                // Keep traversal cheap: avoid exploding on huge trees.
                const int limit = qMin(childCount, 100);
                for (int i = 0; i < limit; ++i) {
                    if (QAccessibleInterface* ch = it.iface->child(i)) {
                        queue.push_back({ch, it.depth + 1});
                    }
                }
            }
            return res;
        }

        bool AccDoPress(QAccessibleInterface* iface) {
            if (!iface) return false;
            if (auto* act = iface->actionInterface()) {
                const QString press = QAccessibleActionInterface::pressAction();
                const QStringList actions = act->actionNames();
                if (actions.contains(press)) {
                    act->doAction(press);
                    return true;
                }
                // Fallback: try first action.
                if (!actions.isEmpty()) {
                    act->doAction(actions.first());
                    return true;
                }
            }
            return false;
        }

        bool AccEnsureChecked(QAccessibleInterface* iface) {
            if (!iface) return false;
            const QAccessible::State st = iface->state();
            if (st.checked) return true;
            if (auto* act = iface->actionInterface()) {
                const QString toggle = QAccessibleActionInterface::toggleAction();
                const QStringList actions = act->actionNames();
                if (actions.contains(toggle)) {
                    act->doAction(toggle);
                    return true;
                }
                // Some controls expose "press" only.
                const QString press = QAccessibleActionInterface::pressAction();
                if (actions.contains(press)) {
                    act->doAction(press);
                    return true;
                }
            }
            return false;
        }

        bool AccEnsureUnchecked(QAccessibleInterface* iface) {
            if (!iface) return false;
            const QAccessible::State st = iface->state();
            if (!st.checked) return true;
            if (auto* act = iface->actionInterface()) {
                const QString toggle = QAccessibleActionInterface::toggleAction();
                const QStringList actions = act->actionNames();
                if (actions.contains(toggle)) {
                    act->doAction(toggle);
                    return true;
                }
                // Some controls expose "press" only.
                const QString press = QAccessibleActionInterface::pressAction();
                if (actions.contains(press)) {
                    act->doAction(press);
                    return true;
                }
            }
            return false;
        }

        bool AccSetValueText(QAccessibleInterface* iface, const QString& value) {
            if (!iface) return false;

            // Try value interface.
            if (auto* v = iface->valueInterface()) {
                v->setCurrentValue(value);
                const QString after = iface->text(QAccessible::Value);
                if (!after.isEmpty() && after.contains(value, Qt::CaseInsensitive)) return true;
                return true;
            }

            // Try editable text interface.
            if (auto* editable = iface->editableTextInterface()) {
                const QString before = iface->text(QAccessible::Value);
                const int len = before.size();
                editable->replaceText(0, len, value);
                const QString after = iface->text(QAccessible::Value);
                if (!after.isEmpty() && after.contains(value, Qt::CaseInsensitive)) return true;
            }

            // Try interface setText on Value.
            iface->setText(QAccessible::Value, value);

            // Heuristic: if it now reports the value, treat as success.
            const QString after = iface->text(QAccessible::Value);
            if (!after.isEmpty() && after.contains(value, Qt::CaseInsensitive)) return true;
            return false;
        }

        bool IsEditableAccessible(QAccessibleInterface* iface) {
            if (!iface) return false;
            if (iface->valueInterface()) return true;
            if (iface->editableTextInterface()) return true;
            const auto role = iface->role();
            return role == QAccessible::EditableText || role == QAccessible::ComboBox || role == QAccessible::Document;
        }

        AccFindResult FindAccessibleEditableByText(QAccessibleInterface* root,
                                                   const QStringList& needlesLower,
                                                   int maxNodes = 12000);

        QAccessibleInterface* FindEditableInSubtree(QAccessibleInterface* root, int maxNodes = 400) {
            if (!root) return nullptr;
            struct Item { QAccessibleInterface* iface; int depth; };
            QVector<Item> queue;
            queue.reserve(128);
            queue.push_back({root, 0});

            int head = 0;
            int visited = 0;
            while (head < queue.size() && visited < maxNodes) {
                Item it = queue[head++];
                if (!it.iface) continue;
                visited++;
                if (IsEditableAccessible(it.iface)) return it.iface;

                const int childCount = it.iface->childCount();
                if (childCount <= 0) continue;
                const int limit = qMin(childCount, 100);
                for (int i = 0; i < limit; ++i) {
                    if (QAccessibleInterface* ch = it.iface->child(i)) {
                        queue.push_back({ch, it.depth + 1});
                    }
                }
            }
            return nullptr;
        }

        QAccessibleInterface* FindEditableNearLabel(QAccessibleInterface* labelIface, int maxUp = 4) {
            if (!labelIface) return nullptr;
            QAccessibleInterface* cur = labelIface;
            for (int up = 0; up < maxUp && cur; ++up) {
                QAccessibleInterface* parent = cur->parent();
                if (!parent) break;

                const int childCount = parent->childCount();
                const int limit = qMin(childCount, 120);
                for (int i = 0; i < limit; ++i) {
                    QAccessibleInterface* ch = parent->child(i);
                    if (!ch || ch == cur) continue;
                    if (IsEditableAccessible(ch)) return ch;
                    if (QAccessibleInterface* sub = FindEditableInSubtree(ch, 120)) return sub;
                }
                cur = parent;
            }
            return nullptr;
        }

        bool AccSetFieldByLabel(QAccessibleInterface* root,
                                const QStringList& labelKeywords,
                                const QString& value) {
            if (!root) return false;
            auto direct = FindAccessibleEditableByText(root, labelKeywords, 6000);
            if (direct.iface && AccSetValueText(direct.iface, value)) return true;

            auto label = FindAccessibleByText(root, labelKeywords, 6000);
            if (!label.iface) return false;

            if (QAccessibleInterface* near = FindEditableNearLabel(label.iface)) {
                if (AccSetValueText(near, value)) return true;
            }
            return false;
        }

        bool AccSelectValueByText(QAccessibleInterface* root,
                                  const QStringList& valueKeywords,
                                  int maxNodes = 12000) {
            if (!root) return false;
            auto item = FindAccessibleByText(root, valueKeywords, maxNodes);
            if (item.iface) return AccDoPress(item.iface);
            return false;
        }

        AccFindResult FindAccessibleEditableByText(QAccessibleInterface* root,
                                                   const QStringList& needlesLower,
                                                   int maxNodes) {
            AccFindResult res;
            if (!root) return res;

            // BFS over accessibility tree, preferring editable targets.
            struct Item {
                QAccessibleInterface* iface;
                int depth;
            };
            QVector<Item> queue;
            queue.reserve(256);
            queue.push_back({root, 0});

            int head = 0;
            int visited = 0;
            while (head < queue.size() && visited < maxNodes) {
                Item it = queue[head++];
                if (!it.iface) continue;
                visited++;

                if (!IsEditableAccessible(it.iface)) {
                    const int childCount = it.iface->childCount();
                    if (childCount <= 0) continue;
                    const int limit = qMin(childCount, 100);
                    for (int i = 0; i < limit; ++i) {
                        if (QAccessibleInterface* ch = it.iface->child(i)) {
                            queue.push_back({ch, it.depth + 1});
                        }
                    }
                    continue;
                }

                const QString name = it.iface->text(QAccessible::Name);
                const QString value = it.iface->text(QAccessible::Value);
                const QString desc = it.iface->text(QAccessible::Description);

                QString hay = (name + " " + value + " " + desc).toLower();
                bool ok = true;
                for (const QString& n : needlesLower) {
                    if (!hay.contains(n)) {
                        ok = false;
                        break;
                    }
                }
                if (ok) {
                    res.iface = it.iface;
                    res.name = name;
                    res.value = value;
                    res.desc = desc;
                    return res;
                }

                const int childCount = it.iface->childCount();
                if (childCount <= 0) continue;
                const int limit = qMin(childCount, 100);
                for (int i = 0; i < limit; ++i) {
                    if (QAccessibleInterface* ch = it.iface->child(i)) {
                        queue.push_back({ch, it.depth + 1});
                    }
                }
            }
            return res;
        }

        bool AccessibleLooksLikeNewProject(QAccessibleInterface* root) {
            if (!root) return false;
            int hits = 0;
            if (FindAccessibleByText(root, {"new", "project"}, 6000).iface) hits++;
            if (FindAccessibleByText(root, {"asset", "texturing"}, 6000).iface) hits++;
            if (FindAccessibleByText(root, {"mesh"}, 6000).iface) hits++;
            return hits >= 2;
        }

        bool EnsureQtAccessibilityActive(QString* outError) {
            if (outError) outError->clear();
            if (QAccessible::isActive()) return true;

            // InstaMAT uses a QtQuick/QML UI for the New Project workflow. If Qt accessibility is not active,
            // QAccessibleInterface scanning cannot "see" the UI at all.
            qInfo().noquote() << "[InstaMAT2Remix]" << "QAccessible active: false";
            qInfo().noquote() << "[InstaMAT2Remix]" << "Requesting accessibility activation via QAccessible::setActive(true)";

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
            // Best-effort: some hosts only honor accessibility when the env-var is present at runtime.
            // Note: many Qt settings are read at startup, but this can still help for backends loaded lazily.
            qputenv("QT_ACCESSIBILITY", "1");
            QAccessible::setActive(true);
#else
            // Qt 5 does not provide QAccessible::setActive(). Host must enable accessibility before launch.
#endif

            // Allow time for the accessibility backend to initialize.
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            {
                QEventLoop loop;
                QTimer::singleShot(300, &loop, &QEventLoop::quit);
                loop.exec();
            }

            if (QAccessible::isActive()) {
                qInfo().noquote() << "[InstaMAT2Remix]" << "Accessibility active after activation request";
                return true;
            }

            qWarning().noquote() << "[InstaMAT2Remix]" << "Accessibility inactive after activation request";
            if (outError) {
                *outError =
                    "Qt accessibility is inactive in this InstaMAT session.\n"
                    "This plugin relies on Qt accessibility (QAccessible) to automate the QtQuick/QML New Project UI.\n\n"
                    "If activation via QAccessible::setActive(true) is ignored by the host/runtime, InstaMAT must be launched with accessibility enabled.\n"
                    "Try setting QT_ACCESSIBILITY=1 before launching InstaMAT.";
            }
            return false;
        }

        bool TryCreateTexturingProjectFromMeshAccessible(QObject* rootObj,
                                                         const QString& meshPathAbs,
                                                         const QString& suggestedName,
                                                         QString* outError) {
            if (outError) outError->clear();
            if (!rootObj) {
                if (outError) *outError = "Accessible fallback failed: root object is null.";
                return false;
            }

            if (auto* win = qobject_cast<QWidget*>(rootObj)) {
                win->raise();
                win->activateWindow();
            } else if (auto* window = qobject_cast<QWindow*>(rootObj)) {
                window->raise();
                window->requestActivate();
            }
            QCoreApplication::processEvents();

            // Root accessibility interface.
            QAccessibleInterface* root = QAccessible::queryAccessibleInterface(rootObj);
            if (!root) {
                if (outError) *outError = "Accessible fallback failed: no accessibility interface for root object.";
                return false;
            }

            // Step 1: If the "New Project" chooser is visible, activate Asset Texturing.
            // Only do this if we actually need to - don't keep clicking it
            {
                auto asset = FindAccessibleByText(root, {"asset", "texturing"});
                if (asset.iface && !asset.iface->state().selected && !asset.iface->state().focused) {
                    AccDoPress(asset.iface);
                    QCoreApplication::processEvents();
                    
                    // Wait a bit for UI to transition
                    QEventLoop loop;
                    QTimer::singleShot(500, &loop, &QEventLoop::quit);
                    loop.exec();
                }
            }

            // Refresh root (tree may change after navigation).
            root = QAccessible::queryAccessibleInterface(rootObj);
            if (!root) {
                if (outError) *outError = "Accessible fallback failed: lost accessibility root after navigation.";
                return false;
            }

            // Step 2: Set project metadata (name/category/type) before mesh assignment.
            {
                const QString projectName = suggestedName.isEmpty() ? "Remix" : suggestedName;
                if (!projectName.isEmpty()) {
                    bool nameSet = AccSetFieldByLabel(root, {"project", "name"}, projectName);
                    if (!nameSet) nameSet = AccSetFieldByLabel(root, {"name"}, projectName);
                    if (!nameSet) AccSelectValueByText(root, {projectName.toLower()}, 6000);
                }

                bool categorySet = AccSetFieldByLabel(root, {"category"}, "Materials/Assets");
                if (!categorySet) categorySet = AccSetFieldByLabel(root, {"category"}, "Materials / Assets");
                if (!categorySet) AccSelectValueByText(root, {"materials", "assets"}, 8000);

                bool typeSet = AccSetFieldByLabel(root, {"type"}, "Multi-Material");
                if (!typeSet) typeSet = AccSetFieldByLabel(root, {"type"}, "Multi Material");
                if (!typeSet) AccSelectValueByText(root, {"multi", "material"}, 8000);
            }

            // Step 3: Select a template if one is visible.
            // Prefer "crate" if present.
            {
                auto crate = FindAccessibleByText(root, {"crate"});
                if (crate.iface) {
                    AccDoPress(crate.iface);
                    QCoreApplication::processEvents();
                } else {
                    // Fallback: any "template" looking item.
                    auto templ = FindAccessibleByText(root, {"template"});
                    if (templ.iface) {
                        AccDoPress(templ.iface);
                        QCoreApplication::processEvents();
                    }
                }
            }

            // Step 4: Ensure bake mesh toggle is disabled (no auto-bake on pull).
            {
                auto bake = FindAccessibleByText(root, {"bake", "mesh"});
                if (bake.iface) {
                    AccEnsureUnchecked(bake.iface);
                    QCoreApplication::processEvents();
                }
            }

            // Step 5: Fill mesh field.
            bool meshSet = false;
            {
                auto noMesh = FindAccessibleByText(root, {"no", "mesh"});
                if (noMesh.iface && IsEditableAccessible(noMesh.iface)) {
                    meshSet = AccSetValueText(noMesh.iface, meshPathAbs);
                    QCoreApplication::processEvents();
                }

                if (!meshSet) {
                    auto meshField = FindAccessibleEditableByText(root, {"mesh"});
                    if (meshField.iface) {
                        meshSet = AccSetValueText(meshField.iface, meshPathAbs);
                        QCoreApplication::processEvents();
                    }
                }

                if (!meshSet) {
                    auto meshLabel = FindAccessibleByText(root, {"mesh"});
                    if (meshLabel.iface) {
                        AccDoPress(meshLabel.iface);
                        QCoreApplication::processEvents();
                    }
                    root = QAccessible::queryAccessibleInterface(rootObj);
                    if (root) {
                        auto meshField = FindAccessibleEditableByText(root, {"mesh"});
                        if (meshField.iface) {
                            meshSet = AccSetValueText(meshField.iface, meshPathAbs);
                            QCoreApplication::processEvents();
                        }
                    }
                }
            }

            // Step 5: Press Create/Select-mesh-to-create button.
            bool createPressed = false;
            {
                auto create = FindAccessibleByText(root, {"create", "project"});
                if (create.iface && !create.iface->state().disabled) {
                    createPressed = AccDoPress(create.iface);
                    QCoreApplication::processEvents();
                }

                if (!createPressed) {
                    auto selectMesh = FindAccessibleByText(root, {"select", "mesh", "project"});
                    if (selectMesh.iface && !selectMesh.iface->state().disabled) {
                        createPressed = AccDoPress(selectMesh.iface);
                        QCoreApplication::processEvents();
                    }
                }
            }

            // CRITICAL: Don't click create if we couldn't set the mesh!
            // This prevents the infinite loop where InstaMAT keeps showing the wizard
            if (!meshSet) {
                QStringList parts;
                parts << "Accessible fallback could not set mesh path.";
                parts << "The automation will NOT click 'Create Project' to avoid looping.";
                parts << QString("meshSet=%1 createPressed=%2").arg(meshSet ? "true" : "false").arg(createPressed ? "true" : "false");
                if (!suggestedName.isEmpty()) parts << QString("suggestedName=%1").arg(suggestedName);
                if (outError) *outError = parts.join("\n");
                return false;
            }
            
            // If we set the mesh but couldn't find create button, that's also a problem
            if (!createPressed) {
                QStringList parts;
                parts << "Accessible fallback set mesh but could not find 'Create Project' button.";
                parts << QString("meshSet=%1 createPressed=%2").arg(meshSet ? "true" : "false").arg(createPressed ? "true" : "false");
                if (!suggestedName.isEmpty()) parts << QString("suggestedName=%1").arg(suggestedName);
                if (outError) *outError = parts.join("\n");
                return false;
            }

            // Success heuristic: if "How do you want to get started" disappears, assume project created.
            const qint64 deadline = QDateTime::currentMSecsSinceEpoch() + 15000;
            while (QDateTime::currentMSecsSinceEpoch() < deadline) {
                root = QAccessible::queryAccessibleInterface(rootObj);
                if (!root) break;
                auto how = FindAccessibleByText(root, {"how", "do", "you", "want", "get", "started"}, 4000);
                if (!how.iface) break;
                QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            }

            if (auto* win = qobject_cast<QWidget*>(rootObj)) {
                win->raise();
                win->activateWindow();
            } else if (auto* window = qobject_cast<QWindow*>(rootObj)) {
                window->raise();
                window->requestActivate();
            }
            QCoreApplication::processEvents();
            return true;
        }

        // ---------------------------------------------------------------------------
        // Helper: Run accessibility-based automation across likely roots.
        // ---------------------------------------------------------------------------
        bool TryCreateTexturingProjectFromMeshAccessibleAnyRoot(QMainWindow* win,
                                                               const QString& meshPathAbs,
                                                               const QString& suggestedName,
                                                               QString* outError) {
            if (outError) outError->clear();
            QString accErr;
            if (!QAccessible::isActive()) {
                QString activationErr;
                if (!EnsureQtAccessibilityActive(&activationErr)) {
                    accErr = activationErr;
                }
            }
            if (!QAccessible::isActive()) {
                if (outError) *outError = accErr.isEmpty() ? "Qt accessibility is inactive." : accErr;
                return false;
            }

            if (TryCreateTexturingProjectFromMeshAccessible(win, meshPathAbs, suggestedName, &accErr)) {
                qInfo().noquote() << "[InstaMAT2Remix]" << "Auto-create: accessibility succeeded on main window.";
                return true;
            }

            QWidget* activeModal = QApplication::activeModalWidget();
            if (activeModal && TryCreateTexturingProjectFromMeshAccessible(activeModal, meshPathAbs, suggestedName, &accErr)) {
                qInfo().noquote() << "[InstaMAT2Remix]" << "Auto-create: accessibility succeeded on active modal widget.";
                return true;
            }

            QString accWindowErr;
            const auto topWindows = QGuiApplication::topLevelWindows();
            for (QWindow* window : topWindows) {
                if (!window || !window->isVisible()) continue;
                QAccessibleInterface* rootAcc = QAccessible::queryAccessibleInterface(window);
                if (!AccessibleLooksLikeNewProject(rootAcc)) continue;
                if (TryCreateTexturingProjectFromMeshAccessible(window, meshPathAbs, suggestedName, &accWindowErr)) {
                    qInfo().noquote() << "[InstaMAT2Remix]" << "Auto-create: accessibility succeeded on top-level window.";
                    return true;
                }
            }
            if (!accWindowErr.isEmpty()) accErr += "\n\n" + accWindowErr;
            if (outError) *outError = accErr.isEmpty() ? "Accessibility automation failed." : accErr;
            return false;
        }

        // Dumps a widget tree to qInfo for debugging which QWidgets are in the hierarchy.
        void DumpWidgetTree(QObject* root, int depth = 0, int maxDepth = 4) {
            if (!root || depth > maxDepth) return;
            const QString indent(depth * 2, ' ');
            const QWidget* w = qobject_cast<const QWidget*>(root);
            QString info = QString("%1[%2] name='%3'")
                .arg(indent)
                .arg(root->metaObject()->className())
                .arg(root->objectName());
            if (w) {
                info += QString(" visible=%1 enabled=%2 size=%3x%4")
                    .arg(w->isVisible()).arg(w->isEnabled())
                    .arg(w->width()).arg(w->height());
                if (auto* btn = qobject_cast<const QAbstractButton*>(w)) {
                    info += QString(" text='%1' checkable=%2 checked=%3")
                        .arg(btn->text()).arg(btn->isCheckable()).arg(btn->isChecked());
                }
                if (auto* le = qobject_cast<const QLineEdit*>(w)) {
                    info += QString(" text='%1' placeholder='%2' readOnly=%3")
                        .arg(le->text()).arg(le->placeholderText()).arg(le->isReadOnly());
                }
                if (auto* lbl = qobject_cast<const QLabel*>(w)) {
                    info += QString(" text='%1'").arg(lbl->text().left(80));
                }
            }
            qInfo().noquote() << "[InstaMAT2Remix] TREE:" << info;
            for (QObject* child : root->children()) {
                DumpWidgetTree(child, depth + 1, maxDepth);
            }
        }

        // ---------------------------------------------------------------------------
        // QML Automation Helpers (Reflection-based, no link dependency)
        // ---------------------------------------------------------------------------
        
        QObject* GetQmlRoot(QObject* obj) {
            if (!obj) return nullptr;
            // QQuickWidget has "rootObject" property
            QVariant rootObj = obj->property("rootObject");
            if (rootObj.isValid() && rootObj.value<QObject*>()) return rootObj.value<QObject*>();
            // QQuickWindow has "contentItem" property (root visual item)
            QVariant contentItem = obj->property("contentItem");
            if (contentItem.isValid() && contentItem.value<QObject*>()) return contentItem.value<QObject*>();
            return nullptr;
        }

        // Recursive search for QML item
        QObject* FindQmlItemRecursive(QObject* root, const std::function<bool(QObject*)>& predicate) {
            if (!root) return nullptr;
            if (predicate(root)) return root;
            
            // QObject::findChildren searches recursively
            auto list = root->findChildren<QObject*>();
            for (QObject* obj : list) {
                if (predicate(obj)) return obj;
            }
            return nullptr;
        }

        bool QmlClick(QObject* item) {
            if (!item) return false;
            // Try invoking "click" (Button) or "toggle" (Switch/CheckBox)
            bool invoked = QMetaObject::invokeMethod(item, "click");
            if (!invoked) invoked = QMetaObject::invokeMethod(item, "toggle");
            return invoked;
        }

        bool QmlSetText(QObject* item, const QString& text) {
            if (!item) return false;
            bool ok = item->setProperty("text", text);
            // Also try invoking textEdited signal to notify bindings
            QMetaObject::invokeMethod(item, "textEdited", Q_ARG(QString, text));
            return ok;
        }

        bool TryCreateTexturingProjectFromMeshQml(const QString& meshPath, const QString& suggestedName, QString* outError) {
            qInfo() << "[InstaMAT2Remix] Attempting QML automation...";
            
            // Collect all potential QML roots from top-level widgets and windows
            QList<QObject*> roots;
            for (QWidget* w : QApplication::topLevelWidgets()) {
                if (w && w->isVisible()) {
                    if (QObject* r = GetQmlRoot(w)) roots.append(r);
                }
            }
            for (QWindow* w : QGuiApplication::topLevelWindows()) {
                if (w && w->isVisible()) {
                    if (QObject* r = GetQmlRoot(w)) roots.append(r);
                }
            }

            if (roots.isEmpty()) {
                qInfo() << "[InstaMAT2Remix] No QML roots found.";
                return false;
            }

            for (QObject* root : roots) {
                // 1. Check for Chooser (Asset Texturing button)
                QObject* assetTexturingBtn = FindQmlItemRecursive(root, [](QObject* o) {
                    QString t = o->property("text").toString();
                    return t.contains("Asset Texturing", Qt::CaseInsensitive);
                });

                if (assetTexturingBtn) {
                    qInfo() << "[InstaMAT2Remix] Found QML 'Asset Texturing' button. Clicking.";
                    QmlClick(assetTexturingBtn);
                    
                    // Wait for transition
                    QEventLoop loop;
                    QTimer::singleShot(1000, &loop, &QEventLoop::quit);
                    loop.exec();
                    
                    // Re-scan roots after transition
                    roots.clear();
                    for (QWidget* w : QApplication::topLevelWidgets()) {
                        if (w && w->isVisible()) {
                            if (QObject* r = GetQmlRoot(w)) roots.append(r);
                        }
                    }
                    for (QWindow* w : QGuiApplication::topLevelWindows()) {
                        if (w && w->isVisible()) {
                            if (QObject* r = GetQmlRoot(w)) roots.append(r);
                        }
                    }
                    // Restart loop with new roots
                    // For simplicity, just break and let the next loop iteration handle the wizard
                    // But we need to update 'root' to the new wizard root.
                    // Let's just continue to the Wizard search logic below, iterating all roots again.
                }
            }

            // 2. Search for Wizard elements (Mesh input, Create button)
            for (QObject* root : roots) {
                // Find Mesh Input
                // Look for objectName "meshPath" or placeholderText containing "mesh"
                QObject* meshInput = FindQmlItemRecursive(root, [](QObject* o) {
                    QString ph = o->property("placeholderText").toString();
                    if (ph.contains("mesh", Qt::CaseInsensitive)) return true;
                    if (o->objectName().contains("mesh", Qt::CaseInsensitive)) return true;
                    // Also check if it's a TextField with label "Mesh" nearby? Hard in QML reflection.
                    return false;
                });

                if (meshInput) {
                    qInfo() << "[InstaMAT2Remix] Found QML Mesh input:" << meshInput->objectName();
                    QmlSetText(meshInput, meshPath);
                    
                    // Set Name if found
                    if (!suggestedName.isEmpty()) {
                        QObject* nameInput = FindQmlItemRecursive(root, [](QObject* o) {
                            if (o->objectName().contains("projectName", Qt::CaseInsensitive)) return true;
                            if (o->property("placeholderText").toString().contains("Project Name", Qt::CaseInsensitive)) return true;
                            return false;
                        });
                        if (nameInput) QmlSetText(nameInput, suggestedName);
                    }

                    // Find Create Button
                    QObject* createBtn = FindQmlItemRecursive(root, [](QObject* o) {
                        QString t = o->property("text").toString();
                        return t.compare("Create Project", Qt::CaseInsensitive) == 0 || 
                               t.compare("Create", Qt::CaseInsensitive) == 0;
                    });

                    if (createBtn) {
                        qInfo() << "[InstaMAT2Remix] Found QML Create button. Clicking.";
                        QmlClick(createBtn);
                        return true;
                    } else {
                        if (outError) *outError = "Found QML Mesh input but could not find Create button.";
                    }
                }
            }

            return false;
        }

        // ---------------------------------------------------------------------------
        // Helper: Given that the new-project dialog is (or should be) visible,
        // locates the wizard/chooser, fills in the mesh path, and clicks Create.
        // Called either directly (dialog already visible on start screen) or from
        // within QDialog::exec()'s nested event loop (via QTimer::singleShot).
        // ---------------------------------------------------------------------------
        bool FillProjectWizardAndCreate(QMainWindow* win,
                                         QWidget* wizardRoot,
                                         QWidget* chooserRoot,
                                         const QString& meshPathAbs,
                                         const QString& suggestedName,
                                         QString* outError) {
            if (outError) outError->clear();

            qInfo().noquote() << "[InstaMAT2Remix] FillProjectWizardAndCreate: wizardRoot="
                << (wizardRoot ? wizardRoot->metaObject()->className() : "null")
                << "chooserRoot=" << (chooserRoot ? chooserRoot->metaObject()->className() : "null")
                << "mesh=" << meshPathAbs << "name=" << suggestedName;

            // If we're on the chooser screen, click Asset Texturing and wait for the actual wizard UI.
            if (!wizardRoot && chooserRoot) {
                QString activateErr;
                if (!ActivateProjectTypeAssetTexturing(chooserRoot, &activateErr)) {
                    qInfo().noquote() << "[InstaMAT2Remix] QWidget activation failed:" << activateErr
                        << "- falling back to accessibility automation.";
                    // QWidget activation failed — don't return false yet, try accessibility below.
                } else {
                    const qint64 wizardDeadline = QDateTime::currentMSecsSinceEpoch() + 10000;
                    while (QDateTime::currentMSecsSinceEpoch() < wizardDeadline) {
                        wizardRoot = FindProjectWizardRoot(win);
                        if (wizardRoot) break;
                        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
                    }
                }
            }

            if (!wizardRoot) {
                qInfo().noquote() << "[InstaMAT2Remix] Widget wizard not found.";
                
                // Try QML automation first (works without accessibility if we can find QObjects)
                QString qmlErr;
                if (TryCreateTexturingProjectFromMeshQml(meshPathAbs, suggestedName, &qmlErr)) {
                    qInfo().noquote() << "[InstaMAT2Remix] QML automation succeeded.";
                    return true;
                }
                qInfo().noquote() << "[InstaMAT2Remix] QML automation failed:" << qmlErr << "- attempting accessibility automation.";

                QString accErr;
                if (TryCreateTexturingProjectFromMeshAccessibleAnyRoot(win, meshPathAbs, suggestedName, &accErr)) {
                    return true;
                }
                if (outError) *outError = "QML Error: " + qmlErr + "\nAccessibility Error: " + accErr;
                qWarning().noquote() << "[InstaMAT2Remix]" << "Auto-create: accessibility automation failed.";
                return false;
            }

            qInfo().noquote() << "[InstaMAT2Remix] Wizard found:"
                << wizardRoot->metaObject()->className()
                << "name=" << wizardRoot->objectName()
                << "title=" << wizardRoot->windowTitle()
                << "children=" << wizardRoot->children().count();

            // Dump the wizard widget tree so we can see what QWidgets are available.
            DumpWidgetTree(wizardRoot, 0, 5);

            // Best-effort: set name/category/type via accessibility even in QWidget builds.
            if (!QAccessible::isActive()) {
                QString activationErr;
                EnsureQtAccessibilityActive(&activationErr);
            }
            if (QAccessible::isActive()) {
                if (QAccessibleInterface* accRoot = QAccessible::queryAccessibleInterface(wizardRoot)) {
                    const QString projectName = suggestedName.isEmpty() ? "Remix" : suggestedName;
                    if (!projectName.isEmpty()) {
                        if (!AccSetFieldByLabel(accRoot, {"project", "name"}, projectName)) {
                            AccSetFieldByLabel(accRoot, {"name"}, projectName);
                        }
                    }
                    if (!AccSetFieldByLabel(accRoot, {"category"}, "Materials/Assets")) {
                        AccSetFieldByLabel(accRoot, {"category"}, "Materials / Assets");
                    }
                    if (!AccSetFieldByLabel(accRoot, {"type"}, "Multi-Material")) {
                        AccSetFieldByLabel(accRoot, {"type"}, "Multi Material");
                    }
                }
            }

            if (!suggestedName.isEmpty()) {
                QLineEdit* nameEdit = FindProjectNameLineEdit(wizardRoot);
                qInfo().noquote() << "[InstaMAT2Remix] FindProjectNameLineEdit:" << (nameEdit ? "found" : "null");
                if (nameEdit) {
                    nameEdit->setText(suggestedName);
                    nameEdit->setCursorPosition(suggestedName.size());
                }
            }

            // Many InstaMAT builds require choosing a template before mesh selection / create becomes enabled.
            // Best-effort: select any available template.
            SelectAnyTemplate(wizardRoot);

            // Disable bake-on-create if available (no auto-bake on pull).
            if (QAbstractButton* bakeToggle = FindBakeMeshToggle(wizardRoot)) {
                if (bakeToggle->isEnabled()) {
                    if (bakeToggle->isCheckable()) {
                        if (bakeToggle->isChecked()) {
                            bakeToggle->click();
                            QCoreApplication::processEvents();
                        }
                    }
                }
            }

            QPointer<QAbstractButton> createBtn = FindCreateProjectButton(wizardRoot);
            if (!createBtn) {
                qWarning().noquote() << "[InstaMAT2Remix] Could not find Create button. Dumping wizard widget tree:";
                DumpWidgetTree(wizardRoot, 0, 6);
                
                // Also dump all top-level widgets for context
                qInfo().noquote() << "[InstaMAT2Remix] --- Top-level widgets ---";
                for (QWidget* tlw : QApplication::topLevelWidgets()) {
                    if (!tlw || !tlw->isVisible()) continue;
                    qInfo().noquote() << "[InstaMAT2Remix] TLW:" << tlw->metaObject()->className()
                        << "name=" << tlw->objectName()
                        << "title=" << tlw->windowTitle()
                        << "size=" << tlw->width() << "x" << tlw->height();
                }
                
                // Try scanning ALL top-level widgets for the button (it might be in a different widget)
                for (QWidget* tlw : QApplication::topLevelWidgets()) {
                    if (!tlw || !tlw->isVisible()) continue;
                    QAbstractButton* btn = FindCreateProjectButton(tlw);
                    if (btn) {
                        qInfo().noquote() << "[InstaMAT2Remix] Found create button in different widget:" 
                            << tlw->metaObject()->className() << "name=" << tlw->objectName();
                        createBtn = btn;
                        break;
                    }
                }
                
                // Also try looking for the button via activeModalWidget
                if (!createBtn) {
                    QWidget* modal = QApplication::activeModalWidget();
                    if (modal && modal != wizardRoot) {
                        qInfo().noquote() << "[InstaMAT2Remix] Checking active modal widget:" 
                            << modal->metaObject()->className() << "name=" << modal->objectName();
                        createBtn = FindCreateProjectButton(modal);
                    }
                }
                
                if (!createBtn) {
                    // Last resort: fall back to accessibility automation for the entire flow.
                    qInfo().noquote() << "[InstaMAT2Remix] QWidget Create button not found — trying accessibility automation.";
                    QString accErr;
                    if (TryCreateTexturingProjectFromMeshAccessibleAnyRoot(win, meshPathAbs, suggestedName, &accErr)) {
                        return true;
                    }
                    if (outError) *outError = "Could not find the project creation button in the UI. Accessibility fallback: " + accErr;
                    return false;
                }
            }

            // Try multiple strategies to set the mesh in a way the UI accepts.
            // Some builds validate on user edits rather than programmatic setText(), so we emit edit signals.
            QPointer<QLineEdit> meshEdit = FindMeshLineEdit(wizardRoot);
            QPointer<QComboBox> meshCombo = FindMeshComboBox(wizardRoot);
            const QString meshForward = QDir::cleanPath(meshPathAbs);
            const QString meshNative = QDir::toNativeSeparators(meshForward);

            auto wizardClosed = [&]() -> bool {
                return !FindProjectWizardRoot(win) && !FindNewProjectChooserRoot(win);
            };

            auto waitCreateEnabled = [&](int timeoutMs) -> bool {
                const qint64 deadline = QDateTime::currentMSecsSinceEpoch() + timeoutMs;
                while (QDateTime::currentMSecsSinceEpoch() < deadline) {
                    if (wizardClosed()) return true;
                    if (!createBtn) return false;
                    if (createBtn->isEnabled()) return true;
                    // Give UI more time to process events and validate
                    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
                }
                return wizardClosed() || (createBtn && createBtn->isEnabled());
            };

            auto tryApplyMesh = [&](const QString& path) -> bool {
                if (path.isEmpty()) return false;

                // 1) Programmatic text set + signal emission (API-level, no input injection).
                if (meshEdit) {
                    meshEdit->setText(path);
                    meshEdit->setCursorPosition(path.size());
                    meshEdit->setModified(true);
                    QMetaObject::invokeMethod(meshEdit, "textEdited", Qt::DirectConnection, Q_ARG(QString, path));
                    QMetaObject::invokeMethod(meshEdit, "editingFinished", Qt::DirectConnection);
                    QCoreApplication::processEvents();
                }

                if (meshCombo) {
                    try {
                        if (meshCombo->isEditable()) {
                            if (QLineEdit* le = meshCombo->lineEdit()) {
                                le->setText(path);
                                le->setCursorPosition(path.size());
                                le->setModified(true);
                                QMetaObject::invokeMethod(le, "textEdited", Qt::DirectConnection, Q_ARG(QString, path));
                                QMetaObject::invokeMethod(le, "editingFinished", Qt::DirectConnection);
                            } else {
                                meshCombo->setEditText(path);
                            }
                            QMetaObject::invokeMethod(meshCombo, "editTextChanged", Qt::DirectConnection, Q_ARG(QString, path));
                        }
                        QMetaObject::invokeMethod(meshCombo, "currentTextChanged", Qt::DirectConnection, Q_ARG(QString, path));
                        QCoreApplication::processEvents();
                    } catch (...) {
                        // ignore
                    }
                }

                // Give UI time to validate and enable the create button
                if (waitCreateEnabled(2000)) return true;
                return false;
            };

            auto meshLooksUnset = [&]() -> bool {
                if (meshEdit) {
                    const QString t = meshEdit->text().trimmed().toLower();
                    if (t.isEmpty()) return true;
                    if (t.contains("no mesh")) return true;
                    return false;
                }
                if (meshCombo) {
                    const QString t = meshCombo->currentText().trimmed().toLower();
                    if (t.isEmpty()) return true;
                    if (t.contains("no mesh")) return true;
                    return false;
                }
                // If we can't find a mesh widget at all, assume it's unset and rely on drop-to-root behavior.
                return true;
            };

            // Always try to apply the mesh if it looks unset (regardless of the create button state).
            if (meshLooksUnset()) {
                bool ok = false;
                ok = tryApplyMesh(meshNative);
                if (!ok && meshNative != meshForward) ok = tryApplyMesh(meshForward);
                if (!ok && !wizardClosed()) {
                    const QString ro = meshEdit ? (meshEdit->isReadOnly() ? "true" : "false") : "n/a";
                    if (outError) {
                        *outError = QString("Could not set mesh in the project creation UI. (meshField=%1, readOnly=%2)")
                                        .arg(meshEdit ? "found" : "missing")
                                        .arg(ro);
                    }
                    return false;
                }
            }

            // Some UIs may auto-create a project on drop/paste and close the wizard.
            if (wizardClosed()) {
                win->raise();
                win->activateWindow();
                QCoreApplication::processEvents();
                return true;
            }

            if (!createBtn || !createBtn->isEnabled()) {
                if (outError) *outError = "Project creation button stayed disabled after setting mesh. (UI did not accept mesh path.)";
                return false;
            }

            createBtn->click();
            QCoreApplication::processEvents();

            // Wait until the wizard closes so the created project becomes the active workspace (mirrors Substance2Remix UX).
            // Give extra time for project creation + mesh baking if enabled
            const qint64 closeDeadline = QDateTime::currentMSecsSinceEpoch() + 30000; // Increased to 30s for mesh baking
            while (QDateTime::currentMSecsSinceEpoch() < closeDeadline) {
                if (!FindProjectWizardRoot(win) && !FindNewProjectChooserRoot(win)) break;
                QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
            }

            if (FindProjectWizardRoot(win) || FindNewProjectChooserRoot(win)) {
                if (outError) *outError = "Create Project clicked, but the new project UI did not close (project may not have been created).";
                return false;
            }

            // Ensure host stays foregrounded after automation.
            win->raise();
            win->activateWindow();
            QCoreApplication::processEvents();
            return true;
        }

        // ---------------------------------------------------------------------------
        // Orchestrator: Attempts to automatically create an Asset Texturing project
        // with the given mesh. Handles two cases:
        //   1) The new-project dialog is already visible (start screen) -> interact directly
        //   2) Need to trigger File -> New, which opens a blocking QDialog::exec() ->
        //      schedule the interaction via QTimer to run inside exec()'s event loop
        // ---------------------------------------------------------------------------
        bool TryCreateTexturingProjectFromMesh(const QString& meshPathAbs,
                                               const QString& suggestedName,
                                               QString* outError) {
            if (outError) outError->clear();

            QMainWindow* win = FindHostMainWindow();
            if (!win) {
                if (outError) *outError = "Could not find InstaMAT main window.";
                return false;
            }

            // If the wizard is already visible (e.g. start screen or user opened it), we can just fill it.
            QWidget* wizardRoot = FindProjectWizardRoot(win);
            QWidget* chooserRoot = nullptr;
            if (!wizardRoot) chooserRoot = FindNewProjectChooserRoot(win);

            // Check if user is already interacting with the wizard
            // If mesh field has text other than "No Mesh selected", user is manually working
            if (wizardRoot) {
                QLineEdit* meshEdit = FindMeshLineEdit(wizardRoot);
                if (meshEdit) {
                    QString existingText = meshEdit->text().trimmed();
                    if (!existingText.isEmpty() && 
                        !existingText.contains("No Mesh", Qt::CaseInsensitive) &&
                        !existingText.contains("no mesh", Qt::CaseInsensitive)) {
                        if (outError) *outError = "Wizard is already open with user input. Skipping automation to avoid interfering.";
                        qInfo().noquote() << "[InstaMAT2Remix]" << "Skipping automation - user appears to be manually creating project";
                        return false;
                    }
                }
            }

            // Case 1: Dialog already visible — interact with it directly (no blocking exec()).
            if (wizardRoot || chooserRoot) {
                qInfo().noquote() << "[InstaMAT2Remix]" << "Auto-create: wizard/chooser already visible, interacting directly.";
                return FillProjectWizardAndCreate(win, wizardRoot, chooserRoot, meshPathAbs, suggestedName, outError);
            }

            // Case 2: Need to trigger File -> New (or equivalent menu action).
            // IMPORTANT: The New Project action opens a dialog via QDialog::exec(), which
            // starts a blocking nested event loop. Any code placed after trigger() would
            // only execute AFTER the dialog closes — too late to interact with it.
            // Solution: Schedule the dialog interaction via QTimer::singleShot so it runs
            // from WITHIN the dialog's exec() event loop while the dialog is still open.
            QMenuBar* bar = win->findChild<QMenuBar*>();
            QAction* newAction = FindBestNewProjectAction(bar);
            if (!newAction) {
                if (outError) *outError = "Could not find a 'New Project' action in the menu bar.";
                return false;
            }

            // Use shared state to ensure safety even if trigger() returns unexpectedly early.
            struct AutoState {
                bool success = false;
                QString error;
            };
            auto state = std::make_shared<AutoState>();

            // Schedule the automation to fire inside exec()'s nested event loop.
            // Using 100ms delay to allow dialog initialization but run promptly.
            QTimer::singleShot(100, win, [state, win, meshPathAbs, suggestedName]() {
                qInfo().noquote() << "[InstaMAT2Remix]" << "Auto-create: deferred callback fired inside dialog event loop.";

                // Wait for the dialog UI widgets to become discoverable.
                QWidget* wiz = nullptr;
                QWidget* chooser = nullptr;
                const qint64 deadline = QDateTime::currentMSecsSinceEpoch() + 10000;
                while (QDateTime::currentMSecsSinceEpoch() < deadline) {
                    wiz = FindProjectWizardRoot(win);
                    if (wiz) break;
                    chooser = FindNewProjectChooserRoot(win);
                    if (chooser) break;
                    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
                }

                // Also check top-level modal dialogs (the dialog opened by exec() may not
                // be a child of the main window).
                if (!wiz && !chooser) {
                    QWidget* activeModal = QApplication::activeModalWidget();
                    if (activeModal) {
                        if (HasProjectWizardSignature(activeModal)) wiz = activeModal;
                        else if (HasNewProjectChooserSignature(activeModal)) chooser = activeModal;
                    }
                }

                if (!wiz && !chooser) {
                    qInfo().noquote() << "[InstaMAT2Remix]"
                                      << "Widget detection failed inside dialog - relying on accessibility automation.";
                }

                state->success = FillProjectWizardAndCreate(win, wiz, chooser, meshPathAbs, suggestedName, &state->error);
            });

            // Trigger the action — this calls QDialog::exec() internally, blocking here
            // until the dialog closes. The QTimer callback above fires inside exec()'s
            // event loop, automates the dialog, and (if successful) closes it.
            qInfo().noquote() << "[InstaMAT2Remix]" << "Auto-create: triggering action:" << newAction->text();
            newAction->trigger();

            // exec() has returned — the dialog is closed.
            if (outError) *outError = state->error;
            return state->success;
        }
    } // namespace

    void RemixConnector::SetInstance(RemixConnector* instance) {
        g_instance = instance;
    }

    RemixConnector* RemixConnector::GetInstance() {
        return g_instance;
    }

    RemixConnector::RemixConnector(InstaMAT::IInstaMAT& instaMAT, QObject* parent)
        : QObject(parent), m_instaMAT(instaMAT) {
        m_logger.SetLogFilePath(DefaultLogFilePath());
        ReloadSettings();
        m_logger.Info(QString("Initialized. Plugin dir: %1").arg(GetPluginDirPath()));
    }

    RemixConnector::~RemixConnector() {}

    void RemixConnector::SetRemixApiBaseUrl(const std::string& baseUrl) {
        m_remixApiBaseUrl = baseUrl;
        QSettings settings("InstaMAT2Remix", "Config");
        settings.setValue("RemixApiBaseUrl", QString::fromStdString(baseUrl));
    }

    void RemixConnector::ReloadSettings() {
        QSettings settings("InstaMAT2Remix", "Config");

        const QString baseUrl = settings.value("RemixApiBaseUrl", "http://localhost:8011").toString().trimmed();
        m_remixApiBaseUrl = (baseUrl.isEmpty() ? "http://localhost:8011" : baseUrl).toStdString();

        m_linkedMaterialPrim = settings.value("LinkedMaterialPrim", "").toString().toStdString();
        m_linkedMeshPath = settings.value("LinkedMeshPath", "").toString().toStdString();

        const QString blender = settings.value("BlenderPath", "").toString().trimmed();
        m_tools.SetBlenderExecutable(blender.toStdString());

        QString texconv = settings.value("TexconvPath", "").toString().trimmed();
        if (texconv.isEmpty()) texconv = DetectTexconvPath();
        if (texconv.isEmpty()) texconv = QDir(QCoreApplication::applicationDirPath()).filePath("texconv.exe");
        m_tools.SetTexconvExecutable(texconv.toStdString());

        const QString ll = settings.value("LogLevel", "info").toString();
        m_logger.SetLevel(ParseLogLevel(ll, LogLevel::Info));

        m_logger.Info(QString("Settings reloaded. API=%1 texconv=%2 blender=%3 logLevel=%4")
                          .arg(QString::fromStdString(m_remixApiBaseUrl))
                          .arg(texconv)
                          .arg(blender)
                          .arg(LogLevelToString(m_logger.GetLevel())));
    }

    QString RemixConnector::GetLogFilePath() const {
        return m_logger.GetLogFilePath();
    }

    bool RemixConnector::TestConnection(QString& outMessage) const {
        QString remixDirAbs;
        QString err;
        if (GetRemixDefaultDirectory(remixDirAbs, err)) {
            outMessage = QString("Remix default directory: %1").arg(remixDirAbs);
            return true;
        }
        outMessage = err.isEmpty() ? "Failed to contact RTX Remix API." : err;
        return false;
    }

    QString RemixConnector::BuildDiagnosticsReport() const {
        QSettings settings("InstaMAT2Remix", "Config");

        QString msg;
        const bool ok = TestConnection(msg);

        QStringList lines;
        lines << QString("%1 v%2").arg(kPluginName).arg(kPluginVersion);
        lines << QString("Description: %1").arg(kPluginDescription);
        lines << "";
        lines << QString("Host: %1").arg(QCoreApplication::applicationName());
        lines << QString("Qt: %1").arg(QString::fromLatin1(qVersion()));
        lines << QString("OS: %1 (%2)").arg(QSysInfo::prettyProductName(), QSysInfo::kernelVersion());
        lines << "";
        lines << QString("Plugin Dir: %1").arg(GetPluginDirPath());
        lines << QString("Log File: %1").arg(GetLogFilePath());
        lines << "";
        lines << "=== Settings ===";
        lines << QString("API Base URL: %1").arg(settings.value("RemixApiBaseUrl", "http://localhost:8011").toString());
        lines << QString("Poll Timeout (sec): %1").arg(settings.value("PollTimeoutSec", 60.0).toDouble());
        lines << QString("Log Level: %1").arg(settings.value("LogLevel", "info").toString());
        lines << QString("Remix Output Subfolder: %1").arg(settings.value("RemixOutputSubfolder", "Textures/InstaMAT2Remix_Ingested").toString());
        lines << QString("Export Base Folder: %1").arg(settings.value("ExportBaseFolder", "").toString());
        lines << QString("Export Resolution: %1 x %2").arg(settings.value("ExportWidth", 2048).toInt()).arg(settings.value("ExportHeight", 2048).toInt());
        lines << QString("Export Format: %1").arg(settings.value("ExportFormat", "png").toString());
        lines << QString("Include Opacity Map: %1").arg(settings.value("IncludeOpacityMap", false).toBool() ? "true" : "false");
        lines << QString("Auto Unwrap: %1").arg(settings.value("AutoUnwrap", false).toBool() ? "true" : "false");
        lines << QString("Blender Path: %1").arg(settings.value("BlenderPath", "").toString());
        lines << QString("Texconv Path: %1").arg(settings.value("TexconvPath", "").toString());
        lines << "";
        lines << "=== Link State ===";
        lines << QString("Linked Material Prim: %1").arg(QString::fromStdString(m_linkedMaterialPrim));
        lines << QString("Linked Mesh Path: %1").arg(QString::fromStdString(m_linkedMeshPath));
        lines << "";
        lines << "=== Connectivity ===";
        lines << QString("RTX Remix API: %1").arg(ok ? "OK" : "FAILED");
        lines << msg;

        return lines.join('\n');
    }

    QString RemixConnector::NormalizePathSlashes(const QString& path) {
        QString p = path;
        return p.replace('\\', '/');
    }

    QString RemixConnector::UrlEncodeKeepSlashes(const QString& value) {
        return QString::fromUtf8(QUrl::toPercentEncoding(value, "/"));
    }

    QString RemixConnector::UrlEncodeKeepColonAndSlashes(const QString& value) {
        return QString::fromUtf8(QUrl::toPercentEncoding(value, ":/"));
    }

    QJsonDocument RemixConnector::RequestJson(const QString& method,
                                              const QString& endpoint,
                                              const QMap<QString, QString>& params,
                                              const QJsonDocument* body,
                                              QString* outError) const {
        if (outError) outError->clear();

        QString base = QString::fromStdString(m_remixApiBaseUrl).trimmed();
        if (base.isEmpty()) base = "http://localhost:8011";
        while (base.endsWith('/')) base.chop(1);

        const QString ep = endpoint.startsWith('/') ? endpoint.mid(1) : endpoint;
        QUrl url(base + "/" + ep);

        if (!params.isEmpty()) {
            QUrlQuery q;
            for (auto it = params.begin(); it != params.end(); ++it) q.addQueryItem(it.key(), it.value());
            url.setQuery(q);
        }

        QNetworkRequest req(url);
        req.setRawHeader("Accept", kRemixAccept);
        if (body) req.setRawHeader("Content-Type", kRemixAccept);

        // Respect user-configured timeout if available.
        {
            QSettings settings("InstaMAT2Remix", "Config");
            const double sec = settings.value("PollTimeoutSec", 60.0).toDouble();
            const int timeoutMs = qMax(200, int(sec * 1000.0));
            req.setTransferTimeout(timeoutMs);
        }

        QNetworkAccessManager mgr;
        QNetworkReply* reply = nullptr;

        const QByteArray payload = body ? body->toJson(QJsonDocument::Compact) : QByteArray();
        const QString m = method.toUpper();
        if (m == "GET") reply = mgr.get(req);
        else if (m == "POST") reply = mgr.post(req, payload);
        else if (m == "PUT") reply = mgr.put(req, payload);
        else if (m == "DELETE") reply = mgr.deleteResource(req);
        else {
            if (outError) *outError = "Unsupported HTTP method: " + method;
            return {};
        }

        QEventLoop loop;
        QTimer timeoutTimer;
        timeoutTimer.setSingleShot(true);
        int timeoutMs = 60000;
        {
            QSettings settings("InstaMAT2Remix", "Config");
            const double sec = settings.value("PollTimeoutSec", 60.0).toDouble();
            timeoutMs = qMax(200, int(sec * 1000.0));
        }
        timeoutTimer.start(timeoutMs);
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        QObject::connect(&timeoutTimer, &QTimer::timeout, &loop, &QEventLoop::quit);
        loop.exec();

        if (!reply->isFinished()) {
            reply->abort();
            if (outError) *outError = "RTX Remix API request timed out.";
            reply->deleteLater();
            return {};
        }

        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray bytes = reply->readAll();

        if (reply->error() != QNetworkReply::NoError) {
            QString details = reply->errorString();
            if (!bytes.isEmpty()) details += " | " + QString::fromUtf8(bytes.left(600));
            if (outError) *outError = QString("HTTP %1: %2").arg(status).arg(details);
            reply->deleteLater();
            return {};
        }

        QJsonParseError perr;
        QJsonDocument doc = QJsonDocument::fromJson(bytes, &perr);
        if (perr.error != QJsonParseError::NoError) {
            if (outError) *outError = "Invalid JSON response: " + perr.errorString();
            reply->deleteLater();
            return {};
        }

        reply->deleteLater();
        return doc;
    }

    bool RemixConnector::GetRemixDefaultDirectory(QString& outDirAbs, QString& outError) const {
        const QJsonDocument doc = RequestJson("GET", "/stagecraft/assets/default-directory", {}, nullptr, &outError);
        if (doc.isNull() || !doc.isObject()) return false;

        const QJsonObject data = doc.object();
        const QString dirRaw = data.value("directory_path").toString(data.value("asset_path").toString());
        if (dirRaw.isEmpty()) {
            outError = "Remix returned no default directory_path.";
            return false;
        }
        outDirAbs = QDir::cleanPath(QFileInfo(dirRaw).absoluteFilePath());
        return true;
    }

    bool RemixConnector::GetSelectedMaterialPrim(QString& outMaterialPrim, QString& outError) const {
        outMaterialPrim.clear();
        outError.clear();

        QMap<QString, QString> params;
        params.insert("selection", "true");
        params.insert("filter_session_assets", "false");
        params.insert("exists", "true");

        const QJsonDocument doc = RequestJson("GET", "/stagecraft/assets/", params, nullptr, &outError);
        if (doc.isNull() || !doc.isObject()) return false;

        const QJsonObject data = doc.object();
        QJsonValue pathsVal = data.value("prim_paths");
        if (pathsVal.isUndefined()) pathsVal = data.value("asset_paths");
        if (!pathsVal.isArray()) {
            outError = "Remix selection response missing prim_paths.";
            return false;
        }

        const QJsonArray paths = pathsVal.toArray();
        if (paths.isEmpty()) {
            outError = "No assets are currently selected in RTX Remix.";
            return false;
        }

        for (const QJsonValue& v : paths) {
            if (!v.isString()) continue;
            const QString p = NormalizePathSlashes(v.toString());
            const QString lower = p.toLower();

            if (p.endsWith("/Shader")) {
                outMaterialPrim = QFileInfo(p).dir().path();
                return true;
            }

            const bool looksLikeMaterial = (lower.contains("/looks/") || lower.contains("/materials/") || lower.contains("/material/")) &&
                                           !lower.contains("/previewsurface");
            if (looksLikeMaterial) {
                outMaterialPrim = p;
                return true;
            }
        }

        outError = "Could not identify a material prim from selection.";
        return false;
    }

    bool RemixConnector::GetMaterialFromMeshPrim(const QString& meshPrim, QString& outMaterialPrim, QString& outError) const {
        const QString encoded = UrlEncodeKeepSlashes(NormalizePathSlashes(meshPrim));
        const QJsonDocument doc = RequestJson("GET", QString("/stagecraft/assets/%1/material").arg(encoded), {}, nullptr, &outError);
        if (doc.isNull() || !doc.isObject()) return false;

        const QString mat = doc.object().value("asset_path").toString();
        if (mat.isEmpty()) {
            outError = "Remix returned no material asset_path for mesh prim.";
            return false;
        }
        outMaterialPrim = NormalizePathSlashes(mat);
        return true;
    }

    bool RemixConnector::GetMeshFilePathFromPrim(const QString& prim,
                                                 QString& outMeshPath,
                                                 QString& outContextAbs,
                                                 QString& outError) const {
        const QString encoded = UrlEncodeKeepSlashes(NormalizePathSlashes(prim));
        const QJsonDocument doc = RequestJson("GET", QString("/stagecraft/assets/%1/file-paths").arg(encoded), {}, nullptr, &outError);
        if (doc.isNull() || !doc.isObject()) return false;

        const QJsonObject obj = doc.object();
        QJsonValue v = obj.value("reference_paths");
        if (v.isUndefined()) v = obj.value("asset_paths");
        if (!v.isArray()) {
            outError = "Remix file-paths response missing reference_paths/asset_paths.";
            return false;
        }

        QString absContext;
        QString relMesh;

        auto consider = [&](const QString& s) {
            const QString p = NormalizePathSlashes(s);
            const QString lower = p.toLower();
            if (QDir::isAbsolutePath(p)) {
                absContext = p;
                return;
            }
            if (lower.endsWith(".usd") || lower.endsWith(".usda") || lower.endsWith(".usdc") ||
                lower.endsWith(".obj") || lower.endsWith(".fbx") || lower.endsWith(".gltf") || lower.endsWith(".glb")) {
                relMesh = p;
            }
        };

        const QJsonArray arr = v.toArray();
        for (const QJsonValue& entryVal : arr) {
            if (entryVal.isString()) {
                consider(entryVal.toString());
            } else if (entryVal.isArray()) {
                const QJsonArray entryArr = entryVal.toArray();
                if (entryArr.size() == 2 && entryArr.at(1).isArray()) {
                    const QJsonArray files = entryArr.at(1).toArray();
                    for (const QJsonValue& f : files)
                        if (f.isString()) consider(f.toString());
                } else {
                    for (const QJsonValue& f : entryArr)
                        if (f.isString()) consider(f.toString());
                }
            }
            if (!relMesh.isEmpty() && !absContext.isEmpty()) break;
        }

        if (relMesh.isEmpty()) {
            outError = "Could not determine mesh path from Remix file-paths response.";
            return false;
        }

        outMeshPath = relMesh;
        outContextAbs = absContext;
        return true;
    }

    QString RemixConnector::DeriveProjectNameFromRemixDir(const QString& remixDefaultDirAbs) const {
        if (remixDefaultDirAbs.trimmed().isEmpty()) return "UnknownProject";
        QString cursor = QDir::cleanPath(remixDefaultDirAbs);
        for (int i = 0; i < 6; ++i) {
            const QString base = QFileInfo(cursor).fileName();
            if (!base.isEmpty() && !kKnownTailNames.contains(base.toLower())) return base;
            const QString parent = QFileInfo(cursor).dir().absolutePath();
            if (parent == cursor) break;
            cursor = parent;
        }
        return "UnknownProject";
    }

    QString RemixConnector::GetPulledTexturesDir(const QString& remixDefaultDirAbs) const {
        const QString projectName = DeriveProjectNameFromRemixDir(remixDefaultDirAbs);
        const QString root = QDir::tempPath() + "/InstaMAT2Remix/Pulled Textures/" + projectName;
        QDir().mkpath(root);
        return QDir::cleanPath(root);
    }

    bool RemixConnector::GetSelectedRemixAssetDetails(RemixSelectionDetails& outDetails, QString& outError) const {
        outDetails = {};
        outError.clear();

        QMap<QString, QString> params;
        params.insert("selection", "true");
        params.insert("filter_session_assets", "false");
        params.insert("exists", "true");

        const QJsonDocument doc = RequestJson("GET", "/stagecraft/assets/", params, nullptr, &outError);
        if (doc.isNull() || !doc.isObject()) return false;

        const QJsonObject data = doc.object();
        QJsonValue pathsVal = data.value("prim_paths");
        if (pathsVal.isUndefined()) pathsVal = data.value("asset_paths");
        if (!pathsVal.isArray()) {
            outError = "Remix selection response missing prim_paths.";
            return false;
        }

        const QJsonArray pathsArr = pathsVal.toArray();
        if (pathsArr.isEmpty()) {
            outError = "No assets are currently selected in RTX Remix.";
            return false;
        }

        QString materialPrim;
        QString meshPrimInitial;

        for (const QJsonValue& v : pathsArr) {
            if (!v.isString()) continue;
            const QString p = NormalizePathSlashes(v.toString());
            const QString lower = p.toLower();

            if (p.endsWith("/Shader")) {
                materialPrim = QFileInfo(p).dir().path();
                continue;
            }

            const bool looksLikeMaterial = (lower.contains("/looks/") || lower.contains("/materials/") || lower.contains("/material/")) &&
                                           !lower.contains("/previewsurface");
            if (looksLikeMaterial && materialPrim.isEmpty()) {
                materialPrim = p;
                continue;
            }

            const bool meshLike = lower.contains("/instances/inst_") || lower.contains("/meshes/") || lower.contains("/mesh/") || lower.contains("/geom/");
            if (meshLike && meshPrimInitial.isEmpty()) {
                meshPrimInitial = p;
                continue;
            }
        }

        auto extractDefinitionPath = [](const QString& primPath) -> QString {
            const QString p = NormalizePathSlashes(primPath);
            QRegularExpression re1(R"(^(.*)/instances/inst_([A-Z0-9]{16}(?:_[0-9]+)?)(?:_[0-9]+)?(?:/.*)?$)");
            auto m1 = re1.match(p);
            if (m1.hasMatch()) return QString("%1/meshes/mesh_%2").arg(m1.captured(1)).arg(m1.captured(2));

            QRegularExpression re2(R"(^(.*(?:/meshes|/Mesh|/Geom)/mesh_[A-Z0-9]{16}(?:_[0-9]+)?)(?:/.*)?$)");
            auto m2 = re2.match(p);
            if (m2.hasMatch()) return m2.captured(1);

            return {};
        };

        if (!meshPrimInitial.isEmpty() && materialPrim.isEmpty()) {
            const QString defPath = extractDefinitionPath(meshPrimInitial);
            const QString lookup = defPath.isEmpty() ? meshPrimInitial : defPath;
            QString matErr;
            if (!GetMaterialFromMeshPrim(lookup, materialPrim, matErr)) {
                outError = "Mesh selected but failed to find bound material: " + matErr;
                return false;
            }
        }

        if (materialPrim.isEmpty()) {
            outError = "Could not identify a material prim from selection.";
            return false;
        }

        QString meshFile;
        QString contextAbs;

        QStringList primsToTry;
        if (!meshPrimInitial.isEmpty()) {
            primsToTry << meshPrimInitial;
            const QString defPath = extractDefinitionPath(meshPrimInitial);
            if (!defPath.isEmpty() && !primsToTry.contains(defPath)) primsToTry << defPath;
        }
        if (!primsToTry.contains(materialPrim)) primsToTry << materialPrim;

        QString lastErr;
        for (const QString& primToQuery : primsToTry) {
            QString err;
            QString m;
            QString ctx;
            if (GetMeshFilePathFromPrim(primToQuery, m, ctx, err)) {
                meshFile = m;
                contextAbs = ctx;
                break;
            }
            lastErr = err;
        }

        if (meshFile.isEmpty()) {
            outError = "Could not determine mesh file path. Last error: " + lastErr;
            return false;
        }

        outDetails.meshFilePath = meshFile.toStdString();
        outDetails.materialPrimPath = materialPrim.toStdString();
        outDetails.contextFilePath = contextAbs.toStdString();
        return true;
    }

    bool RemixConnector::RunTemplateGraphForMesh(const QString& meshPath, QString& outMessage, QString& outError) {
        outMessage.clear();
        outError.clear();

        QSettings settings("InstaMAT2Remix", "Config");
        const bool autoRun = settings.value(kKeyTemplateAutoRunOnPull, true).toBool();
        if (!autoRun) return false;

        const QString templatePath = QDir::cleanPath(settings.value(kKeyTemplateGraphPath, "").toString().trimmed());
        const QString inputName = settings.value(kKeyTemplateMeshInputName, "InputMeshPath").toString().trimmed();

        if (templatePath.isEmpty()) {
            outError = "Template graph path is not configured (TemplateGraphPath).";
            return false;
        }
        if (!QFileInfo::exists(templatePath)) {
            outError = QString("Template graph not found:\n%1").arg(templatePath);
            return false;
        }
        if (inputName.isEmpty()) {
            outError = "Template mesh input name is empty (TemplateMeshInputName).";
            return false;
        }

        InstaMAT::IGraphPackage* pkg = m_instaMAT.AllocPackageFromFile(templatePath.toStdString().c_str(), true);
        if (!pkg) {
            outError = QString("Failed to load template package:\n%1").arg(templatePath);
            return false;
        }

        struct PackageGuard {
            InstaMAT::IInstaMAT& api;
            InstaMAT::IGraphPackage* pkg;
            ~PackageGuard() { api.DeallocPackage(pkg); }
        } pkgGuard{m_instaMAT, pkg};

        InstaMAT::IGraphObject** objs = nullptr;
        if (!m_instaMAT.GetGraphObjectsInPackage(*pkg, &objs) || !objs) {
            outError = "Template package has no graph objects.";
            return false;
        }

        const InstaMAT::IGraph* chosenGraph = nullptr;
        for (int i = 0; objs[i] != nullptr; ++i) {
            if (const InstaMAT::IGraph* g = objs[i]->AsGraph()) {
                chosenGraph = g;
                break;
            }
        }

        if (!chosenGraph) {
            outError = "No graph found in template package.";
            return false;
        }

        InstaMAT::IElementExecution* exec = m_instaMAT.AllocElementExecution();
        if (!exec) {
            outError = "Failed to allocate element execution.";
            return false;
        }
        struct ExecGuard {
            InstaMAT::IInstaMAT& api;
            InstaMAT::IElementExecution* exec;
            ~ExecGuard() { api.DeallocElementExecution(exec); }
        } execGuard{m_instaMAT, exec};

        if (!exec->CreateInstance(*chosenGraph, InstaMAT::ElementExecutionFlags::None)) {
            outError = "Failed to create instance from template graph.";
            return false;
        }

        InstaMAT::IGraph* instance = exec->GetInstance();
        if (!instance) {
            outError = "Template graph instance is null after creation.";
            return false;
        }

        InstaMAT::IGraphVariable* meshVar = instance->GetParameterWithName(inputName.toStdString().c_str(),
                                                                           InstaMAT::IGraph::ParameterTypeInput);
        if (!meshVar) {
            outError = QString("Template graph is missing exposed input named '%1'.").arg(inputName);
            return false;
        }

        if (!exec->SetStringValueForInputParameter(*meshVar, meshPath.toStdString().c_str())) {
            outError = QString("Failed to set mesh path on template input '%1'.").arg(inputName);
            return false;
        }

        // Execute to realize the instance (graph can load the mesh internally).
        if (!exec->Execute(nullptr)) {
            outError = "Template graph execution failed.";
            return false;
        }

        outMessage = QString("Template graph executed with mesh:\n%1").arg(meshPath);
        return true;
    }

    void RemixConnector::PullFromRemix(bool autoUnwrap, PullMeshMode meshMode) {
        QString err;
        RemixSelectionDetails details;
        if (!GetSelectedRemixAssetDetails(details, err)) {
            QMessageBox::warning(nullptr, "InstaMAT2Remix", "Pull from Remix failed:\n\n" + err);
            return;
        }

        if (!QAccessible::isActive()) {
            m_logger.Warning("Qt accessibility is disabled. Auto-create requires QT_ACCESSIBILITY=1 before launching InstaMAT.");
        }

        const QString meshPathRaw = QString::fromStdString(details.meshFilePath);
        const QString ctxFile = QString::fromStdString(details.contextFilePath);
        const QString materialPrim = QString::fromStdString(details.materialPrimPath);

        QSettings settings("InstaMAT2Remix", "Config");
        bool useTilingMesh = settings.value("UseTilingMeshOnPull", false).toBool();
        QString tilingMeshPath = QDir::cleanPath(settings.value("TilingMeshPath", "").toString());
        const bool autoImportAfterPull = settings.value("AutoImportTexturesAfterPull", true).toBool();

        // Allow caller (UI) to override tiling mode without touching persistent settings.
        if (meshMode == PullMeshMode::SelectedMesh) useTilingMesh = false;
        else if (meshMode == PullMeshMode::TilingMesh) useTilingMesh = true;

        // If the tiling mesh path is relative, interpret it relative to this plugin dir (common when copied between machines).
        if (useTilingMesh && !tilingMeshPath.isEmpty() && !QDir::isAbsolutePath(tilingMeshPath)) {
            const QString abs = QDir(GetPluginDirPath()).filePath(tilingMeshPath);
            if (QFileInfo::exists(abs)) tilingMeshPath = QDir::cleanPath(abs);
        }

        // If tiling mesh is requested but not configured, fall back to the built-in tiling plane (if present).
        if (useTilingMesh && (tilingMeshPath.isEmpty() || !QFileInfo::exists(tilingMeshPath))) {
            const QString fallback = DetectDefaultTilingMeshPath();
            if (!fallback.isEmpty()) tilingMeshPath = fallback;
        }

        QString meshAbs;
        if (QDir::isAbsolutePath(meshPathRaw)) {
            meshAbs = QDir::cleanPath(meshPathRaw);
        } else if (!ctxFile.isEmpty() && QDir::isAbsolutePath(ctxFile)) {
            const QString ctxDir = QFileInfo(ctxFile).dir().absolutePath();
            meshAbs = QDir::cleanPath(QDir(ctxDir).filePath(meshPathRaw));
        } else {
            QMessageBox::warning(nullptr, "InstaMAT2Remix", "Mesh path is relative, but no absolute context file was provided by Remix.");
            return;
        }

        // Prefer naming the project after the *selected* Remix mesh, even if we later substitute a tiling plane
        // or generate an auto-unwrapped mesh file.
        QString pulledMeshBaseName = QFileInfo(meshAbs).completeBaseName().trimmed();

        if (useTilingMesh) {
            if (!tilingMeshPath.isEmpty() && QFileInfo::exists(tilingMeshPath)) {
                meshAbs = QDir::cleanPath(tilingMeshPath);
            } else {
                QMessageBox::warning(nullptr,
                                     "InstaMAT2Remix",
                                     "Tiling mesh is enabled, but no valid tiling mesh path is configured.\n\n"
                                     "Set 'Tiling Mesh Path' in Settings, or ensure the plugin ships its default tiling mesh.");
                return;
            }
        }

        if (!QFileInfo::exists(meshAbs)) {
            QMessageBox::warning(nullptr, "InstaMAT2Remix", "Mesh file does not exist locally:\n\n" + meshAbs);
            return;
        }

        QString finalMesh = meshAbs;
        if (autoUnwrap && !useTilingMesh && !m_tools.GetBlenderExecutable().empty()) {
            ExternalTools::UnwrapParams params;
            {
                params.angleLimit = settings.value("BlenderSmartUVAngleLimit", 66.0).toDouble();
                params.islandMargin = settings.value("BlenderSmartUVIslandMargin", 0.003).toDouble();
                params.areaWeight = settings.value("BlenderSmartUVAreaWeight", 0.0).toDouble();
                params.stretchToBounds = settings.value("BlenderSmartUVStretchToBounds", false).toBool();
            }

            std::string unwrapped;
            if (m_tools.RunAutoUnwrap(meshAbs.toStdString(), unwrapped, params)) {
                finalMesh = QString::fromStdString(unwrapped);
            } else {
                qWarning() << "[InstaMAT2Remix] Auto-unwrap failed, using original mesh.";
            }
        }

        // Cache link state
        m_linkedMaterialPrim = materialPrim.toStdString();
        m_linkedMeshPath = finalMesh.toStdString();
        settings.setValue("LinkedMaterialPrim", materialPrim);
        settings.setValue("LinkedMeshPath", finalMesh);

        // Register external folder so the mesh is easy to find in InstaMAT's library picker.
        m_instaMAT.RegisterExternalAssetFolder(QFileInfo(finalMesh).dir().absolutePath().toStdString().c_str());

        if (QGuiApplication::clipboard()) QGuiApplication::clipboard()->setText(finalMesh);

        // Template Method: Copy template project and open it.
        const QString templatePath = settings.value(kKeyTemplateGraphPath, "").toString();
        QString projectsDir = settings.value(kKeyProjectsFolder, "").toString();
        if (projectsDir.isEmpty()) {
             const QString docs = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
             projectsDir = QDir(docs).filePath("InstaMAT/Projects");
        }

        
        bool templateFound = !templatePath.isEmpty() && QFileInfo::exists(templatePath);
        
        if (!templateFound) {
            // No template configured. Offer to create a new project and guide the user.
            QMessageBox::StandardButton reply = QMessageBox::question(
                nullptr, 
                "InstaMAT2Remix", 
                "No template project is configured.\n\n"
                "Do you want to create a new blank project now?\n"
                "(You will need to add the RTX Remix Import/Export nodes manually)",
                QMessageBox::Yes | QMessageBox::No);
                
            if (reply == QMessageBox::No) return;
        }

        const QString projectName = "Remix_" + pulledMeshBaseName;
        const QString projectDir = QDir(projectsDir).filePath(projectName);
        const QString projectFile = QDir(projectDir).filePath(projectName + ".imp");
        
        if (!QDir().mkpath(projectDir)) {
             QMessageBox::warning(nullptr, "InstaMAT2Remix", "Failed to create project directory:\n" + projectDir);
             return;
        }

        // 1. Copy Template (only if new)
        bool isNewProject = false;
        if (!QFileInfo::exists(projectFile)) {
            if (templateFound) {
                if (!QFile::copy(templatePath, projectFile)) {
                    QMessageBox::warning(nullptr, "InstaMAT2Remix", "Failed to copy template project to:\n" + projectFile);
                    return;
                }
                // Make file writable if template was read-only
                QFile::setPermissions(projectFile, QFile::ReadOwner | QFile::WriteOwner | QFile::ReadUser | QFile::WriteUser);
            } else {
                // Trigger auto-creation of NEW project via automation (handled by Python script usually, but here we just prepare the folder)
                // Actually, if we don't have a template file to copy, we rely on the automation to click "New Project" in the UI.
                // But we are in C++. We can't easily click UI.
                // The Python script 'rtx_remix_connector.py' handles the "New Project" click if it detects we are in a "AutoCreate" state.
                m_isAutoCreatingProject = true;
            }
            isNewProject = true;
        }


        // 2. Copy Mesh
        const QString meshExt = QFileInfo(finalMesh).suffix();
        const QString destMeshName = "input_mesh." + meshExt;
        const QString destMeshPath = QDir(projectDir).filePath(destMeshName);
        
        QFile::remove(destMeshPath); // Overwrite
        if (!QFile::copy(finalMesh, destMeshPath)) {
             QMessageBox::warning(nullptr, "InstaMAT2Remix", "Failed to copy mesh to project folder:\n" + destMeshPath);
             return;
        }
        
        // 3. Open Project
        if (templateFound || !isNewProject) {
            const QUrl url = QUrl::fromLocalFile(projectFile);
            if (!QDesktopServices::openUrl(url)) {
                 QMessageBox::warning(nullptr, "InstaMAT2Remix", "Failed to open project:\n" + projectFile);
            }
        } else {
            // No template, and it is a new project.
            // We need to trigger the "New Project" action in the host.
            // We can try to invoke the Python script logic or just tell the user.
            // Since we are in C++, we can't easily invoke the Python "auto create" logic unless we signal it.
            // However, the Python script watches for "Pull" actions? No, it's menu driven.
            
            // Let's just show a message telling them what to do.
            QMessageBox::information(nullptr, "InstaMAT2Remix", 
                "Ready to create project.\n\n"
                "1. Go to File > New Project\n"
                "2. Choose 'Element Graph'\n"
                "3. Save it as '" + projectName + "' in:\n" + projectsDir + "\n\n"
                "Then add the RTX Remix nodes to start working.");
                
            // Open the folder so they can save it there easily
            QDesktopServices::openUrl(QUrl::fromLocalFile(projectsDir));
            return; 
        }

        QString info = isNewProject ? "✓ New project created!\n\n" : "✓ Project updated!\n\n";
        info += "Project: " + projectName + "\n";
        info += "Location: " + projectDir + "\n";
        info += "Mesh: " + destMeshName + "\n\n";
        
        if (isNewProject) {
            info += "💡 The mesh has been copied to the project folder.\n";
            info += "If your template references './" + destMeshName + "', it will load automatically.\n";
            info += "Otherwise, please link the mesh manually in the Graph Object Editor.";
        } else {
            info += "💡 The mesh file in the project folder has been updated.\n";
            info += "InstaMAT should reload it automatically.";
        }

        QMessageBox::information(nullptr, "InstaMAT2Remix", info);

        if (autoImportAfterPull) {
             ImportTexturesFromRemix();
        }
    }

    void RemixConnector::ImportTexturesFromRemix() {
        QSettings settings("InstaMAT2Remix", "Config");

        QString materialPrim = settings.value("LinkedMaterialPrim", "").toString();
        if (materialPrim.isEmpty()) {
            QString selErr;
            if (!GetSelectedMaterialPrim(materialPrim, selErr)) {
                QMessageBox::warning(nullptr, "InstaMAT2Remix", "Import Textures failed:\n\nNo linked material and selection query failed:\n" + selErr);
                return;
            }
            settings.setValue("LinkedMaterialPrim", materialPrim);
            m_linkedMaterialPrim = materialPrim.toStdString();
        }

        QString remixDirAbs;
        QString dirErr;
        if (!GetRemixDefaultDirectory(remixDirAbs, dirErr)) {
            QMessageBox::warning(nullptr, "InstaMAT2Remix", "Import Textures failed:\n\nCould not determine Remix project directory:\n" + dirErr);
            return;
        }

        const QString destDir = GetPulledTexturesDir(remixDirAbs);

        const QString encodedMat = UrlEncodeKeepSlashes(NormalizePathSlashes(materialPrim));
        QString apiErr;
        const QJsonDocument doc = RequestJson("GET", QString("/stagecraft/assets/%1/textures").arg(encodedMat), {}, nullptr, &apiErr);
        if (doc.isNull() || !doc.isObject()) {
            QMessageBox::warning(nullptr, "InstaMAT2Remix", "Import Textures failed:\n\n" + apiErr);
            return;
        }

        const QJsonArray textures = doc.object().value("textures").toArray();
        if (textures.isEmpty()) {
            QMessageBox::information(nullptr, "InstaMAT2Remix", "No textures were returned for the material:\n\n" + materialPrim);
            return;
        }

        QProgressDialog progress("Importing textures from RTX Remix...", "Cancel", 0, textures.size(), nullptr);
        progress.setWindowTitle(kPluginName);
        progress.setWindowModality(Qt::WindowModal);
        progress.setMinimumDuration(0);
        progress.setValue(0);
        progress.show();
        QCoreApplication::processEvents();

        int pulledCount = 0;
        int convertedCount = 0;

        int idx = 0;
        for (const QJsonValue& entry : textures) {
            if (progress.wasCanceled()) break;
            if (!entry.isArray()) continue;
            const QJsonArray pair = entry.toArray();
            if (pair.size() < 2) continue;
            if (!pair.at(0).isString() || !pair.at(1).isString()) continue;

            const QString usdAttr = pair.at(0).toString();
            const QString texPathRaw = NormalizePathSlashes(pair.at(1).toString());

            progress.setLabelText(QString("Importing %1\n%2").arg(QFileInfo(texPathRaw).fileName(), usdAttr));
            progress.setValue(idx++);
            QCoreApplication::processEvents();

            QString absTex = texPathRaw;
            if (!QDir::isAbsolutePath(absTex)) absTex = QDir(remixDirAbs).filePath(absTex);
            absTex = QDir::cleanPath(absTex);

            if (!QFileInfo::exists(absTex)) {
                qWarning() << "[InstaMAT2Remix] Missing texture file:" << absTex << "for" << usdAttr;
                continue;
            }

            const QString lower = absTex.toLower();
            if (lower.endsWith(".dds") || lower.endsWith(".rtex.dds")) {
                std::string pngOut;
                if (m_tools.ConvertDdsToPng(absTex.toStdString(), destDir.toStdString(), pngOut)) {
                    convertedCount++;
                    pulledCount++;
                } else {
                    qWarning() << "[InstaMAT2Remix] texconv failed converting:" << absTex;
                }
                continue;
            }

            const QString destPath = QDir(destDir).filePath(QFileInfo(absTex).fileName());
            QFile::remove(destPath);
            if (QFile::copy(absTex, destPath)) pulledCount++;
        }

        // Make folder available as external asset folder for easy picking.
        if (pulledCount > 0) m_instaMAT.RegisterExternalAssetFolder(destDir.toStdString().c_str());

        if (progress.wasCanceled()) {
            QMessageBox::information(nullptr, "InstaMAT2Remix", "Import cancelled.");
            return;
        }

        QMessageBox::information(nullptr,
                                 "InstaMAT2Remix",
                                 QString("Imported %1 texture(s) from RTX Remix.\nConverted DDS->PNG: %2\n\nSaved to:\n%3")
                                     .arg(pulledCount)
                                     .arg(convertedCount)
                                     .arg(destDir));
    }

    void RemixConnector::PushToRemix(bool forceDialog) {
        QSettings settings("InstaMAT2Remix", "Config");

        QProgressDialog progress("Pushing textures to RTX Remix...", "Cancel", 0, 0, nullptr);
        progress.setWindowTitle(kPluginName);
        progress.setWindowModality(Qt::WindowModal);
        progress.setMinimumDuration(0);
        progress.show();
        QCoreApplication::processEvents();

        // Determine push target material prim
        QString materialPrim = settings.value("LinkedMaterialPrim", "").toString();
        if (forceDialog || materialPrim.isEmpty()) {
            QString selErr;
            if (!GetSelectedMaterialPrim(materialPrim, selErr)) {
                QMessageBox::warning(nullptr, "InstaMAT2Remix", "Push failed:\n\nCould not determine target material prim:\n" + selErr);
                return;
            }
            settings.setValue("LinkedMaterialPrim", materialPrim);
            m_linkedMaterialPrim = materialPrim.toStdString();
        }

        if (progress.wasCanceled()) return;
        progress.setLabelText("Resolving RTX Remix project...");
        QCoreApplication::processEvents();

        QString remixDirAbs;
        QString dirErr;
        if (!GetRemixDefaultDirectory(remixDirAbs, dirErr)) {
            QMessageBox::warning(nullptr, "InstaMAT2Remix", "Push failed:\n\nCould not determine Remix project directory:\n" + dirErr);
            return;
        }

        // Choose export graph (persisted)
        QString graphId = settings.value("ExportGraphId", "").toString();
        const InstaMAT::IGraphObject* graphObj = nullptr;
        if (!graphId.isEmpty()) graphObj = m_instaMAT.GetGraphObjectByID(graphId.toStdString().c_str());

        auto chooseGraph = [&]() -> QString {
            struct Candidate {
                QString id;
                QString label;
            };
            QList<Candidate> candidates;

            const char** categories = nullptr;
            if (!m_instaMAT.GetCategories(&categories) || !categories) return {};

            for (int ci = 0; categories[ci] != nullptr; ++ci) {
                const char* cat = categories[ci];
                InstaMAT::IGraphObject** objs = nullptr;
                if (!m_instaMAT.GetGraphObjectsInCategory(cat, &objs) || !objs) continue;

                for (int oi = 0; objs[oi] != nullptr; ++oi) {
                    const InstaMAT::IGraphObject* o = objs[oi];
                    const InstaMAT::IGraph* g = o->AsGraph();
                    if (!g) continue;

                    const InstaMAT::uint32 type = g->GetGraphTypeUI32();
                    if (type == InstaMAT::IGraph::GraphTypeFunction || type == InstaMAT::IGraph::GraphTypeAtom) continue;

                    const InstaMAT::IGraphPackage* pkg = o->GetParentPackage();
                    const QString originType = pkg ? QString::fromUtf8(pkg->GetOriginType()) : QString();
                    if (originType == "SystemLibrary") continue;

                    const InstaMAT::uint64 idSize = o->GetID(nullptr, 0);
                    if (idSize == 0) continue;
                    QByteArray idBuf(int(idSize), '\0');
                    o->GetID(idBuf.data(), idBuf.size());
                    const QString id = QString::fromUtf8(idBuf.constData()).trimmed();
                    if (id.isEmpty()) continue;

                    const QString name = QString::fromUtf8(o->GetName(true));
                    const QString pkgName = pkg ? QString::fromUtf8(pkg->GetName()) : QString("<no package>");
                    const QString label = QString("%1  [%2 | %3]").arg(name, pkgName, originType);

                    candidates.push_back({id, label});
                }
            }

            if (candidates.isEmpty()) return {};

            QStringList items;
            items.reserve(candidates.size());
            for (const auto& c : candidates) items << c.label;

            bool ok = false;
            const QString chosen = QInputDialog::getItem(nullptr, "InstaMAT2Remix", "Select graph to export:", items, 0, false, &ok);
            if (!ok || chosen.isEmpty()) return {};

            const int idx = items.indexOf(chosen);
            if (idx < 0 || idx >= candidates.size()) return {};
            return candidates[idx].id;
        };

        if (!graphObj) {
            const QString chosenId = chooseGraph();
            if (chosenId.isEmpty()) return;

            settings.setValue("ExportGraphId", chosenId);
            graphId = chosenId;
            graphObj = m_instaMAT.GetGraphObjectByID(graphId.toStdString().c_str());
            if (!graphObj) {
                QMessageBox::warning(nullptr, "InstaMAT2Remix", "Push failed:\n\nSelected graph could not be resolved. Please retry.");
                return;
            }
        }

        const InstaMAT::IGraph* graph = graphObj->AsGraph();
        if (!graph) {
            QMessageBox::warning(nullptr, "InstaMAT2Remix", "Push failed:\n\nSelected object is not a graph.");
            return;
        }

        if (progress.wasCanceled()) return;
        progress.setLabelText("Executing graph...");
        QCoreApplication::processEvents();

        // Export directory
        QString materialHash = "Material";
        {
            QRegularExpression re(R"(([A-Z0-9]{16})$)");
            const auto m = re.match(materialPrim);
            if (m.hasMatch()) materialHash = m.captured(1);
        }
        const QString stamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
        QString exportRoot = settings.value("ExportBaseFolder", QDir::tempPath() + "/InstaMAT2Remix/Export").toString().trimmed();
        if (exportRoot.isEmpty()) exportRoot = QDir::tempPath() + "/InstaMAT2Remix/Export";
        const QString exportDir = QDir::cleanPath(QDir(exportRoot).filePath(materialHash + "_" + stamp));
        QDir().mkpath(exportDir);

        // Execute graph
        InstaMAT::IElementExecution* execRaw = m_instaMAT.AllocElementExecution();
        if (!execRaw) {
            QMessageBox::warning(nullptr, "InstaMAT2Remix", "Push failed:\n\nFailed to allocate InstaMAT element execution.");
            return;
        }

        struct ExecGuard {
            InstaMAT::IInstaMAT& api;
            InstaMAT::IElementExecution* exec;
            ~ExecGuard() { api.DeallocElementExecution(exec); }
        } execGuard{m_instaMAT, execRaw};

        const int width = settings.value("ExportWidth", 2048).toInt();
        const int height = settings.value("ExportHeight", 2048).toInt();
        execRaw->SetFormat(InstaMAT::uint32(width), InstaMAT::uint32(height), InstaMAT::ElementExecutionFormat::Normalized16);
        if (!execRaw->CreateInstance(*graph, InstaMAT::ElementExecutionFlags::None)) {
            QMessageBox::warning(nullptr, "InstaMAT2Remix", "Push failed:\n\nFailed to create execution instance for selected graph.");
            return;
        }
        if (!execRaw->Execute(nullptr)) {
            QMessageBox::warning(nullptr, "InstaMAT2Remix", "Push failed:\n\nGraph execution failed.");
            return;
        }

        InstaMAT::IGraph* instance = execRaw->GetInstance();
        if (!instance) {
            QMessageBox::warning(nullptr, "InstaMAT2Remix", "Push failed:\n\nGraph execution produced no instance.");
            return;
        }

        // Match outputs by name
        struct OutputMatch {
            QString pbrType;
            QStringList names;
        };
        const QList<OutputMatch> outputMatches = {
            {"albedo", {"Albedo", "BaseColor", "Base Color", "Diffuse"}},
            {"normal", {"Normal", "NormalDX", "Normal DX", "NormalMap"}},
            {"roughness", {"Roughness"}},
            {"metallic", {"Metallic", "Metalness"}},
            {"emissive", {"Emissive", "Emission"}},
            {"height", {"Height", "Displacement"}},
            {"opacity", {"Opacity", "Alpha"}},
            {"ao", {"Ambient Occlusion", "AO", "Occlusion"}},
            {"transmittance", {"Transmittance", "Transmission"}},
            {"ior", {"IOR", "Refraction", "Index of Refraction"}},
            {"subsurface", {"Subsurface", "SSS", "Subsurface Scattering"}},
        };

        QHash<QString, const InstaMAT::IGraphVariable*> outputs;
        const InstaMAT::uint32 outCount = instance->GetParameterCount(InstaMAT::IGraph::ParameterTypeOutput);
        for (InstaMAT::uint32 i = 0; i < outCount; ++i) {
            const InstaMAT::IGraphVariable* v = instance->GetParameterAtIndex(i, InstaMAT::IGraph::ParameterTypeOutput);
            if (!v) continue;
            const auto vt = v->GetVariableTypeValue();
            if (vt != InstaMAT::IGraphVariable::TypeElementImage && vt != InstaMAT::IGraphVariable::TypeElementImageGray &&
                vt != InstaMAT::IGraphVariable::TypeAtomOutputImage && vt != InstaMAT::IGraphVariable::TypeAtomOutputImageGray) {
                continue;
            }

            const InstaMAT::IGraphObject* vo = v->AsObject();
            const QString rawName = vo ? QString::fromUtf8(vo->GetName(true)) : QString();
            const QString normName = NormalizeSpacesUnderscoresLower(rawName);

            for (const auto& m : outputMatches) {
                if (outputs.contains(m.pbrType)) continue;
                for (const QString& candidate : m.names) {
                    if (normName == NormalizeSpacesUnderscoresLower(candidate)) {
                        outputs.insert(m.pbrType, v);
                        break;
                    }
                }
            }
        }

        // Export outputs to PNG
        const bool includeOpacity = settings.value("IncludeOpacityMap", false).toBool();
        QString exportFormat = settings.value("ExportFormat", "png").toString().trimmed().toLower();
        if (exportFormat != "png" && exportFormat != "tga" && exportFormat != "jpg") exportFormat = "png";

        progress.setLabelText("Exporting textures...");
        QCoreApplication::processEvents();
        if (progress.wasCanceled()) return;

        QHash<QString, QString> exportedFiles;
        for (const auto& spec : kDefaultPbrSpecs) {
            if (!includeOpacity && spec.pbrType == "opacity") continue;
            const InstaMAT::IGraphVariable* v = outputs.value(spec.pbrType, nullptr);
            if (!v) continue;

            InstaMAT::IImageSampler* sampler = execRaw->AllocImageSamplerForOutputParameter(*v, spec.sRGB);
            if (!sampler) continue;

            QString outExt = exportFormat;
            // Keep high bit-depth outputs as PNG to preserve precision.
            if (spec.bits > 8 && outExt != "png") outExt = "png";

            const QString outPath = QDir(exportDir).filePath(materialHash + "_" + spec.pbrType + "." + outExt);
            bool wrote = false;
            if (outExt == "png") {
                wrote = sampler->WritePNG(outPath.toStdString().c_str(), InstaMAT::uint32(spec.bits), true);
            } else if (outExt == "tga") {
                wrote = sampler->WriteTGA(outPath.toStdString().c_str(), true);
            } else if (outExt == "jpg") {
                wrote = sampler->WriteJPEG(outPath.toStdString().c_str(), 95, true);
            }
            m_instaMAT.DeallocImageSampler(sampler);

            if (wrote && QFileInfo::exists(outPath)) exportedFiles.insert(spec.pbrType, outPath);
        }

        if (exportedFiles.isEmpty()) {
            QMessageBox::warning(nullptr,
                                 "InstaMAT2Remix",
                                 "Push failed:\n\nNo matching output textures were exported.\n\n"
                                 "Tip: name your graph outputs: Albedo, Normal, Roughness, Metallic, Emissive, Height (optional). ");
            return;
        }

        // Ingest textures into Remix
        const QString outputSubfolder = settings.value("RemixOutputSubfolder", "Textures/InstaMAT2Remix_Ingested").toString();
        const QString targetIngestDirAbs = QDir(remixDirAbs).filePath(outputSubfolder);
        QDir().mkpath(targetIngestDirAbs);

        auto ingestTexture = [&](const QString& pbrType, const QString& texturePath, QString& outIngested, QString& outIngestErr) -> bool {
            outIngested.clear();
            outIngestErr.clear();

            const QString validationType = kPbrToIngestValidation.value(pbrType.toLower(), "DIFFUSE");
            const QString absTexture = NormalizePathSlashes(QFileInfo(texturePath).absoluteFilePath());
            const QString outDirApi = NormalizePathSlashes(QFileInfo(targetIngestDirAbs).absoluteFilePath());

            QJsonObject payload;
            payload.insert("executor", 1);
            payload.insert("name", QString("Ingest_%1_%2").arg(pbrType, QFileInfo(absTexture).fileName()));

            QJsonObject contextData;
            contextData.insert("context_name", "ingestcraft_browser");
            QJsonArray inputFiles;
            QJsonArray inPair;
            inPair.append(absTexture);
            inPair.append(validationType);
            inputFiles.append(inPair);
            contextData.insert("input_files", inputFiles);
            contextData.insert("output_directory", outDirApi);
            contextData.insert("allow_empty_input_files_list", true);

            QJsonArray dataFlows;
            auto addFlow = [&](const QString& channel) {
                QJsonObject f;
                f.insert("name", "InOutData");
                f.insert("push_output_data", true);
                f.insert("channel", channel);
                dataFlows.append(f);
            };
            addFlow("ingestion_output");
            addFlow("cleanup_files");
            addFlow("write_metadata");

            contextData.insert("data_flows", dataFlows);
            contextData.insert("hide_context_ui", true);
            contextData.insert("create_context_if_not_exist", true);
            contextData.insert("expose_mass_ui", false);
            contextData.insert("cook_mass_template", true);

            QJsonObject contextPlugin;
            contextPlugin.insert("name", "TextureImporter");
            contextPlugin.insert("data", contextData);
            payload.insert("context_plugin", contextPlugin);

            QJsonArray checkPlugins;
            {
                QJsonObject check;
                check.insert("name", "ConvertToDDS");

                QJsonArray selectors;
                QJsonObject sel;
                sel.insert("name", "AllShaders");
                sel.insert("data", QJsonObject{});
                selectors.append(sel);
                check.insert("selector_plugins", selectors);

                QJsonObject checkData;
                QJsonArray checkFlows;
                auto addCheckFlow = [&](bool pushInput, bool pushOutput, const QString& channel) {
                    QJsonObject f;
                    f.insert("name", "InOutData");
                    if (pushInput) f.insert("push_input_data", true);
                    if (pushOutput) f.insert("push_output_data", true);
                    f.insert("channel", channel);
                    checkFlows.append(f);
                };
                addCheckFlow(true, true, "ingestion_output");
                addCheckFlow(true, true, "cleanup_files");
                addCheckFlow(false, true, "write_metadata");
                checkData.insert("data_flows", checkFlows);
                check.insert("data", checkData);

                check.insert("stop_if_fix_failed", true);
                check.insert("context_plugin", QJsonObject{{"name", "CurrentStage"}, {"data", QJsonObject{}}});
                checkPlugins.append(check);
            }
            payload.insert("check_plugins", checkPlugins);

            QJsonArray resultors;
            resultors.append(QJsonObject{{"name", "FileCleanup"}, {"data", QJsonObject{{"channel", "cleanup_files"}, {"cleanup_output", false}}}});
            resultors.append(QJsonObject{{"name", "FileMetadataWritter"}, {"data", QJsonObject{{"channel", "write_metadata"}}}});
            payload.insert("resultor_plugins", resultors);

            QJsonDocument body(payload);
            QString apiErr;
            const QJsonDocument resp = RequestJson("POST", "/ingestcraft/mass-validator/queue/material", {}, &body, &apiErr);
            if (resp.isNull() || !resp.isObject()) {
                outIngestErr = apiErr;
                return false;
            }

            QStringList outputPaths;
            auto harvest = [&](const QJsonObject& pluginRes) {
                const QJsonObject d = pluginRes.value("data").toObject();
                const QJsonArray flows = d.value("data_flows").toArray();
                for (const QJsonValue& fv : flows) {
                    const QJsonObject fo = fv.toObject();
                    if (fo.value("channel").toString() != "ingestion_output") continue;
                    const QJsonArray outs = fo.value("output_data").toArray();
                    for (const QJsonValue& ov : outs)
                        if (ov.isString()) outputPaths << ov.toString();
                }
            };

            const QJsonObject ro = resp.object();
            const QJsonArray schemas = ro.value("completed_schemas").toArray();
            for (const QJsonValue& sv : schemas) {
                const QJsonObject schema = sv.toObject();
                harvest(schema.value("context_plugin").toObject());
                const QJsonArray checks = schema.value("check_plugins").toArray();
                for (const QJsonValue& cv : checks) harvest(cv.toObject());
            }
            const QJsonArray content = ro.value("content").toArray();
            for (const QJsonValue& cv : content)
                if (cv.isString()) outputPaths << cv.toString();

            outputPaths.removeDuplicates();
            if (outputPaths.isEmpty()) {
                outIngestErr = "Ingest succeeded but returned no output paths.";
                return false;
            }

            const QString wantBase = QFileInfo(absTexture).completeBaseName().toLower();
            QString best;
            for (const QString& p : outputPaths) {
                const QString pl = p.toLower();
                if (!pl.endsWith(".dds") && !pl.endsWith(".rtex.dds")) continue;
                const QString base = QFileInfo(pl).completeBaseName();
                if (base.contains(wantBase)) {
                    best = p;
                    if (pl.endsWith(".rtex.dds")) break;
                }
            }
            if (best.isEmpty()) best = outputPaths.first();

            QString absOut = best;
            if (!QDir::isAbsolutePath(absOut)) absOut = QDir(targetIngestDirAbs).filePath(best);
            absOut = QDir::cleanPath(absOut);

            if (!QFileInfo::exists(absOut)) {
                outIngestErr = "Ingest output path not found on disk: " + absOut;
                return false;
            }

            outIngested = absOut;
            return true;
        };

        QHash<QString, QString> ingestedPaths;
        QStringList ingestErrors;
        progress.setRange(0, exportedFiles.size());
        progress.setValue(0);
        int ingestIdx = 0;
        for (auto it = exportedFiles.begin(); it != exportedFiles.end(); ++it) {
            if (progress.wasCanceled()) break;
            progress.setLabelText(QString("Ingesting %1...").arg(it.key()));
            progress.setValue(ingestIdx++);
            QCoreApplication::processEvents();

            QString ingested;
            QString ingestErr;
            if (ingestTexture(it.key(), it.value(), ingested, ingestErr)) {
                ingestedPaths.insert(it.key(), ingested);
            } else {
                ingestErrors << QString("%1: %2").arg(it.key(), ingestErr);
            }
        }

        if (progress.wasCanceled()) {
            QMessageBox::information(nullptr, "InstaMAT2Remix", "Push cancelled.");
            return;
        }

        if (ingestedPaths.isEmpty()) {
            QMessageBox::warning(nullptr, "InstaMAT2Remix", "Push failed:\n\nIngestion failed for all textures:\n" + ingestErrors.join("\n"));
            return;
        }

        // Batch update
        QJsonArray texturePairs;
        for (const auto& spec : kDefaultPbrSpecs) {
            if (!includeOpacity && spec.pbrType == "opacity") continue;
            if (!ingestedPaths.contains(spec.pbrType)) continue;
            const QString usdAttr = NormalizePathSlashes(materialPrim) + "/Shader.inputs:" + spec.mdlInput;
            QJsonArray pair;
            pair.append(usdAttr);
            pair.append(NormalizePathSlashes(ingestedPaths.value(spec.pbrType)));
            texturePairs.append(pair);
        }

        QJsonObject updatePayload;
        updatePayload.insert("force", true);
        updatePayload.insert("textures", texturePairs);

        QJsonDocument updateBody(updatePayload);
        QString updateErr;
        progress.setLabelText("Updating RTX Remix material...");
        QCoreApplication::processEvents();
        const QJsonDocument updateResp = RequestJson("PUT", "/stagecraft/textures/", {}, &updateBody, &updateErr);
        if (updateResp.isNull()) {
            QMessageBox::warning(nullptr, "InstaMAT2Remix", "Push failed:\n\nTexture update failed:\n" + updateErr);
            return;
        }

        // Save layer
        auto getCurrentLayer = [&](QString& outLayer, QString& outLayerErr) -> bool {
            outLayer.clear();
            outLayerErr.clear();

            QString apiErr1;
            const QJsonDocument d1 = RequestJson("GET", "/stagecraft/layers/target", {}, nullptr, &apiErr1);
            if (d1.isObject()) outLayer = d1.object().value("layer_id").toString();
            if (!outLayer.isEmpty()) return true;

            QString apiErr2;
            const QJsonDocument d2 = RequestJson("GET", "/stagecraft/project/", {}, nullptr, &apiErr2);
            if (d2.isObject()) outLayer = d2.object().value("layer_id").toString();
            if (!outLayer.isEmpty()) return true;

            outLayerErr = apiErr1 + "\n" + apiErr2;
            return false;
        };

        QString layerId;
        QString layerErr;
        if (getCurrentLayer(layerId, layerErr)) {
            const QString encLayer = UrlEncodeKeepColonAndSlashes(NormalizePathSlashes(layerId));
            QString saveErr;
            const QJsonDocument saveResp = RequestJson("POST", QString("/stagecraft/layers/%1/save").arg(encLayer), {}, nullptr, &saveErr);
            if (saveResp.isNull()) {
                qWarning() << "[InstaMAT2Remix] Layer save failed:" << saveErr;
            }
        } else {
            qWarning() << "[InstaMAT2Remix] Could not determine layer to save:" << layerErr;
        }

        QString summary = QString("Push complete.\nUpdated %1 texture(s) on:\n%2").arg(ingestedPaths.size()).arg(materialPrim);
        if (!ingestErrors.isEmpty()) summary += "\n\nWarnings:\n" + ingestErrors.join("\n");
        QMessageBox::information(nullptr, "InstaMAT2Remix", summary);
    }
}

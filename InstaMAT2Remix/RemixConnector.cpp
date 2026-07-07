#include "RemixConnector.h"
#include "MeshData.h"
#include "PluginInfo.h"
#include "PluginPaths.h"

#include <QApplication>
#include <QAbstractButton>
#include <QAbstractItemView>
#include <QClipboard>
#include <QComboBox>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QGuiApplication>
#include <QHash>
#include <QImageReader>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMainWindow>
#include <QMap>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMetaProperty>
#include <QPointer>
#include <QProcess>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QStringList>
#include <QThread>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QUrl>
#include <QUrlQuery>
#include <QProgressDialog>
#include <QSysInfo>
#include <QKeyEvent>
#include <QWindow>
#include <functional>
// NOTE: We intentionally avoid linking against QtQuick/QML headers here.
// Including them would add Qt6Quick.dll / Qt6Qml.dll as hard load-time dependencies,
// breaking plugin loading when InstaMAT extracts the DLL to a temp directory.
// The New Project recipe drives QtWidgets / QObject directly, identifying widgets
// by metaObject()->className() against the InstaMAT::UI::* classes the InstaMAT
// engineering team confirmed are stable.

namespace InstaMAT2Remix {
    namespace {
        static RemixConnector* g_instance = nullptr;

        constexpr const char* kRemixAccept = "application/lightspeed.remix.service+json; version=1.0";

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

        // NOTE: the Studio-output-name → canonical-PBR-channel mapping
        // ("Base Color" → albedo, …) lives in ExportWorker.cpp
        // (MapStudioOutputToCanonicalPbr) — the out-of-process exporter is the
        // only place layer-graph outputs are walked now.

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

        // Mirror of WBC remix_api.py:21-29 REMIX_ATTR_SUFFIX_TO_PBR_MAP.
        // Keep this in lock-step with WBC if the table there changes.
        static const QHash<QString, QString> kRemixAttrSuffixToPbr = {
            {"diffuse_texture",             "albedo"},
            {"albedo_texture",              "albedo"},
            {"basecolor_texture",           "albedo"},
            {"base_color_texture",          "albedo"},
            {"normalmap_texture",           "normal"},
            {"normal_texture",              "normal"},
            {"worldspacenormal_texture",    "normal"},
            {"heightmap_texture",           "height"},
            {"height_texture",              "height"},
            {"displacement_texture",        "height"},
            {"roughness_texture",           "roughness"},
            {"reflectionroughness_texture", "roughness"},
            {"specularroughness_texture",   "roughness"},
            {"metallic_texture",            "metallic"},
            {"metalness_texture",           "metallic"},
            {"emissive_mask_texture",       "emissive"},
            {"emissive_texture",            "emissive"},
            {"emissive_color_texture",      "emissive"},
            {"opacity_texture",             "opacity"},
            {"opacitymask_texture",         "opacity"},
            {"opacity",                     "opacity"},
            {"transparency_texture",        "opacity"},
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

        // Finds Studio's File > Save action (Ctrl+S) so Push can auto-save the
        // live project before reading it off disk. Prefers the Ctrl+S-bound
        // action; excludes "Save As", "Save All", "Save Copy". Returns null if
        // no suitable action is found (caller falls back to a manual prompt).
        QAction* FindBestSaveAction(QMenuBar* bar) {
            if (!bar) return nullptr;
            QList<QAction*> actions;
            for (QAction* top : bar->actions()) {
                if (!top) continue;
                actions.push_back(top);
                if (top->menu()) CollectMenuActionsRecursive(top->menu(), actions);
            }

            QAction* byShortcut = nullptr;
            QAction* byText = nullptr;
            for (QAction* a : actions) {
                if (!a || !a->isEnabled() || a->menu()) continue;
                const QString lower = NormalizeActionText(a->text()).toLower();
                const bool isVariant = lower.contains("as") || lower.contains("all")
                                       || lower.contains("copy") || lower.contains("selection");
                if (a->shortcut().matches(QKeySequence::Save) == QKeySequence::ExactMatch
                    && !isVariant) {
                    return a; // exact Ctrl+S Save — best possible
                }
                if (!isVariant && (lower == "save" || lower == "save project"
                                   || lower == "save layering project")) {
                    if (!byText) byText = a;
                }
            }
            return byShortcut ? byShortcut : byText;
        }

        // ---------------------------------------------------------------------------
        // Close ANY visible dialog/overlay in the application, whether it's a
        // QWidget modal, a QWindow, or a QML overlay. Used for error recovery.
        // ---------------------------------------------------------------------------
        void CloseAnyVisibleDialog(QMainWindow* mainWin) {
            // 1. QWidget modal dialogs
            QWidget* modal = QApplication::activeModalWidget();
            if (modal) {
                qInfo().noquote() << "[InstaMAT2Remix] Closing QWidget modal:" << modal->metaObject()->className();
                modal->close();
                QCoreApplication::processEvents();
            }

            // 2. Non-modal QWidget dialogs that appeared on top
            for (QWidget* w : QApplication::topLevelWidgets()) {
                if (!w || !w->isVisible() || w == mainWin) continue;
                if (qobject_cast<QMainWindow*>(w)) continue; // Don't close main windows
                const QString cls = w->metaObject()->className();
                if (cls.contains("Dialog", Qt::CaseInsensitive) || cls.contains("Popup", Qt::CaseInsensitive)) {
                    qInfo().noquote() << "[InstaMAT2Remix] Closing QWidget dialog:" << cls;
                    w->close();
                    QCoreApplication::processEvents();
                }
            }

            // 3. QWindow-based dialogs (QML Windows, QQuickWindow popups)
            QWindow* mainHandle = mainWin ? mainWin->windowHandle() : nullptr;
            for (QWindow* w : QGuiApplication::topLevelWindows()) {
                if (!w || !w->isVisible() || w == mainHandle) continue;
                if (w->modality() != Qt::NonModal) {
                    qInfo().noquote() << "[InstaMAT2Remix] Closing modal QWindow:" << w->title();
                    w->close();
                    QCoreApplication::processEvents();
                }
            }

            // 4. Try pressing Escape on the active focus window (QML dialogs may respond to this)
            if (QWindow* focus = QGuiApplication::focusWindow()) {
                if (focus != mainHandle) {
                    QKeyEvent press(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
                    QKeyEvent release(QEvent::KeyRelease, Qt::Key_Escape, Qt::NoModifier);
                    QCoreApplication::sendEvent(focus, &press);
                    QCoreApplication::sendEvent(focus, &release);
                    QCoreApplication::processEvents();
                }
            }
        }

        // ---------------------------------------------------------------------------
        // QObject class-name lookup helpers.
        //
        // The InstaMAT::UI::* widget classes used by the New Project dialog are
        // not exposed in any SDK header, so we identify them by
        // metaObject()->className() string match. Callers operate on QObject*
        // and either qobject_cast to a safe Qt base (QAbstractButton,
        // QToolButton, QListWidget, etc.) or use QMetaObject::invokeMethod.
        // ---------------------------------------------------------------------------

        QObject* FindFirstDescendantByClassName(QObject* root, const char* className) {
            if (!root || !className) return nullptr;
            if (qstrcmp(root->metaObject()->className(), className) == 0) return root;
            const auto kids = root->findChildren<QObject*>();
            for (QObject* k : kids) {
                if (k && qstrcmp(k->metaObject()->className(), className) == 0) return k;
            }
            return nullptr;
        }

        QList<QObject*> FindAllDescendantsByClassName(QObject* root, const char* className) {
            QList<QObject*> out;
            if (!root || !className) return out;
            if (qstrcmp(root->metaObject()->className(), className) == 0) out.push_back(root);
            for (QObject* k : root->findChildren<QObject*>()) {
                if (k && qstrcmp(k->metaObject()->className(), className) == 0) out.push_back(k);
            }
            return out;
        }

        // Polls top-level widgets and the active modal for one whose metaObject
        // class name matches. Pumps the event loop between polls. Returns
        // nullptr on timeout. timeoutMs<=0 = single-shot probe.
        QWidget* WaitForTopLevelByClassName(const char* className, int timeoutMs) {
            const qint64 deadline = QDateTime::currentMSecsSinceEpoch() + timeoutMs;
            do {
                if (QWidget* m = QApplication::activeModalWidget()) {
                    if (qstrcmp(m->metaObject()->className(), className) == 0) return m;
                    if (QObject* hit = FindFirstDescendantByClassName(m, className))
                        if (auto* w = qobject_cast<QWidget*>(hit)) return w;
                }
                for (QWidget* w : QApplication::topLevelWidgets()) {
                    if (!w || !w->isVisible()) continue;
                    if (qstrcmp(w->metaObject()->className(), className) == 0) return w;
                    if (QObject* hit = FindFirstDescendantByClassName(w, className))
                        if (auto* cw = qobject_cast<QWidget*>(hit)) return cw;
                }
                if (timeoutMs <= 0) break;
                QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
            } while (QDateTime::currentMSecsSinceEpoch() < deadline);
            return nullptr;
        }

        // Polls until QApplication::activePopupWidget() is a QMenu (or returns
        // the first visible top-level QMenu). Used to wait for the modal context
        // menu that appears after clicking PickButton.
        QMenu* WaitForActivePopupMenu(int timeoutMs) {
            const qint64 deadline = QDateTime::currentMSecsSinceEpoch() + timeoutMs;
            while (QDateTime::currentMSecsSinceEpoch() < deadline) {
                if (QWidget* p = QApplication::activePopupWidget()) {
                    if (auto* m = qobject_cast<QMenu*>(p)) return m;
                }
                for (QWidget* w : QApplication::topLevelWidgets()) {
                    if (auto* m = qobject_cast<QMenu*>(w)) {
                        if (m->isVisible()) return m;
                    }
                }
                QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
            }
            return nullptr;
        }

        bool ClickButtonViaQAbstractButton(QObject* obj, const char* logTag) {
            if (!obj) {
                qWarning().noquote() << "[InstaMAT2Remix]" << logTag << "click: object is null";
                return false;
            }
            if (auto* btn = qobject_cast<QAbstractButton*>(obj)) {
                btn->click();
                qInfo().noquote() << "[InstaMAT2Remix]" << logTag
                    << "clicked via QAbstractButton (class="
                    << obj->metaObject()->className() << ")";
                return true;
            }
            // Defensive: cross-DLL qobject_cast can fail in unusual setups.
            const bool invoked = QMetaObject::invokeMethod(obj, "click", Qt::DirectConnection);
            qInfo().noquote() << "[InstaMAT2Remix]" << logTag
                << "qobject_cast<QAbstractButton*> failed; invokeMethod(\"click\") ->"
                << invoked << "(class=" << obj->metaObject()->className() << ")";
            return invoked;
        }

        bool ActivateListWidgetItemByText(QListWidget* list,
                                          const QString& filenameWithExt,
                                          const QString& filenameNoExt) {
            if (!list) return false;
            auto match = [&](const QString& itemText) {
                if (itemText.compare(filenameWithExt, Qt::CaseInsensitive) == 0) return true;
                if (!filenameNoExt.isEmpty() &&
                    itemText.compare(filenameNoExt, Qt::CaseInsensitive) == 0) return true;
                if (!filenameNoExt.isEmpty() &&
                    itemText.contains(filenameNoExt, Qt::CaseInsensitive)) return true;
                return false;
            };
            for (int i = 0; i < list->count(); ++i) {
                QListWidgetItem* it = list->item(i);
                if (!it) continue;
                if (match(it->text())) {
                    // QListWidget::itemActivated is a protected signal — emit via meta call.
                    const bool ok = QMetaObject::invokeMethod(
                        list, "itemActivated", Qt::DirectConnection,
                        Q_ARG(QListWidgetItem*, it));
                    qInfo().noquote() << "[InstaMAT2Remix] Asset list match: row" << i
                        << "text='" << it->text() << "' invoked itemActivated ->" << ok;
                    return ok;
                }
            }
            qWarning().noquote() << "[InstaMAT2Remix] Asset list: no match for '"
                << filenameWithExt << "' / '" << filenameNoExt << "' across"
                << list->count() << "items";
            for (int i = 0; i < qMin(20, list->count()); ++i) {
                if (auto* it = list->item(i))
                    qInfo().noquote() << "[InstaMAT2Remix]   item[" << i << "]='" << it->text() << "'";
            }
            return false;
        }

        void PumpEventsFor(int ms) {
            const qint64 deadline = QDateTime::currentMSecsSinceEpoch() + ms;
            while (QDateTime::currentMSecsSinceEpoch() < deadline)
                QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        }

        // ---------------------------------------------------------------------------
        // RunNewProjectRecipe
        //
        // Drives the InstaMAT New Project dialog (QML-based, identified by class
        // names supplied by the InstaMAT engineering team) end-to-end:
        //   1. Click first IMProjectTypeSelectionButton (Asset Texturing).
        //   2. Open mesh PickButton -> deferred work runs inside its modal QMenu.
        //   3. Trigger first action -> waits for asset selection popup.
        //   4. Toolbar: enable "Show Library Objects", disable "Show Only User".
        //   5. Locate mesh in QListWidget by filename, emit itemActivated.
        //   6. Click first IMTemplateSelectionButton -> dialog closes, project loads.
        // On any failure: logs the cause and calls CloseAnyVisibleDialog so the
        // outer QDialog::exec() unblocks (no stuck modal).
        // ---------------------------------------------------------------------------
        bool RunNewProjectRecipe(QWidget* dialogRoot,
                                 const QString& meshPathAbs,
                                 QString* outError,
                                 bool* outProjectTypeUncertain,
                                 const std::function<void(const QString&)>& fileLog) {
            // Every step is mirrored into fileLog (the plugin's file Logger)
            // so a failed recipe is diagnosable from remix_connector.log alone;
            // qInfo() only reaches the debug console. Diagnostics also ride
            // along on outError for the caller's summary.
            auto diagLines = std::make_shared<QStringList>();
            auto log = [diagLines, &fileLog](const QString& s) {
                qInfo().noquote() << "[InstaMAT2Remix]" << s;
                if (fileLog) fileLog(s);
                diagLines->append(s);
            };
            // Verbose widget-introspection dumps route here: debug console +
            // diagLines only (NOT the Info file log), so a normal Pull leaves a
            // readable log. A failed recipe still surfaces them because diagLines
            // is appended to outError.
            auto logDebug = [diagLines](const QString& s) {
                qDebug().noquote() << "[InstaMAT2Remix]" << s;
                diagLines->append(s);
            };
            auto fail = [diagLines, outError, &fileLog](const QString& msg) -> bool {
                qWarning().noquote() << "[InstaMAT2Remix] Recipe FAILED:" << msg;
                if (fileLog) fileLog("FAIL: " + msg);
                diagLines->append("FAIL: " + msg);
                if (outError) *outError = msg + "\n--- recipe diagnostics ---\n"
                                              + diagLines->join("\n");
                CloseAnyVisibleDialog(FindHostMainWindow());
                return false;
            };

            if (!dialogRoot) return fail("dialogRoot is null");
            log(QString("Recipe START dialog=%1 name='%2' mesh=%3")
                .arg(dialogRoot->metaObject()->className(),
                     dialogRoot->objectName(),
                     meshPathAbs));

            QPointer<QWidget> dialogGuard(dialogRoot);
            const QFileInfo meshInfo(meshPathAbs);
            const QString meshFileName = meshInfo.fileName();
            const QString meshBaseName = meshInfo.completeBaseName();

            // Step 1: pick the "Asset Texturing" IMProjectTypeSelectionButton.
            // The buttons have empty text/tooltip/accessibleName at the
            // QAbstractButton level (confirmed by Step 8 diagnostic), so the
            // label lives in either:
            //   - a child QLabel,
            //   - a child widget's accessibleName,
            //   - a dynamic QObject property, or
            //   - a static meta-property (likely a name-like one).
            // We scan all four, matching "asset texturing". Falling back to
            // typeButtons.first() was observed to pick the Layering Project
            // tile, which causes the wizard to discard the mesh — so we only
            // do that as a last resort and log every button's introspection
            // so the next iteration has data to work from.
            const QList<QObject*> typeButtons = FindAllDescendantsByClassName(
                dialogRoot, "InstaMAT::UI::IMProjectTypeSelectionButton");
            log(QString("Recipe Step1: project type buttons found = %1").arg(typeButtons.size()));
            if (typeButtons.isEmpty())
                return fail("No IMProjectTypeSelectionButton found");

            auto matchesAssetTexturing = [](const QString& s) -> bool {
                if (s.isEmpty()) return false;
                const QString n = NormalizeActionText(s).trimmed().toLower();
                return n.contains(QLatin1String("asset texturing"))
                    || n.contains(QLatin1String("assettexturing"));
            };

            auto collectStringSources = [](QObject* btn,
                                           QStringList* labels,
                                           QStringList* childAcc,
                                           QStringList* dynProps,
                                           QStringList* staticProps) {
                if (auto* w = qobject_cast<QWidget*>(btn)) {
                    for (QLabel* lbl : w->findChildren<QLabel*>()) {
                        const QString t = lbl->text().trimmed();
                        if (!t.isEmpty()) labels->append(t);
                    }
                    for (QWidget* c : w->findChildren<QWidget*>()) {
                        const QString a = c->accessibleName().trimmed();
                        if (!a.isEmpty()) childAcc->append(a);
                    }
                }
                for (const QByteArray& pn : btn->dynamicPropertyNames()) {
                    const QVariant v = btn->property(pn.constData());
                    if (!v.canConvert<QString>()) continue;
                    const QString s = v.toString().trimmed();
                    if (!s.isEmpty() && s.length() < 120)
                        dynProps->append(QString("%1=%2")
                            .arg(QString::fromLatin1(pn), s));
                }
                const QMetaObject* mo = btn->metaObject();
                for (int i = 0; i < mo->propertyCount(); ++i) {
                    const QMetaProperty p = mo->property(i);
                    if (!p.isReadable()) continue;
                    const QString pname = QString::fromLatin1(p.name());
                    if (pname == QLatin1String("objectName")) continue;
                    // Only consider name-like properties to keep the log scannable.
                    if (!(pname.contains(QLatin1String("name"), Qt::CaseInsensitive)
                          || pname.contains(QLatin1String("type"), Qt::CaseInsensitive)
                          || pname.contains(QLatin1String("title"), Qt::CaseInsensitive)
                          || pname.contains(QLatin1String("label"), Qt::CaseInsensitive)
                          || pname.contains(QLatin1String("text"), Qt::CaseInsensitive)
                          || pname.contains(QLatin1String("role"), Qt::CaseInsensitive)
                          || pname.contains(QLatin1String("project"), Qt::CaseInsensitive)))
                        continue;
                    const QVariant v = p.read(btn);
                    if (!v.canConvert<QString>()) continue;
                    const QString s = v.toString().trimmed();
                    if (s.isEmpty() || s.length() > 120) continue;
                    staticProps->append(QString("%1=%2").arg(pname, s));
                }
            };

            QObject* assetTexBtn = nullptr;
            for (int i = 0; i < typeButtons.size(); ++i) {
                QObject* btn = typeButtons[i];
                QStringList labels, childAcc, dynProps, staticProps;
                collectStringSources(btn, &labels, &childAcc, &dynProps, &staticProps);
                logDebug(QString("  type[%1]: labels=[%2] childAcc=[%3] "
                            "dynProps=[%4] staticProps=[%5]")
                    .arg(i)
                    .arg(labels.join(" | "),
                         childAcc.join(" | "),
                         dynProps.join(" | "),
                         staticProps.join(" | ")));
                if (assetTexBtn) continue; // keep logging, but stop matching
                auto anyMatches = [&](const QStringList& xs) {
                    for (const QString& s : xs)
                        if (matchesAssetTexturing(s)) return true;
                    return false;
                };
                if (anyMatches(labels) || anyMatches(childAcc)
                    || anyMatches(dynProps) || anyMatches(staticProps))
                    assetTexBtn = btn;
            }

            if (!assetTexBtn) {
                log("Recipe Step1: no 'Asset Texturing' label match — falling "
                    "back to typeButtons.first(); inspect dump above to find "
                    "the correct identifier.");
                // The first tile has been observed to be the Material Layering
                // project type on some Studio versions. The workflow still
                // completes, but the caller surfaces a note in the Pull summary.
                if (outProjectTypeUncertain) *outProjectTypeUncertain = true;
                assetTexBtn = typeButtons.first();
            } else {
                log("Recipe Step1: matched Asset Texturing button via "
                    "introspection.");
            }

            if (!ClickButtonViaQAbstractButton(assetTexBtn, "AssetTexturingTypeBtn"))
                return fail("Could not click Asset Texturing project type button");

            // Step 2: locate the inline mesh picker widget. The dialog needs a
            // moment to swap to the project-config view after the type click, so
            // poll up to 5 s. The picker is an
            // InstaMAT::UI::IMGraphObjectPickerGroupWidget whose objectName is
            // "WIDGET_Mesh<index>" (one ILGroupWidget per form field). A second,
            // hidden picker with an empty name also exists, so we filter on the
            // "WIDGET_Mesh" prefix and prefer the visible instance.
            auto findMeshPicker = [](QObject* root) -> QObject* {
                if (!root) return nullptr;
                QObject* firstHit = nullptr;
                const auto all = root->findChildren<QObject*>();
                for (QObject* o : all) {
                    if (!o) continue;
                    if (qstrcmp(o->metaObject()->className(),
                                "InstaMAT::UI::IMGraphObjectPickerGroupWidget") != 0)
                        continue;
                    if (!o->objectName().startsWith(QStringLiteral("WIDGET_Mesh")))
                        continue;
                    if (auto* w = qobject_cast<QWidget*>(o)) {
                        if (w->isVisible()) return o;
                    }
                    if (!firstHit) firstHit = o;
                }
                return firstHit;
            };
            QObject* pickerFrame = nullptr;
            const qint64 step2Deadline = QDateTime::currentMSecsSinceEpoch() + 5000;
            while (QDateTime::currentMSecsSinceEpoch() < step2Deadline) {
                if (!dialogGuard) return fail("Dialog disappeared during Step 2 wait");
                pickerFrame = findMeshPicker(dialogGuard);
                if (pickerFrame) break;
                QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            }
            log(QString("Recipe Step2: pickerFrame = %1 name='%2'")
                .arg(pickerFrame ? pickerFrame->metaObject()->className() : QStringLiteral("null"),
                     pickerFrame ? pickerFrame->objectName() : QString()));

            if (!pickerFrame) {
                // Diagnostic dump: enumerate unique (className, objectName) pairs
                // in the dialog so we can see what classes are actually present.
                QSet<QString> seen;
                int dumped = 0;
                if (dialogGuard) {
                    logDebug(QString("Step2 diagnostic: dumping dialog '%1' children:")
                        .arg(dialogGuard->metaObject()->className()));
                    const auto allChildren = dialogGuard->findChildren<QObject*>();
                    logDebug(QString("  total descendants = %1").arg(allChildren.size()));
                    for (QObject* c : allChildren) {
                        if (!c) continue;
                        const QString key = QString("%1|%2")
                            .arg(c->metaObject()->className(), c->objectName());
                        if (seen.contains(key)) continue;
                        seen.insert(key);
                        if (dumped < 80) {
                            QString visible;
                            if (auto* w = qobject_cast<QWidget*>(c))
                                visible = w->isVisible() ? " [vis]" : " [hid]";
                            logDebug(QString("  class='%1' name='%2'%3")
                                .arg(c->metaObject()->className(),
                                     c->objectName(),
                                     visible));
                            ++dumped;
                        }
                    }
                    logDebug(QString("  unique class+name combos = %1 (showed first %2)")
                        .arg(seen.size()).arg(dumped));
                }
                return fail("Mesh picker frame not found");
            }

            QToolButton* pickButton = pickerFrame->findChild<QToolButton*>(
                "PickButton", Qt::FindChildrenRecursively);
            if (!pickButton) return fail("PickButton (QToolButton) not found in picker frame");

            // Steps 3-9 deferred: pickButton->click() below enters QMenu::exec().
            // We schedule the rest of the recipe as a queued event so it fires
            // inside that nested event loop while the menu is still open.
            auto stepResult = std::make_shared<std::pair<bool, QString>>(false, QString());

            QTimer::singleShot(0, pickButton, [stepResult, diagLines, log, logDebug,
                                               dialogGuard, meshPathAbs,
                                               meshFileName, meshBaseName]() {
                auto setErr = [&](const QString& msg) {
                    diagLines->append("FAIL: " + msg);
                    stepResult->second = msg;
                    qWarning().noquote() << "[InstaMAT2Remix] Recipe (deferred) FAILED:" << msg;
                };

                QMenu* contextMenu = WaitForActivePopupMenu(5000);
                if (!contextMenu) { setErr("Context menu after PickButton never appeared"); return; }
                log(QString("Recipe Step4: context menu has %1 actions").arg(contextMenu->actions().size()));

                QAction* firstAction = nullptr;
                for (QAction* a : contextMenu->actions()) {
                    if (!a) continue;
                    if (a->isSeparator()) continue;
                    if (!a->isEnabled()) continue;
                    firstAction = a;
                    break;
                }
                if (!firstAction) {
                    setErr("Context menu has no usable action");
                    contextMenu->close();
                    return;
                }
                log(QString("Triggering menu action: '%1'").arg(firstAction->text()));
                firstAction->trigger();

                QWidget* assetPopup = WaitForTopLevelByClassName(
                    "InstaMAT::UI::IMGraphObjectPickerPopupFrame", 5000);
                if (!assetPopup) {
                    for (QWidget* w : QApplication::topLevelWidgets()) {
                        if (w && w->isVisible() &&
                            w->objectName() == "GraphObjectImagePickerPopupFrame") {
                            assetPopup = w; break;
                        }
                    }
                }
                if (!assetPopup) { setErr("GraphObjectImagePickerPopupFrame did not appear"); return; }
                log(QString("Recipe Step5: asset popup found, class=%1")
                    .arg(assetPopup->metaObject()->className()));

                QToolBar* toolbar = assetPopup->findChild<QToolBar*>(
                    "Toolbar", Qt::FindChildrenRecursively);
                if (!toolbar) {
                    setErr("Toolbar not found in asset popup");
                    assetPopup->close();
                    return;
                }
                const QList<QAction*> toolbarActs = toolbar->actions();
                log(QString("Recipe Step6: toolbar action count = %1").arg(toolbarActs.size()));
                if (toolbarActs.size() < 3) {
                    setErr(QString("Toolbar has %1 actions, expected >=3").arg(toolbarActs.size()));
                    assetPopup->close();
                    return;
                }
                toolbarActs[1]->setChecked(true);
                log(QString("  actions[1] '%1' checked=true").arg(toolbarActs[1]->text()));
                toolbarActs[2]->setChecked(false);
                log(QString("  actions[2] '%1' checked=false").arg(toolbarActs[2]->text()));

                PumpEventsFor(400);

                QListWidget* list = assetPopup->findChild<QListWidget*>(
                    QString(), Qt::FindChildrenRecursively);
                if (!list) {
                    setErr("QListWidget not found in asset popup");
                    assetPopup->close();
                    return;
                }
                log(QString("Recipe Step7: list count = %1").arg(list->count()));
                if (!ActivateListWidgetItemByText(list, meshFileName, meshBaseName)) {
                    setErr(QString("Mesh '%1' not found in asset list").arg(meshFileName));
                    assetPopup->close();
                    return;
                }

                const qint64 popupDeadline = QDateTime::currentMSecsSinceEpoch() + 3000;
                while (QDateTime::currentMSecsSinceEpoch() < popupDeadline) {
                    if (!assetPopup || !assetPopup->isVisible()) break;
                    QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
                }

                if (!dialogGuard) { setErr("Dialog vanished before template button click"); return; }

                // Step 7.5: Force the wizard's "Up Axis" combo box to Z.
                // RTX Remix captures are Z-up by convention; leaving the wizard
                // at its default (typically Y) imports them sideways and breaks
                // downstream baking. We locate the field by its objectName
                // ("WIDGET_Up Axis4" in the current SDK; the trailing digit is
                // the form index, so we tolerate any suffix) and pick the
                // item whose visible text starts with "Z".
                {
                    QComboBox* upAxisCombo = nullptr;
                    QString upAxisGroupName;
                    for (QObject* child : dialogGuard->findChildren<QObject*>()) {
                        if (!child) continue;
                        const QString cls = QString::fromLatin1(
                            child->metaObject()->className());
                        if (!cls.endsWith(QLatin1String("ILComboBoxGroupWidget")))
                            continue;
                        const QString name = child->objectName();
                        if (!name.startsWith(QLatin1String("WIDGET_Up Axis")))
                            continue;
                        if (auto* w = qobject_cast<QWidget*>(child)) {
                            upAxisCombo = w->findChild<QComboBox*>(
                                QString(), Qt::FindChildrenRecursively);
                        }
                        upAxisGroupName = name;
                        break;
                    }
                    if (!upAxisCombo) {
                        log("Step7.5: Up Axis combo not found, skipping");
                    } else {
                        QStringList items;
                        int zIdx = -1;
                        for (int i = 0; i < upAxisCombo->count(); ++i) {
                            const QString t = upAxisCombo->itemText(i).trimmed();
                            items.append(t);
                            if (zIdx < 0 &&
                                (t.compare(QLatin1String("Z"), Qt::CaseInsensitive) == 0
                                 || t.startsWith(QLatin1String("Z "), Qt::CaseInsensitive)
                                 || t.startsWith(QLatin1String("Z-"), Qt::CaseInsensitive)
                                 || t.startsWith(QLatin1String("+Z"), Qt::CaseInsensitive)
                                 || t.compare(QLatin1String("Z Up"), Qt::CaseInsensitive) == 0))
                                zIdx = i;
                        }
                        log(QString("Step7.5: %1 items=[%2] currentText='%3'")
                            .arg(upAxisGroupName, items.join("|"),
                                 upAxisCombo->currentText()));
                        if (zIdx < 0) {
                            log("Step7.5: no 'Z' option found in Up Axis combo");
                        } else if (upAxisCombo->currentIndex() == zIdx) {
                            log("Step7.5: Up Axis already Z, no change needed");
                        } else {
                            upAxisCombo->setCurrentIndex(zIdx);
                            log(QString("Step7.5: set Up Axis to '%1' (index %2)")
                                .arg(upAxisCombo->itemText(zIdx)).arg(zIdx));
                        }
                    }
                }

                // Step 8: locate the Create button. SDK versions differ:
                //  - Older: an InstaMAT::UI::IMTemplateSelectionButton (template
                //    tile) both selected the template and created the project.
                //  - Newer: an explicit ILToolButton named "CreateProject" must
                //    be clicked after a template is picked.
                //  - Newest (observed 2026-05): neither of the above; the Create
                //    affordance is a generic QAbstractButton (likely QML-backed,
                //    empty objectName + text) carrying a "Create" tooltip or
                //    accessibleName. We fall back to a text/tooltip/accessible
                //    match while excluding known cancel / navigation buttons so
                //    we don't accidentally click CloseButton or BackToStart.
                // The form may take a moment to repaint once the asset popup
                // closes, so poll for up to 5 s for either button to appear in
                // a clickable state.
                auto findCreateButton = [](QObject* root,
                                           QString* outDesc) -> QAbstractButton* {
                    if (!root) return nullptr;

                    // Strategy 1: ILToolButton named "CreateProject" (newer SDK).
                    // The objectName is unique enough that we accept a
                    // hidden-but-enabled match — observed: when the dialog is
                    // showing the "Mesh" header tab the CreateProject button
                    // lives on the sibling "Project" panel and is collapsed,
                    // even though the form is valid (enabled=1). click() just
                    // emits clicked() regardless of visibility, so the wizard's
                    // accept handler still fires.
                    {
                        QToolButton* visibleHit = nullptr;
                        QToolButton* hiddenHit = nullptr;
                        for (QToolButton* tb : root->findChildren<QToolButton*>(
                                 QStringLiteral("CreateProject"),
                                 Qt::FindChildrenRecursively)) {
                            if (!tb || !tb->isEnabled()) continue;
                            if (tb->isVisible()) { visibleHit = tb; break; }
                            if (!hiddenHit) hiddenHit = tb;
                        }
                        if (visibleHit) {
                            if (outDesc) *outDesc = QStringLiteral("ILToolButton/CreateProject");
                            return visibleHit;
                        }
                        if (hiddenHit) {
                            if (outDesc) *outDesc = QStringLiteral("ILToolButton/CreateProject (hidden)");
                            return hiddenHit;
                        }
                    }

                    // Strategy 2: legacy IMTemplateSelectionButton.
                    if (QObject* legacy = FindFirstDescendantByClassName(
                            root, "InstaMAT::UI::IMTemplateSelectionButton")) {
                        auto* btn = qobject_cast<QAbstractButton*>(legacy);
                        if (btn && btn->isVisible() && btn->isEnabled()) {
                            if (outDesc) *outDesc = QStringLiteral("IMTemplateSelectionButton");
                            return btn;
                        }
                    }

                    // Strategy 3: alternative explicit object names some SDK
                    // builds use. Cheaper than the text scan and avoids any
                    // ambiguity for buttons whose names match exactly.
                    static const char* kCandidateNames[] = {
                        "CreateProjectButton", "CreateButton", "CreateBtn",
                        "Create", "OkButton", "ApplyButton", "ConfirmButton",
                        "FinishButton", "DoneButton",
                    };
                    for (const char* candName : kCandidateNames) {
                        for (QAbstractButton* b : root->findChildren<QAbstractButton*>(
                                 QString::fromLatin1(candName),
                                 Qt::FindChildrenRecursively)) {
                            if (b && b->isVisible() && b->isEnabled()) {
                                if (outDesc) *outDesc =
                                    QString("name='%1' class='%2'")
                                        .arg(QString::fromLatin1(candName),
                                             b->metaObject()->className());
                                return b;
                            }
                        }
                    }

                    // Strategy 4: semantic match on text / tooltip /
                    // accessibleName. We must skip the dialog's well-known
                    // navigation/cancel buttons so we never click CloseButton,
                    // BackToStart, PickButton, the tab headers, or one of the
                    // (now disabled) project-type tiles.
                    auto isExcludedNav = [](QAbstractButton* b) -> bool {
                        const QString name = b->objectName();
                        return name == QLatin1String("CloseButton")
                            || name == QLatin1String("BackToStart")
                            || name == QLatin1String("PickButton")
                            || name == QLatin1String("HeaderToolButton")
                            || name == QLatin1String("AQSwitch")
                            || name == QLatin1String("ProjectTypeSelectionButton");
                    };
                    auto matchesCreate = [](const QString& s) -> bool {
                        if (s.isEmpty()) return false;
                        const QString n = NormalizeActionText(s).trimmed().toLower();
                        if (n.isEmpty()) return false;
                        if (n.contains(QLatin1String("create"))) return true;
                        return n == QLatin1String("ok")
                            || n == QLatin1String("apply")
                            || n == QLatin1String("confirm")
                            || n == QLatin1String("finish")
                            || n == QLatin1String("done")
                            || n == QLatin1String("generate")
                            || n == QLatin1String("build");
                    };
                    for (QAbstractButton* b : root->findChildren<QAbstractButton*>()) {
                        if (!b || !b->isVisible() || !b->isEnabled()) continue;
                        if (isExcludedNav(b)) continue;
                        if (matchesCreate(b->text())
                            || matchesCreate(b->toolTip())
                            || matchesCreate(b->accessibleName())
                            || matchesCreate(b->accessibleDescription())) {
                            if (outDesc) *outDesc = QString(
                                "textmatch class='%1' name='%2' text='%3' "
                                "tip='%4' acc='%5'")
                                .arg(b->metaObject()->className(),
                                     b->objectName(),
                                     b->text(),
                                     b->toolTip(),
                                     b->accessibleName());
                            return b;
                        }
                    }

                    return nullptr;
                };
                QAbstractButton* createBtn = nullptr;
                QString createBtnDesc;
                const qint64 createDeadline = QDateTime::currentMSecsSinceEpoch() + 5000;
                while (QDateTime::currentMSecsSinceEpoch() < createDeadline) {
                    if (!dialogGuard) { setErr("Dialog vanished before Create click"); return; }
                    createBtn = findCreateButton(dialogGuard, &createBtnDesc);
                    if (createBtn) break;
                    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
                }

                if (!createBtn) {
                    // Diagnostic dump: enumerate visible buttons so we can see
                    // what's actually present (e.g. whether a template tile
                    // needs picking before Create becomes enabled). Also dump
                    // hidden buttons whose text/tooltip/accessibleName mentions
                    // "create" — they're prime suspects for an SDK class we
                    // don't recognize yet. Include tooltip + accessibleName
                    // because the QML-backed anonymous buttons we've seen carry
                    // their label there rather than in text().
                    if (dialogGuard) {
                        const auto allBtns = dialogGuard->findChildren<QAbstractButton*>();
                        int visibleCount = 0;
                        for (QAbstractButton* b : allBtns)
                            if (b && b->isVisible()) ++visibleCount;
                        logDebug(QString("Step8 diagnostic: %1 total buttons, %2 visible")
                            .arg(allBtns.size()).arg(visibleCount));

                        logDebug("  --- visible buttons ---");
                        int dumped = 0;
                        for (QAbstractButton* b : allBtns) {
                            if (!b || !b->isVisible()) continue;
                            logDebug(QString("  class='%1' name='%2' text='%3' "
                                        "tip='%4' acc='%5' enabled=%6")
                                .arg(b->metaObject()->className(),
                                     b->objectName(),
                                     b->text(),
                                     b->toolTip(),
                                     b->accessibleName())
                                .arg(b->isEnabled()));
                            if (++dumped >= 80) {
                                logDebug(QString("  (visible dump truncated at %1)").arg(dumped));
                                break;
                            }
                        }

                        // Hidden buttons whose name/text/tooltip/accName
                        // mentions "create" — likely the real Create button
                        // waiting on a pending repaint or hidden behind a tab.
                        logDebug("  --- hidden buttons matching 'create' ---");
                        int hiddenHits = 0;
                        for (QAbstractButton* b : allBtns) {
                            if (!b || b->isVisible()) continue;
                            const QString blob = (b->objectName() + "|"
                                                 + b->text() + "|"
                                                 + b->toolTip() + "|"
                                                 + b->accessibleName()).toLower();
                            if (!blob.contains(QLatin1String("create"))) continue;
                            logDebug(QString("  HIDDEN class='%1' name='%2' text='%3' "
                                        "tip='%4' acc='%5' enabled=%6")
                                .arg(b->metaObject()->className(),
                                     b->objectName(),
                                     b->text(),
                                     b->toolTip(),
                                     b->accessibleName())
                                .arg(b->isEnabled()));
                            if (++hiddenHits >= 20) break;
                        }
                        if (hiddenHits == 0)
                            logDebug("  (none)");

                        // Unique button class names across the whole tree —
                        // helps identify a renamed Create button class.
                        QSet<QString> uniqueClasses;
                        for (QAbstractButton* b : allBtns)
                            if (b) uniqueClasses.insert(QString::fromLatin1(
                                       b->metaObject()->className()));
                        QStringList classList(uniqueClasses.begin(), uniqueClasses.end());
                        classList.sort();
                        logDebug(QString("  --- unique button classes (%1) ---")
                            .arg(classList.size()));
                        logDebug(QString("  %1").arg(classList.join(", ")));
                    }
                    setErr("Create button not found (looked for ILToolButton/CreateProject, IMTemplateSelectionButton, common alt names, and 'create' text/tooltip/accessibleName)");
                    return;
                }

                log(QString("Recipe Step8: createBtn = %1 text='%2'")
                    .arg(createBtnDesc, createBtn->text()));
                if (!ClickButtonViaQAbstractButton(createBtn, "CreateProjectBtn")) {
                    setErr(QString("Could not click %1").arg(createBtnDesc));
                    return;
                }

                const qint64 closeDeadline = QDateTime::currentMSecsSinceEpoch() + 10000;
                while (QDateTime::currentMSecsSinceEpoch() < closeDeadline) {
                    if (!dialogGuard || !dialogGuard->isVisible()) {
                        stepResult->first = true;
                        log("Recipe SUCCESS: dialog closed.");
                        return;
                    }
                    QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
                }
                setErr("Dialog did not close after Create click");
            });

            log("Recipe Step3: clicking PickButton (enters QMenu::exec)");
            pickButton->click();
            log("Recipe: PickButton click() returned (menu closed)");

            const qint64 outerDeadline = QDateTime::currentMSecsSinceEpoch() + 12000;
            while (QDateTime::currentMSecsSinceEpoch() < outerDeadline) {
                if (stepResult->first || !stepResult->second.isEmpty()) break;
                if (!dialogGuard || !dialogGuard->isVisible()) {
                    stepResult->first = true;
                    break;
                }
                QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
            }

            if (stepResult->first) {
                // Surface the diagnostic trail to the caller even on success so
                // we can audit which project-type button was matched and which
                // Create-button strategy fired. The caller distinguishes
                // success from failure by the return value, not by outError.
                if (outError) *outError = QString("--- recipe diagnostics (SUCCESS) ---\n%1")
                                              .arg(diagLines->join("\n"));
                return true;
            }
            return fail(stepResult->second.isEmpty()
                ? QString("Recipe timed out without completing")
                : stepResult->second);
        }

        // ---------------------------------------------------------------------------
        // Helper: Detects and dismisses a "Save Changes?" dialog that may appear
        // when triggering File > New while a project is already open.
        // This dialog is a standard QMessageBox (QWidget-based), so QWidget
        // detection works reliably here — unlike the QML-based New Project wizard.
        // Returns true if a Save Changes dialog was found and dismissed.
        // ---------------------------------------------------------------------------
        bool HandleSaveChangesPrompt(QWidget* modal) {
            if (!modal) return false;

            // Check window title for save/changes context
            const QString title = modal->windowTitle().toLower();
            bool looksLikeSavePrompt = title.contains("save") || title.contains("changes") || title.contains("closing");

            // Double-check labels if title is generic (e.g. just the app name)
            if (!looksLikeSavePrompt) {
                const auto labels = modal->findChildren<QLabel*>();
                for (QLabel* l : labels) {
                    if (!l) continue;
                    const QString lt = l->text().toLower();
                    if (lt.contains("save changes") || lt.contains("unsaved") || lt.contains("do you want to save")) {
                        looksLikeSavePrompt = true;
                        break;
                    }
                }
            }

            if (!looksLikeSavePrompt) return false;

            qInfo().noquote() << "[InstaMAT2Remix] Detected 'Save Changes' prompt. Dismissing to force new project.";

            // Look for "Don't Save", "Discard", or "No" button
            const auto buttons = modal->findChildren<QAbstractButton*>();
            for (QAbstractButton* b : buttons) {
                if (!b) continue;
                const QString t = NormalizeActionText(b->text()).toLower();
                if (t.contains("don't save") || t.contains("discard") || t == "no") {
                    b->click();
                    return true;
                }
            }

            // Fallback for standard QMessageBox
            if (auto* msgBox = qobject_cast<QMessageBox*>(modal)) {
                QAbstractButton* discard = msgBox->button(QMessageBox::Discard);
                if (discard) { discard->click(); return true; }
                QAbstractButton* no = msgBox->button(QMessageBox::No);
                if (no) { no->click(); return true; }
            }

            qWarning().noquote() << "[InstaMAT2Remix] Save Changes prompt detected but could not find dismiss button.";
            return false;
        }

        // ---------------------------------------------------------------------------
        // Orchestrator: Attempts to automatically create an Asset Texturing project
        // with the given mesh by driving InstaMAT's New Project dialog via the
        // class names provided by the InstaMAT engineering team.
        //
        // Two cases:
        //   1) The new-project dialog is already visible (e.g. start screen) ->
        //      run the recipe directly on it.
        //   2) Need to trigger File -> New, which opens a blocking QDialog::exec().
        //      Schedule the recipe via QTimer to fire inside that nested event loop.
        // ---------------------------------------------------------------------------
        bool TryCreateTexturingProjectFromMesh(const QString& meshPathAbs,
                                               const QString& suggestedName,
                                               QString* outError,
                                               bool* outProjectTypeUncertain,
                                               const std::function<void(const QString&)>& fileLog) {
            if (outError) outError->clear();
            if (outProjectTypeUncertain) *outProjectTypeUncertain = false;
            (void)suggestedName; // The recipe reads the asset library entry, not a name field.

            auto log = [&fileLog](const QString& s) {
                qInfo().noquote() << "[InstaMAT2Remix]" << s;
                if (fileLog) fileLog(s);
            };

            QMainWindow* win = FindHostMainWindow();
            if (!win) {
                if (outError) *outError = "Could not find InstaMAT main window.";
                return false;
            }

            // Case 1: dialog already visible (start screen, or user opened it manually).
            if (QWidget* existing = WaitForTopLevelByClassName(
                    "InstaMAT::UI::IMProjectTypeSelectionDialog", 0)) {
                log("Auto-create: IMProjectTypeSelectionDialog already visible, running recipe directly.");
                return RunNewProjectRecipe(existing, meshPathAbs, outError,
                                           outProjectTypeUncertain, fileLog);
            }

            // Case 2: trigger File -> New and run the recipe inside the nested exec().
            QMenuBar* bar = win->findChild<QMenuBar*>();
            QAction* newAction = FindBestNewProjectAction(bar);
            if (!newAction) {
                if (outError) *outError = "Could not find a 'New Project' action in the menu bar.";
                return false;
            }

            struct AutoState {
                bool success = false;
                bool typeUncertain = false;
                QString error;
            };
            auto state = std::make_shared<AutoState>();

            // Schedule the recipe to fire inside the about-to-block exec()'s
            // event loop. fileLog is copied into the lambda: the deferred
            // callback outlives this function's stack frame.
            const std::function<void(const QString&)> deferredFileLog = fileLog;
            QTimer::singleShot(500, win, [state, win, meshPathAbs, deferredFileLog]() {
                auto log = [&deferredFileLog](const QString& s) {
                    qInfo().noquote() << "[InstaMAT2Remix]" << s;
                    if (deferredFileLog) deferredFileLog(s);
                };
                log("Auto-create: deferred callback fired inside dialog event loop.");

                // Wait up to 10s for the recipe dialog to appear, dismissing any
                // "Save Changes?" prompt that may block first.
                QWidget* recipeDlg = nullptr;
                const qint64 deadline = QDateTime::currentMSecsSinceEpoch() + 10000;
                while (QDateTime::currentMSecsSinceEpoch() < deadline) {
                    QWidget* modal = QApplication::activeModalWidget();
                    if (modal && modal != win) {
                        if (HandleSaveChangesPrompt(modal)) {
                            log("Dismissed Save Changes prompt. Waiting for project dialog...");
                            QCoreApplication::processEvents(QEventLoop::AllEvents, 200);
                            continue;
                        }
                        if (qstrcmp(modal->metaObject()->className(),
                                    "InstaMAT::UI::IMProjectTypeSelectionDialog") == 0) {
                            recipeDlg = modal;
                            break;
                        }
                    }
                    if ((recipeDlg = WaitForTopLevelByClassName(
                            "InstaMAT::UI::IMProjectTypeSelectionDialog", 0))) {
                        break;
                    }
                    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
                }

                if (!recipeDlg) {
                    state->error =
                        "Recipe dialog class 'InstaMAT::UI::IMProjectTypeSelectionDialog' "
                        "not detected. The InstaMAT-supplied class name may have changed.";
                    qWarning().noquote() << "[InstaMAT2Remix]" << state->error;
                    if (deferredFileLog) deferredFileLog("FAIL: " + state->error);
                    CloseAnyVisibleDialog(FindHostMainWindow());
                    return;
                }

                log(QString("Recipe dialog detected: %1 name='%2'")
                        .arg(QString::fromUtf8(recipeDlg->metaObject()->className()),
                             recipeDlg->objectName()));

                state->success = RunNewProjectRecipe(recipeDlg, meshPathAbs, &state->error,
                                                     &state->typeUncertain, deferredFileLog);

                if (!state->success) {
                    log("Auto-create failed inside dialog event loop. Closing dialog to unblock.");
                    CloseAnyVisibleDialog(FindHostMainWindow());
                }
            });

            log(QString("Auto-create: triggering action: %1").arg(newAction->text()));
            newAction->trigger();

            // exec() has returned — the dialog is closed (either by the recipe's
            // template-button click on success, or by CloseAnyVisibleDialog on failure).
            if (outError) *outError = state->error;
            if (outProjectTypeUncertain) *outProjectTypeUncertain = state->typeUncertain;
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

        const QString baseUrl = settings.value("RemixApiBaseUrl", kDefaultApiBaseUrl).toString().trimmed();
        m_remixApiBaseUrl = (baseUrl.isEmpty() ? kDefaultApiBaseUrl : baseUrl).toStdString();

        m_linkedMaterialPrim = settings.value("LinkedMaterialPrim", "").toString().toStdString();
        m_linkedMeshPath = settings.value("LinkedMeshPath", "").toString().toStdString();

        const QString blender = settings.value("BlenderPath", "").toString().trimmed();
        m_tools.SetBlenderExecutable(blender.toStdString());

        QString texconv = settings.value("TexconvPath", "").toString().trimmed();
        if (texconv.isEmpty() || !QFileInfo::exists(texconv)) texconv = DetectTexconvPath();
        m_tools.SetTexconvExecutable(texconv.toStdString());
        if (texconv.isEmpty()) {
            m_logger.Warning("texconv.exe not found in any known install location. "
                             "DDS->PNG conversion will fail (Pull will write an empty manifest). "
                             "Set Texconv Path in Settings to fix.");
        }

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

        // Path validity suffixes (WBC parity: texconv/Blender show OK/MISSING).
        const auto pathStatus = [](const QString& p) -> QString {
            if (p.trimmed().isEmpty()) return "(not set)";
            return QFileInfo::exists(p) ? (p + "  [OK]") : (p + "  [MISSING]");
        };

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
        lines << QString("API Base URL: %1").arg(settings.value("RemixApiBaseUrl", kDefaultApiBaseUrl).toString());
        lines << QString("Poll Timeout (sec): %1").arg(settings.value("PollTimeoutSec", 60.0).toDouble());
        lines << QString("Log Level: %1").arg(settings.value("LogLevel", "info").toString());
        lines << QString("Remix Output Subfolder: %1").arg(settings.value("RemixOutputSubfolder", "Textures/InstaMAT2Remix_Ingested").toString());
        lines << QString("Export Folder: %1").arg(settings.value("ExportFolder",
                                                                 QDir::cleanPath(QDir::tempPath() + "/InstaMAT2Remix_Export")).toString());
        lines << QString("Export Format: %1").arg(settings.value("ExportFileFormat", "png").toString());
        lines << QString("Export Resolution: %1").arg([&settings]() {
                            const int r = settings.value("ExportResolution", 0).toInt();
                            return r > 0 ? QString::number(r) : QString("Auto (match Remix texture)");
                        }());
        lines << QString("Include Opacity Map: %1").arg(settings.value("IncludeOpacityMap", false).toBool() ? "true" : "false");
        lines << QString("Restore Aspect On Export: %1").arg(settings.value("RestoreAspectOnExport", true).toBool() ? "true" : "false");
        lines << QString("Auto Unwrap: %1").arg(settings.value("AutoUnwrap", false).toBool() ? "true" : "false");
        lines << QString("Use Tiling Mesh On Pull: %1").arg(settings.value("UseTilingMeshOnPull", false).toBool() ? "true" : "false");
        lines << QString("Tiling Mesh Path: %1").arg(pathStatus(settings.value("TilingMeshPath", "").toString()));
        lines << QString("Blender Path: %1").arg(pathStatus(settings.value("BlenderPath", "").toString()));
        lines << QString("Texconv Path: %1").arg(pathStatus(settings.value("TexconvPath", "").toString()));
        lines << "";
        lines << "=== Link State ===";
        lines << QString("Linked Material Prim: %1").arg(QString::fromStdString(m_linkedMaterialPrim));
        lines << QString("Linked Mesh Path: %1").arg(QString::fromStdString(m_linkedMeshPath));
        lines << "";
        lines << "=== Connectivity ===";
        lines << QString("RTX Remix API: %1").arg(ok ? "OK" : "FAILED");
        lines << msg;
        lines << "";
        lines << QString("Repo: %1").arg(kPluginRepoUrl);

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

    QString RemixConnector::ResolveCanonicalChannel(const QString& usdAttr) {
        const int colon = usdAttr.lastIndexOf(QLatin1Char(':'));
        const QString suffix = (colon >= 0) ? usdAttr.mid(colon + 1) : usdAttr;
        const auto it = kRemixAttrSuffixToPbr.find(suffix);
        return (it != kRemixAttrSuffixToPbr.end()) ? it.value() : QString();
    }

    QJsonDocument RemixConnector::RequestJson(const QString& method,
                                              const QString& endpoint,
                                              const QMap<QString, QString>& params,
                                              const QJsonDocument* body,
                                              QString* outError,
                                              double timeoutSecOverride,
                                              int    maxAttemptsOverride) const {
        if (outError) outError->clear();

        QString base = QString::fromStdString(m_remixApiBaseUrl).trimmed();
        if (base.isEmpty()) base = kDefaultApiBaseUrl;
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

        // Respect user-configured timeout if available; per-call override (e.g.
        // the long-running ingest endpoint) wins over the setting.
        const double resolvedTimeoutSec = [&] {
            if (timeoutSecOverride > 0.0) return timeoutSecOverride;
            QSettings settings("InstaMAT2Remix", "Config");
            return settings.value("PollTimeoutSec", 60.0).toDouble();
        }();
        const int resolvedTimeoutMs = qMax(200, int(resolvedTimeoutSec * 1000.0));
        req.setTransferTimeout(resolvedTimeoutMs);

        const QByteArray payload = body ? body->toJson(QJsonDocument::Compact) : QByteArray();
        const QString m = method.toUpper();

        // Retry loop: default 3 attempts with 2s/4s backoff (matches Substance2Remix).
        // Callers can override the attempt count (e.g. ingest sets 1 — re-queuing
        // an in-flight job on the server is worse than failing the call).
        const int maxAttempts = maxAttemptsOverride > 0 ? maxAttemptsOverride : 3;
        const int retryDelaysMs[] = {2000, 4000};
        QString lastError;

        for (int attempt = 0; attempt < maxAttempts; ++attempt) {
            if (attempt > 0) {
                qInfo().noquote() << "[InstaMAT2Remix] Retry" << attempt << "of" << (maxAttempts - 1) << "for" << endpoint;
                const int delayIdx = qMin(attempt - 1, int(sizeof(retryDelaysMs) / sizeof(retryDelaysMs[0])) - 1);
                QThread::msleep(retryDelaysMs[delayIdx]);
            }

            QNetworkAccessManager mgr;
            QNetworkReply* reply = nullptr;

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
            timeoutTimer.start(resolvedTimeoutMs);
            QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
            QObject::connect(&timeoutTimer, &QTimer::timeout, &loop, &QEventLoop::quit);
            loop.exec();

            if (!reply->isFinished()) {
                reply->abort();
                lastError = "RTX Remix API request timed out.";
                reply->deleteLater();
                continue; // Retry on timeout
            }

            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const QByteArray bytes = reply->readAll();

            const auto errCode = reply->error();
            if (errCode != QNetworkReply::NoError) {
                QString details = reply->errorString();
                if (!bytes.isEmpty()) details += " | " + QString::fromUtf8(bytes.left(600));
                lastError = QString("HTTP %1: %2").arg(status).arg(details);
                m_logger.Warning(QString("%1 %2 failed: %3").arg(m, endpoint, lastError));
                reply->deleteLater();

                // Only retry on network/connection errors, not on HTTP 4xx/5xx.
                if (errCode == QNetworkReply::ConnectionRefusedError ||
                    errCode == QNetworkReply::RemoteHostClosedError ||
                    errCode == QNetworkReply::HostNotFoundError ||
                    errCode == QNetworkReply::TimeoutError ||
                    errCode == QNetworkReply::TemporaryNetworkFailureError ||
                    errCode == QNetworkReply::NetworkSessionFailedError ||
                    errCode == QNetworkReply::UnknownNetworkError) {
                    continue; // Retry on network errors
                }
                // Non-retriable error (4xx, 5xx, etc.)
                if (outError) *outError = lastError;
                return {};
            }

            reply->deleteLater();

            // Match Substance2Remix's contract: any 2xx is success regardless
            // of body content. Mutations (PUT /stagecraft/textures/,
            // POST .../save) return either an empty body or a plain status
            // string like "OK" — neither parses as JSON, but Remix accepted
            // the request. Return a non-null empty document on any parse
            // failure so callers that check isNull() see success; callers
            // that need data still fail their downstream isObject() /
            // isArray() check with a clear "Remix returned X missing"
            // message.
            if (bytes.trimmed().isEmpty()) {
                return QJsonDocument(QJsonObject());
            }

            QJsonParseError perr;
            QJsonDocument doc = QJsonDocument::fromJson(bytes, &perr);
            if (perr.error != QJsonParseError::NoError) {
                const QString bodyExcerpt = QString::fromUtf8(bytes.left(400));
                m_logger.Info(QString("%1 %2 HTTP %3 non-JSON body (treated as success): %4 | body[0..400]: %5")
                                  .arg(m).arg(endpoint).arg(status)
                                  .arg(perr.errorString(), bodyExcerpt));
                return QJsonDocument(QJsonObject());
            }

            return doc;
        }

        // All retries exhausted.
        if (outError) *outError = lastError.isEmpty() ? "Request failed after retries." : lastError;
        return {};
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
        // Documents-rooted (not %TEMP%): with the WBC-strict Pull these files
        // are the user's only local copies and are dragged onto channels
        // manually across a whole session. Mirrors WBC's persistent
        // "Pulled Textures/<project>" layout.
        const QString projectName = DeriveProjectNameFromRemixDir(remixDefaultDirAbs);
        QString docs = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
        if (docs.isEmpty()) docs = QDir::homePath();
        const QString root = QDir(docs).filePath("InstaMAT2Remix/Pulled Textures/" + projectName);
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

    void RemixConnector::PullFromRemix() {
        // ---------------------------------------------------------------------------
        // WBC-parity Pull (Substance2Remix behavior): resolve the Remix
        // selection to a mesh + material, optionally substitute the tiling
        // plane or Smart-UV-unwrap via Blender, then auto-create an InstaMAT
        // project from the mesh and link it. Zero prompts; no texture
        // download — textures come from 'Import Textures from Remix'.
        // ---------------------------------------------------------------------------
        QSettings settings("InstaMAT2Remix", "Config");
        const bool autoUnwrap = settings.value("AutoUnwrap", false).toBool();

        QProgressDialog progress("Querying RTX Remix for selection...", "Cancel", 0, 3, nullptr);
        progress.setWindowTitle(kPluginName);
        progress.setWindowModality(Qt::WindowModal);
        progress.setMinimumDuration(0);
        progress.setValue(0);
        progress.show();
        QCoreApplication::processEvents();

        // Step 0: Query Remix for selection details.
        QString err;
        RemixSelectionDetails details;
        if (!GetSelectedRemixAssetDetails(details, err)) {
            progress.close();
            QMessageBox::warning(nullptr, kPluginName, "Pull From Remix failed:\n\n" + err);
            return;
        }

        if (progress.wasCanceled()) { progress.close(); return; }

        const QString meshPathRaw = QString::fromStdString(details.meshFilePath);
        const QString ctxFile = QString::fromStdString(details.contextFilePath);
        const QString materialPrim = QString::fromStdString(details.materialPrimPath);

        const bool useTilingMesh = settings.value("UseTilingMeshOnPull", false).toBool();
        QString tilingMeshPath = QDir::cleanPath(settings.value("TilingMeshPath", "").toString());

        // If the tiling mesh path is relative, interpret it relative to this plugin dir.
        if (useTilingMesh && !tilingMeshPath.isEmpty() && !QDir::isAbsolutePath(tilingMeshPath)) {
            const QString abs = QDir(GetPluginDirPath()).filePath(tilingMeshPath);
            if (QFileInfo::exists(abs)) tilingMeshPath = QDir::cleanPath(abs);
        }

        // If tiling mesh is requested but not configured, fall back to the built-in tiling plane.
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
            progress.close();
            QMessageBox::warning(nullptr, kPluginName, "Mesh path is relative, but no absolute context file was provided by Remix.");
            return;
        }

        if (useTilingMesh) {
            if (!tilingMeshPath.isEmpty() && QFileInfo::exists(tilingMeshPath)) {
                meshAbs = QDir::cleanPath(tilingMeshPath);
            } else {
                progress.close();
                QMessageBox::warning(nullptr, kPluginName,
                                     "Tiling mesh is enabled, but no valid tiling mesh path is configured.\n\n"
                                     "Set 'Tiling Mesh Path' in Settings, or ensure the plugin ships its default tiling mesh.");
                return;
            }
        }

        if (!QFileInfo::exists(meshAbs)) {
            progress.close();
            QMessageBox::warning(nullptr, kPluginName, "Mesh file does not exist locally:\n\n" + meshAbs);
            return;
        }

        // Step 1: Auto-unwrap with Blender (if enabled).
        if (progress.wasCanceled()) { progress.close(); return; }
        progress.setLabelText("Preparing mesh...");
        progress.setValue(1);
        QCoreApplication::processEvents();

        QString finalMesh = meshAbs;

        if (autoUnwrap && !useTilingMesh && finalMesh == meshAbs
                && !m_tools.GetBlenderExecutable().empty()) {
            progress.setLabelText("Auto-unwrapping mesh with Blender...");
            QCoreApplication::processEvents();

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
                m_logger.Warning("Auto-unwrap failed, using original mesh.");
            }
        }

        // Cache link state.
        m_linkedMaterialPrim = materialPrim.toStdString();
        m_linkedMeshPath = finalMesh.toStdString();
        settings.setValue("LinkedMaterialPrim", materialPrim);
        settings.setValue("LinkedMeshPath", finalMesh);

        // Register external folder so the mesh is easy to find in InstaMAT's library picker.
        m_instaMAT.RegisterExternalAssetFolder(QFileInfo(finalMesh).dir().absolutePath().toStdString().c_str());

        if (QGuiApplication::clipboard()) QGuiApplication::clipboard()->setText(finalMesh);

        // Step 2: Drive InstaMAT's New Project dialog so the mesh opens as an
        // Asset Texturing project automatically (Substance2Remix-style).
        if (progress.wasCanceled()) { progress.close(); return; }
        progress.setLabelText("Creating InstaMAT project...");
        progress.setValue(2);
        QCoreApplication::processEvents();

        const QString suggestedProjectName = QFileInfo(finalMesh).completeBaseName();
        QString recipeErr;
        bool projectTypeUncertain = false;
        auto recipeFileLog = [this](const QString& s) { m_logger.Info("Recipe: " + s); };
        const bool projectCreated = TryCreateTexturingProjectFromMesh(
            finalMesh, suggestedProjectName, &recipeErr, &projectTypeUncertain, recipeFileLog);

        progress.setValue(3);
        progress.close();

        if (projectCreated) {
            m_logger.Info(QString("Auto-create project succeeded for mesh: %1").arg(finalMesh));
            if (projectTypeUncertain) {
                m_logger.Warning("Project type could not be confirmed as Asset Texturing — "
                                 "the recipe fell back to the first project-type tile "
                                 "(possibly Material Layering).");
            }

            QStringList summary;
            summary << "Project created and linked to Remix.";
            summary << "";
            summary << QString("Material: %1").arg(materialPrim);
            summary << QString("Mesh: %1").arg(QFileInfo(finalMesh).fileName());
            summary << "";
            summary << "Use 'Import Textures from Remix' to pull this material's "
                       "textures when you want them.";
            if (projectTypeUncertain) {
                summary << "";
                summary << "Note: the project type could not be auto-confirmed as "
                           "'Asset Texturing'. InstaMAT may have created a 'Material "
                           "Layering' project instead. Painting and Push To Remix still "
                           "work as normal.";
            }
            QMessageBox::information(nullptr, kPluginName, summary.join("\n"));
        } else {
            m_logger.Warning(QString("Auto-create project failed: %1").arg(recipeErr));

            // Fallback: the mesh path is already on the clipboard; open its
            // folder so the user can create the project manually.
            const QString meshDir = QFileInfo(finalMesh).dir().absolutePath();
            QMessageBox::warning(
                nullptr, kPluginName,
                QString("Pull From Remix: the project could not be created automatically:\n\n"
                        "%1\n\n"
                        "The mesh path was copied to your clipboard and its folder was "
                        "opened in Explorer:\n%2\n\n"
                        "Create a project manually (File > New) and pick the mesh, then "
                        "use 'Import Textures from Remix' as usual. "
                        "Details are in the plugin log.")
                    .arg(recipeErr.section('\n', 0, 0), finalMesh));
            QDesktopServices::openUrl(QUrl::fromLocalFile(meshDir));
        }
    }

    bool RemixConnector::DownloadAndConvertTextureList(const QJsonArray& textures,
                                                       const QString&    remixDirAbs,
                                                       const QString&    destDir,
                                                       NamingPolicy      policy,
                                                       QProgressDialog*  progress,
                                                       QVector<ChannelEntry>& outEntries,
                                                       int& outPulledCount,
                                                       int& outConvertedCount) {
        outEntries.clear();
        outPulledCount = 0;
        outConvertedCount = 0;

        if (!QDir().mkpath(destDir)) {
            m_logger.Warning(QString("Could not create output directory: %1").arg(destDir));
            return false;
        }

        int idx = 0;
        for (const QJsonValue& entry : textures) {
            if (progress && progress->wasCanceled()) break;
            if (!entry.isArray()) continue;
            const QJsonArray pair = entry.toArray();
            if (pair.size() < 2) continue;
            if (!pair.at(0).isString() || !pair.at(1).isString()) continue;

            const QString usdAttr    = pair.at(0).toString();
            const QString texPathRaw = NormalizePathSlashes(pair.at(1).toString());

            if (progress) {
                progress->setLabelText(QString("Writing %1\n%2").arg(QFileInfo(texPathRaw).fileName(), usdAttr));
                progress->setValue(idx++);
                QCoreApplication::processEvents();
            }

            QString absTex = texPathRaw;
            if (!QDir::isAbsolutePath(absTex)) absTex = QDir(remixDirAbs).filePath(absTex);
            absTex = QDir::cleanPath(absTex);

            if (!QFileInfo::exists(absTex)) {
                m_logger.Warning(QString("Missing texture file: %1 (for %2)").arg(absTex, usdAttr));
                continue;
            }

            const QString origName = QFileInfo(absTex).fileName();
            QString canonicalChannel;
            if (policy == NamingPolicy::CanonicalChannelOrOriginal) {
                canonicalChannel = ResolveCanonicalChannel(usdAttr);
            }

            // Decide the destination filename based on policy + canonical channel.
            // For DDS we let texconv pick the extension (.png) and rename below
            // when canonical naming is requested; the preserve-original filename
            // is only consulted in the non-DDS branch.
            const QString lower = absTex.toLower();
            if (lower.endsWith(".dds") || lower.endsWith(".rtex.dds")) {
                std::string pngOut;
                // ConvertDdsToPng writes <destDir>/<input-basename>.png; we then rename to the
                // canonical name when needed so the on-disk filename matches the spec.
                if (m_tools.ConvertDdsToPng(absTex.toStdString(), destDir.toStdString(), pngOut)) {
                    QString writtenPath = QString::fromStdString(pngOut);
                    QString writtenName = QFileInfo(writtenPath).fileName();
                    if (!canonicalChannel.isEmpty()) {
                        const QString desired = canonicalChannel + ".png";
                        const QString desiredPath = QDir(destDir).filePath(desired);
                        if (desiredPath != writtenPath) {
                            QFile::remove(desiredPath);
                            if (QFile::rename(writtenPath, desiredPath)) {
                                writtenName = desired;
                            } else {
                                m_logger.Warning(QString("Could not rename %1 -> %2").arg(writtenPath, desiredPath));
                            }
                        }
                    }
                    outConvertedCount++;
                    outPulledCount++;
                    outEntries.append({usdAttr, canonicalChannel, writtenName});
                } else {
                    m_logger.Warning(QString("texconv failed converting: %1").arg(absTex));
                }
                continue;
            }

            // Non-DDS: copy with chosen filename (canonical channel + original extension, or raw filename).
            QString destName;
            if (!canonicalChannel.isEmpty()) {
                const QString ext = QFileInfo(absTex).suffix();
                destName = ext.isEmpty() ? canonicalChannel : (canonicalChannel + "." + ext);
            } else {
                destName = origName;
            }
            const QString destPath = QDir(destDir).filePath(destName);
            QFile::remove(destPath);
            if (QFile::copy(absTex, destPath)) {
                outPulledCount++;
                outEntries.append({usdAttr, canonicalChannel, destName});
            }
        }

        return outPulledCount > 0 || textures.isEmpty();
    }

    bool RemixConnector::ImportTexturesFromRemix() {
        QSettings settings("InstaMAT2Remix", "Config");

        QString materialPrim = settings.value("LinkedMaterialPrim", "").toString();
        if (materialPrim.isEmpty()) {
            QString selErr;
            if (!GetSelectedMaterialPrim(materialPrim, selErr)) {
                QMessageBox::warning(nullptr, kPluginName, "Import Textures failed:\n\nNo linked material and selection query failed:\n" + selErr);
                return false;
            }
            settings.setValue("LinkedMaterialPrim", materialPrim);
            m_linkedMaterialPrim = materialPrim.toStdString();
        }

        QString remixDirAbs;
        QString dirErr;
        if (!GetRemixDefaultDirectory(remixDirAbs, dirErr)) {
            QMessageBox::warning(nullptr, kPluginName, "Import Textures failed:\n\nCould not determine Remix project directory:\n" + dirErr);
            return false;
        }

        const QString destDir = GetPulledTexturesDir(remixDirAbs);

        const QString encodedMat = UrlEncodeKeepSlashes(NormalizePathSlashes(materialPrim));
        QString apiErr;
        const QJsonDocument doc = RequestJson("GET", QString("/stagecraft/assets/%1/textures").arg(encodedMat), {}, nullptr, &apiErr);
        if (doc.isNull() || !doc.isObject()) {
            QMessageBox::warning(nullptr, kPluginName, "Import Textures failed:\n\n" + apiErr);
            return false;
        }

        const QJsonArray textures = doc.object().value("textures").toArray();
        if (textures.isEmpty()) {
            QMessageBox::information(nullptr, kPluginName, "No textures were returned for the material:\n\n" + materialPrim);
            return false;
        }

        QProgressDialog progress("Importing textures from RTX Remix...", "Cancel", 0, textures.size(), nullptr);
        progress.setWindowTitle(kPluginName);
        progress.setWindowModality(Qt::WindowModal);
        progress.setMinimumDuration(0);
        progress.setValue(0);
        progress.show();
        QCoreApplication::processEvents();

        QVector<ChannelEntry> entries;
        int pulledCount = 0;
        int convertedCount = 0;
        // Canonical names (albedo.png, normal.png, …): unlike WBC — which
        // auto-assigns into Painter channels and keeps original names — the
        // InstaMAT SDK has no channel-assignment API, so the user drags files
        // manually and canonical names tell them where each map goes.
        DownloadAndConvertTextureList(textures, remixDirAbs, destDir,
                                      NamingPolicy::CanonicalChannelOrOriginal,
                                      &progress, entries,
                                      pulledCount, convertedCount);

        // Make folder available as external asset folder for easy picking.
        if (pulledCount > 0) m_instaMAT.RegisterExternalAssetFolder(destDir.toStdString().c_str());

        if (progress.wasCanceled()) {
            QMessageBox::information(nullptr, kPluginName, "Import cancelled.");
            return false;
        }

        if (pulledCount > 0) {
            QDesktopServices::openUrl(QUrl::fromLocalFile(destDir));
        }

        QMessageBox::information(
            nullptr, kPluginName,
            QString("Imported %1 texture(s) from RTX Remix (%2 converted from DDS).\n\n"
                    "Saved to:\n%3\n\n"
                    "The folder is registered in InstaMAT's Asset Browser and was opened "
                    "in Explorer. InstaMAT's plugin SDK has no API to assign textures "
                    "into an open project automatically — drag each map onto the "
                    "matching channel (albedo → Base Color, normal → Normal, ...).")
                .arg(pulledCount)
                .arg(convertedCount)
                .arg(destDir));
        return pulledCount > 0;
    }

    QString RemixConnector::PreIngestStageDir() {
        return QDir::cleanPath(QDir(QDir::tempPath()).filePath("InstaMAT2Remix_PreIngest"));
    }

    QString RemixConnector::DefaultLayerProjectDir() {
        return QDir::cleanPath(QDir::homePath() + "/Documents/InstaMAT/Library");
    }

    bool RemixConnector::FindMostRecentLayerPackageIn(const QString& dirPath,
                                                      QString&       outAbsPath,
                                                      QDateTime&     outMtime,
                                                      QString&       outErr) const {
        outAbsPath.clear();
        outMtime = QDateTime();
        outErr.clear();

        QDir dir(dirPath);
        if (!dir.exists()) {
            outErr = "Layer-project directory does not exist: " + dirPath;
            return false;
        }

        const QFileInfoList files = dir.entryInfoList(
            QStringList() << "*.IMP",
            QDir::Files | QDir::NoDotAndDotDot,
            QDir::Time); // newest first

        for (const QFileInfo& fi : files) {
            if (fi.fileName().compare("InstaMAT2Remix.IMP", Qt::CaseInsensitive) == 0) {
                continue;
            }
            outAbsPath = QDir::cleanPath(fi.absoluteFilePath());
            outMtime = fi.lastModified();
            return true;
        }

        outErr = "No layer-project .IMP files in: " + dirPath;
        return false;
    }

    QString RemixConnector::StageSourceForIngest(const QString& sourcePath, QString& outErr) const {
        return StageSourceForIngest(sourcePath, QFileInfo(sourcePath).fileName(), outErr);
    }

    QString RemixConnector::StageSourceForIngest(const QString& sourcePath,
                                                 const QString& destFileName,
                                                 QString&       outErr) const {
        outErr.clear();

        const QFileInfo srcInfo(sourcePath);
        if (!srcInfo.exists() || !srcInfo.isFile()) {
            outErr = "Source not found: " + sourcePath;
            return QString();
        }

        const QString stageRoot = PreIngestStageDir();
        if (!QDir().mkpath(stageRoot)) {
            outErr = "Failed to create stage dir: " + stageRoot;
            return QString();
        }

        const QString safeName = destFileName.isEmpty() ? srcInfo.fileName() : destFileName;
        const QString staged = QDir::cleanPath(QDir(stageRoot).filePath(safeName));
        // QFile::copy refuses to overwrite, so clear any prior copy first. We
        // intentionally do NOT scrub other files in the dir — PushToRemix wipes
        // the whole dir up-front before staging the current batch.
        if (QFile::exists(staged) && !QFile::remove(staged)) {
            outErr = "Failed to clear stale staged file: " + staged;
            return QString();
        }

        if (!QFile::copy(srcInfo.absoluteFilePath(), staged)) {
            outErr = QString("Failed to copy %1 -> %2")
                         .arg(srcInfo.absoluteFilePath(), staged);
            return QString();
        }

        return staged;
    }

    QString RemixConnector::SanitizeFilenameStem(const QString& stem) {
        // Strip characters Windows filenames can't contain, plus control chars.
        static const QString kIllegal = "\\/:*?\"<>|";
        QString out;
        out.reserve(stem.size());
        for (const QChar c : stem) {
            if (kIllegal.contains(c) || c.unicode() < 0x20) continue;
            out.append(c);
        }
        return out.trimmed();
    }

    QString RemixConnector::DeriveDesiredRootFromPrim(const QString& materialPrim) {
        // WBC's material-hash extraction: the trailing 16 alphanumerics of the
        // material prim path (e.g. /RootNode/Looks/mat_98A2BAA819D189F6).
        static const QRegularExpression re("([A-Za-z0-9]{16})$");
        const auto m = re.match(NormalizePathSlashes(materialPrim));
        return m.hasMatch() ? m.captured(1) : QString();
    }

    bool RemixConnector::ForcePushRootConflicts(const QString& root, const QString& ingestDirAbs) {
        // Boundary-aware conflict scan (port of WBC texture_processor.py
        // _force_push_root_conflicts): a .dds/.rtex.dds file conflicts when its
        // name equals the root or continues it with '.', '_' or '-'. "chair"
        // conflicts with "chair_albedo.rtex.dds" but not "chair2_albedo.dds".
        const QDir dir(ingestDirAbs);
        if (root.isEmpty() || !dir.exists()) return false;

        const QString rootLower = root.toLower();
        const QStringList entries = dir.entryList(QDir::Files);
        for (const QString& entry : entries) {
            const QString lower = entry.toLower();
            if (!lower.endsWith(".dds")) continue; // covers .rtex.dds too
            if (!lower.startsWith(rootLower)) continue;
            if (lower.size() == rootLower.size()) return true;
            const QChar next = lower.at(rootLower.size());
            if (next == '.' || next == '_' || next == '-') return true;
        }
        return false;
    }

    QString RemixConnector::ChooseNonOverwritingRoot(const QString& desiredRoot,
                                                     const QString& ingestDirAbs) {
        QString base = SanitizeFilenameStem(desiredRoot);
        if (base.isEmpty()) base = "ForcePush";
        if (!ForcePushRootConflicts(base, ingestDirAbs)) return base;
        for (int i = 1; i <= 9999; ++i) {
            const QString candidate = QString("%1_%2").arg(base).arg(i);
            if (!ForcePushRootConflicts(candidate, ingestDirAbs)) return candidate;
        }
        return QString("%1_%2").arg(base).arg(QDateTime::currentSecsSinceEpoch());
    }

    bool RemixConnector::IngestTextureToRemix(const QString& pbrType,
                                              const QString& texturePath,
                                              const QString& targetIngestDirAbs,
                                              QString&       outIngested,
                                              QString&       outIngestErr) const {
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
        // Ingest is long-running on RTX Remix's side (DDS conversion + asset
        // staging). WBC uses 600s + 1 attempt for this endpoint; retrying would
        // re-queue a job the server may still be processing. Matches
        // INGEST_REQUEST_TIMEOUT_SECONDS / retries=1 in WBC remix_api.py.
        constexpr double kIngestTimeoutSec = 600.0;
        constexpr int    kIngestMaxAttempts = 1;
        const QJsonDocument resp = RequestJson("POST", "/ingestcraft/mass-validator/queue/material",
                                               {}, &body, &apiErr,
                                               kIngestTimeoutSec, kIngestMaxAttempts);
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
    }

    QString RemixConnector::MeshCacheDirFor(const QString& meshPath) {
        QString docs = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
        if (docs.isEmpty()) docs = QDir::homePath();
        const QString stem = QFileInfo(meshPath).completeBaseName();
        return QDir::cleanPath(QDir(docs).filePath(
            "InstaMAT2Remix/MeshCache/" + (stem.isEmpty() ? QString("mesh") : stem)));
    }

    QSize RemixConnector::QueryOriginalTextureSize() const {
        // Reads the linked material's current texture dimensions straight from
        // the first reachable DDS header. Invalid QSize when unknown.
        const QString materialPrim = QString::fromStdString(m_linkedMaterialPrim);
        if (materialPrim.isEmpty()) return QSize();

        const QString encodedMat = UrlEncodeKeepSlashes(NormalizePathSlashes(materialPrim));
        QString apiErr;
        const QJsonDocument texDoc = RequestJson(
            "GET", QString("/stagecraft/assets/%1/textures").arg(encodedMat),
            {}, nullptr, &apiErr);
        if (!texDoc.isObject()) {
            if (!apiErr.isEmpty())
                m_logger.Debug("QueryOriginalTextureSize: textures query failed: " + apiErr);
            return QSize();
        }

        QString remixDirAbs;
        QString dirErr;
        GetRemixDefaultDirectory(remixDirAbs, dirErr); // best-effort, for relative paths

        const QJsonArray textures = texDoc.object().value("textures").toArray();
        for (const QJsonValue& entry : textures) {
            if (!entry.isArray()) continue;
            const QJsonArray pair = entry.toArray();
            if (pair.size() < 2 || !pair.at(1).isString()) continue;
            QString p = NormalizePathSlashes(pair.at(1).toString());
            if (p.isEmpty() || !p.endsWith(".dds", Qt::CaseInsensitive)) continue;
            if (!QDir::isAbsolutePath(p)) {
                if (remixDirAbs.isEmpty()) continue;
                p = QDir(remixDirAbs).filePath(p);
            }
            p = QDir::cleanPath(p);
            uint32_t w = 0, h = 0;
            if (QFileInfo::exists(p) && ReadDdsDimensions(p, w, h)) {
                m_logger.Info(QString("Original Remix texture size %1x%2 from %3").arg(w).arg(h).arg(p));
                return QSize(static_cast<int>(w), static_cast<int>(h));
            }
        }
        return QSize();
    }

    QSize RemixConnector::ComputePushExportSize() const {
        // Auto (ExportResolution 0) → QSize(0,0): a sentinel telling the export
        // worker to render at the resolution the user BAKED the project at
        // (read from the project's BakeSettings). This is the WBC-equivalent
        // "push what you baked" — NOT the original Remix DDS size (which for a
        // Remix capture can be tiny, e.g. 66x66, and was the cause of the
        // "pushed the original pulled size" bug). A fixed ExportResolution
        // setting still wins, aspect-corrected against the original Remix
        // texture when RestoreAspectOnExport is on.
        QSettings settings("InstaMAT2Remix", "Config");
        const int fixed = settings.value("ExportResolution", 0).toInt();
        const bool restoreAspect = settings.value("RestoreAspectOnExport", true).toBool();

        if (fixed > 0) {
            if (restoreAspect) {
                const QSize orig = QueryOriginalTextureSize();
                if (orig.isValid() && orig.width() > 0 && orig.height() > 0
                    && orig.width() != orig.height()) {
                    QSize scaled;
                    if (orig.width() >= orig.height()) {
                        scaled = QSize(fixed, qMax(1, qRound(double(fixed) * orig.height() / orig.width())));
                    } else {
                        scaled = QSize(qMax(1, qRound(double(fixed) * orig.width() / orig.height())), fixed);
                    }
                    return scaled;
                }
            }
            return QSize(fixed, fixed);
        }

        // Auto: let the worker resolve the baked size.
        return QSize(0, 0);
    }

    // Triggers Studio's File > Save (Ctrl+S) on the live project so Push can
    // pick up the latest paint from disk. The project was auto-created and
    // already has a Library .IMP, so Save re-writes that path silently (no
    // Save-As dialog). Returns false when no Save action is reachable (caller
    // falls back to a manual "press Ctrl+S then Retry" prompt).
    bool RemixConnector::TrySaveActiveProject() {
        QMainWindow* win = FindHostMainWindow();
        if (!win) { m_logger.Warning("Auto-save: no host main window found."); return false; }
        QMenuBar* bar = win->findChild<QMenuBar*>();
        QAction* save = FindBestSaveAction(bar);
        if (!save) { m_logger.Warning("Auto-save: no Save action found in the menu bar."); return false; }
        m_logger.Info(QString("Auto-save: triggering '%1'").arg(NormalizeActionText(save->text())));
        save->trigger();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 500);
        return true;
    }

    bool RemixConnector::ExportActiveLayeringProject(const QString& outDir,
                                                     QStringList&   outChannelFiles,
                                                     QString&       outError) {
        outChannelFiles.clear();
        outError.clear();
        m_exportHadCollapse = false;
        m_lastExportSize.clear();

        if (!QDir().mkpath(outDir)) {
            outError = QString("Could not create export directory: %1").arg(outDir);
            return false;
        }

        // 1. Find the layer-stack graph by loading the newest non-plugin .IMP
        // from Documents/InstaMAT/Library/ via AllocPackageFromFile. Executing
        // on Studio's live in-memory graph (GetGraphByName) collided with the
        // host's viewport render in earlier testing, so a private on-disk copy
        // is the only safe execution source. The on-disk .IMP only reflects
        // what the user last saved; when it is stale we first AUTO-SAVE (drive
        // File > Save), then fall back to a manual prompt if that is unavailable.
        QString libPath;
        QDateTime libMtime;
        bool autoSaveTried = false;
        for (;;) {
            QString findErr;
            if (!FindMostRecentLayerPackageIn(DefaultLayerProjectDir(),
                                              libPath, libMtime, findErr)) {
                outError = "Cannot find a layering project (" + findErr +
                           "). Use 'Pull From Remix' first to create one.";
                return false;
            }

            const qint64 ageSec = libMtime.secsTo(QDateTime::currentDateTime());
            if (ageSec <= 60) {
                m_logger.Info(QString("Loading layer project from Library (%1s old): %2")
                                  .arg(ageSec).arg(libPath));
                break;
            }

            // Stale on disk. Try to auto-save the live project once, then poll
            // for the Library .IMP to refresh before re-evaluating.
            if (!autoSaveTried) {
                autoSaveTried = true;
                if (TrySaveActiveProject()) {
                    m_logger.Info("Auto-save: triggered; waiting for the Library .IMP to refresh...");
                    QElapsedTimer t;
                    t.start();
                    while (t.elapsed() < 8000) {
                        QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
                        QThread::msleep(50);
                        QString p2, e2;
                        QDateTime m2;
                        if (FindMostRecentLayerPackageIn(DefaultLayerProjectDir(), p2, m2, e2)
                            && m2.secsTo(QDateTime::currentDateTime()) <= 60) {
                            break;
                        }
                    }
                    continue; // re-check freshness at the top of the loop
                }
                m_logger.Info("Auto-save unavailable; falling back to the manual save prompt.");
            }

            const auto answer = QMessageBox::warning(
                nullptr, kPluginName,
                QString("Your saved project is %1 seconds old:\n%2\n\n"
                        "Press Ctrl+S in InstaMAT Studio to save your latest paint, "
                        "then click Retry.")
                    .arg(ageSec)
                    .arg(libPath),
                QMessageBox::Retry | QMessageBox::Cancel, QMessageBox::Retry);
            if (answer != QMessageBox::Retry) {
                outError = "Push cancelled: the saved project on disk does not "
                           "include your latest paint. Press Ctrl+S in InstaMAT "
                           "Studio and push again.";
                return false;
            }
            autoSaveTried = false; // let Retry attempt auto-save again too
        }

        // 2. Render OUT OF PROCESS. Executing a layer graph inside Studio's
        // process is fatal on Studio 3.1+: IElementExecution::Execute routes
        // through a host-registered GL-context callback whose QOpenGLContext
        // belongs to Studio's render thread, and Qt fail-fasts ("Cannot make
        // QOpenGLContext current in a different thread" → qFatal →
        // __fastfail) — uncatchable by any SEH guard (WinDbg-confirmed on the
        // 2026-07-06 crash dumps; this was also the likely cause of the
        // historical 3.0-era Execute crashes). InstaMAT2RemixExport.exe hosts
        // the SDK itself (GetInstaMAT + the shared machine license), so
        // execution runs on a thread/GL context the worker owns and a failure
        // can never take Studio down. The worker binds the linked mesh by
        // feeding its raw file bytes: the package's saved pkg:// mesh URL and
        // file:/// URLs do not resolve in a fresh SDK process, and executing
        // with a missing mesh silently yields blank outputs (all three modes
        // headless-verified on a real project, 2026-07-06).
        const QString workerExe = DetectExportWorkerPath();
        if (workerExe.isEmpty()) {
            outError = "InstaMAT2RemixExport.exe was not found next to the plugin. "
                       "Re-run the installer (or build_plugin.ps1) so the export "
                       "worker is deployed alongside texconv.exe.";
            return false;
        }

        const QSize exportSize = ComputePushExportSize();
        QSettings settings("InstaMAT2Remix", "Config");
        QString fileFormat = settings.value("ExportFileFormat", "png").toString().trimmed().toLower();
        if (fileFormat != "png" && fileFormat != "tga" && fileFormat != "jpg") fileFormat = "png";

        QStringList baseArgs;
        baseArgs << "--imp" << libPath
                 << "--out" << outDir
                 << "--format" << fileFormat
                 << "--studio" << QCoreApplication::applicationDirPath();
        const QString meshAbs = QString::fromStdString(m_linkedMeshPath);
        if (!meshAbs.isEmpty() && QFileInfo::exists(meshAbs)) {
            baseArgs << "--mesh" << meshAbs;
        } else {
            m_logger.Warning("ExportActive: linked mesh path missing (" + meshAbs +
                             ") — the worker will fall back to the package's own mesh binding.");
        }

        // One worker invocation at the given size (0,0 = worker resolves the
        // baked size). Fills channelFiles; sets outCollapsed when the worker
        // reported a render race (some channels came out 1x1).
        auto runWorkerOnce = [&](int reqW, int reqH, QStringList& channelFiles,
                                 bool& outCollapsed, QString& outFinalSize,
                                 QString& runError) -> bool {
            channelFiles.clear();
            outCollapsed = false;
            outFinalSize.clear();

            QStringList args = baseArgs;
            args << "--width" << QString::number(reqW) << "--height" << QString::number(reqH);

            m_logger.Info(QString("ExportActive: spawning worker: \"%1\" %2")
                              .arg(workerExe, args.join(' ')));
            QProcess proc;
            proc.setProgram(workerExe);
            proc.setArguments(args);
            proc.setProcessChannelMode(QProcess::MergedChannels);
            proc.start();
            if (!proc.waitForStarted(15000)) {
                runError = "could not start the export worker: " + workerExe;
                return false;
            }

            // Keep Studio painting (user input excluded) while the worker runs;
            // a typical export is ~15-25 s (SDK init + library load + execute).
            QElapsedTimer waitTimer;
            waitTimer.start();
            constexpr qint64 kWorkerTimeoutMs = 10 * 60 * 1000;
            while (proc.state() != QProcess::NotRunning) {
                if (waitTimer.elapsed() > kWorkerTimeoutMs) {
                    proc.kill();
                    proc.waitForFinished(5000);
                    runError = QString("the export worker timed out after %1 s")
                                   .arg(kWorkerTimeoutMs / 1000);
                    return false;
                }
                QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 50);
                proc.waitForFinished(50);
            }

            const QString output = QString::fromUtf8(proc.readAllStandardOutput());
            QString workerError;
            QStringList engineTail;
            for (const QString& rawLine : output.split('\n')) {
                const QString line = rawLine.trimmed();
                if (line.isEmpty()) continue;
                if (!line.startsWith("IM2RX ")) {
                    engineTail << line;
                    while (engineTail.size() > 15) engineTail.removeFirst();
                    continue;
                }
                const QString msg = line.mid(6);
                m_logger.Info("ExportWorker: " + msg);
                if (msg.startsWith("CHANNEL=")) {
                    const QString value = msg.mid(8); // canonical:filename
                    const int colon = value.indexOf(':');
                    if (colon > 0) channelFiles.append(value.mid(colon + 1).trimmed());
                } else if (msg.startsWith("COLLAPSED=")) {
                    outCollapsed = (msg.mid(10).trimmed() == "1");
                } else if (msg.startsWith("FINALSIZE=")) {
                    outFinalSize = msg.mid(10).trimmed();
                } else if (msg.startsWith("ERROR=")) {
                    workerError = msg.mid(6);
                }
            }

            const bool exitedOk =
                (proc.exitStatus() == QProcess::NormalExit && proc.exitCode() == 0);
            if (!exitedOk || channelFiles.isEmpty()) {
                for (const QString& line : engineTail) {
                    m_logger.Warning("ExportWorker(engine): " + line);
                }
                if (workerError.isEmpty()) {
                    workerError = (proc.exitStatus() != QProcess::NormalExit)
                        ? QString("the export worker crashed")
                        : QString("the export worker exited with code %1").arg(proc.exitCode());
                }
                runError = workerError;
                return false;
            }
            return true;
        };

        // The standalone renderer has a nondeterministic race where non-'Normal'
        // channels intermittently come out 1x1 (see ExportWorker.cpp), and this
        // project also appears to have an upper resolution beyond which the
        // collapse is near-certain. Each worker process is an independent draw,
        // so: (1) try the requested/baked size twice (fresh process each), then
        // (2) step the size DOWN via fresh processes — 2048, 1024, 512 — until a
        // clean render. Stepping in a fresh process avoids the in-process
        // backend corruption that same-process size changes caused. We keep the
        // FIRST clean render; if none is clean we keep the last (best-effort)
        // and the caller warns the user. The outDir is wiped between attempts.
        auto parseDim = [](const QString& sz) -> int {
            const int x = sz.indexOf('x');
            return x > 0 ? sz.left(x).toInt() : 0;
        };

        QList<int> sizeLadder;
        // Prefer the user's actual baked/requested size — the collapse odds are
        // roughly size-independent (~30% clean per fresh process on a healthy
        // GPU, measured), so spending draws at the baked size delivers the
        // intended resolution rather than a needlessly smaller one. Step-downs
        // are appended only as a last resort once the baked-size draws are
        // exhausted. Loop is capped at 6 attempts to bound the worst-case push
        // time (each worker is ~13 s); it stops at the first clean render, so a
        // healthy GPU pays only one attempt.
        for (int i = 0; i < 3; ++i) sizeLadder << exportSize.width();

        QString lastError;
        bool lastCollapsed = true;
        QString finalSize;
        bool gotClean = false;
        int steppedFloor = 0; // largest step-down already queued
        for (int attempt = 0; attempt < sizeLadder.size() && attempt < 6; ++attempt) {
            const int reqW = sizeLadder.at(attempt);
            if (QDir(outDir).exists()) QDir(outDir).removeRecursively();
            QDir().mkpath(outDir);

            QStringList channelFiles;
            bool collapsed = false;
            QString runError;
            const bool ok = runWorkerOnce(reqW, reqW, channelFiles, collapsed, finalSize, runError);
            if (!ok) {
                lastError = runError;
                m_logger.Warning(QString("ExportActive: worker attempt %1 failed: %2")
                                     .arg(attempt + 1).arg(runError));
                continue; // transient failure — next ladder entry (same size)
            }
            outChannelFiles = channelFiles;
            lastCollapsed = collapsed;
            if (!collapsed) {
                gotClean = true;
                m_logger.Info(QString("ExportActive: clean render on attempt %1 (size %2).")
                                  .arg(attempt + 1).arg(finalSize.isEmpty() ? "?" : finalSize));
                break;
            }
            m_logger.Warning(QString("ExportActive: attempt %1 had collapsed channels (size %2).")
                                 .arg(attempt + 1).arg(finalSize));

            // Queue step-downs once, based on the resolution actually rendered.
            if (steppedFloor == 0) {
                int base = parseDim(finalSize);
                if (base <= 0) base = 2048;
                for (int s = (base > 2048 ? 2048 : base / 2); s >= 512; s /= 2) {
                    sizeLadder << s;
                }
                steppedFloor = 512;
            }
        }

        if (outChannelFiles.isEmpty()) {
            outError = QString("Live export failed: %1. See the plugin log for details: %2")
                           .arg(lastError.isEmpty() ? "no channels were produced" : lastError,
                                GetLogFilePath());
            return false;
        }

        // Belt: only report channel files that actually exist on disk.
        QStringList verified;
        for (const QString& f : outChannelFiles) {
            if (QFileInfo::exists(QDir(outDir).filePath(f))) {
                verified.append(f);
            } else {
                m_logger.Warning("ExportWorker reported a channel file that is missing: " + f);
            }
        }
        outChannelFiles = verified;
        if (outChannelFiles.isEmpty()) {
            outError = "The export worker reported success but no channel files "
                       "were found in " + outDir;
            return false;
        }

        // Not a hard failure (the good channels + Normal are worth pushing), but
        // surface it so the caller can warn the user in the summary.
        m_exportHadCollapse = (!gotClean && lastCollapsed);
        m_lastExportSize = finalSize;
        if (m_exportHadCollapse) {
            m_logger.Warning("ExportActive: some channels still rendered at 1x1 after all "
                             "size attempts (GPU/driver race — worse under GPU contention).");
        }
        return true;
    }

    void RemixConnector::PushToRemix(bool forceRelinkAndRename) {
        QSettings settings("InstaMAT2Remix", "Config");

        // 1. Resolve the target material. A normal push targets the material
        //    linked at Pull time; Force Push silently relinks to the current
        //    Remix selection (WBC's relink-and-push) and persists the new link.
        QString materialPrim = settings.value("LinkedMaterialPrim", "").toString();
        if (forceRelinkAndRename) {
            QString selErr;
            QString currentSel;
            if (!GetSelectedMaterialPrim(currentSel, selErr) || currentSel.isEmpty()) {
                QMessageBox::warning(nullptr, kPluginName,
                    "Force Push failed:\n\nCould not get the current material selection "
                    "from RTX Remix:\n" + selErr);
                return;
            }
            materialPrim = currentSel;
            settings.setValue("LinkedMaterialPrim", materialPrim);
            m_logger.Info("Force Push: relinked to " + materialPrim);
        }
        if (materialPrim.isEmpty()) {
            QMessageBox::warning(nullptr, kPluginName,
                "Push failed:\n\nNo linked material.\n\nUse 'Pull From Remix' first "
                "(or 'Force Push to Remix' to link to the current Remix selection).");
            return;
        }
        m_linkedMaterialPrim = materialPrim.toStdString();
        m_linkedMeshPath = settings.value("LinkedMeshPath", "").toString().toStdString();

        // 2. Resolve Remix project directory (for the ingest destination).
        QString remixDirAbs;
        QString dirErr;
        if (!GetRemixDefaultDirectory(remixDirAbs, dirErr)) {
            QMessageBox::warning(nullptr, kPluginName,
                "Push failed:\n\nCould not determine Remix project directory:\n" + dirErr);
            return;
        }

        // 3. Live export of the painted project into the (wiped) export folder.
        //    Mirrors WBC's export_project_textures step. There is NO fallback:
        //    if the export fails, the push fails cleanly with an actionable
        //    reason and nothing is ingested (WBC behavior).
        const QString exportDir = settings.value(
            "ExportFolder", QDir::cleanPath(QDir::tempPath() + "/InstaMAT2Remix_Export")).toString();
        if (QDir(exportDir).exists()) {
            QDir(exportDir).removeRecursively();
        }

        QStringList exportedFilesList;
        QString exportErr;
        if (!ExportActiveLayeringProject(exportDir, exportedFilesList, exportErr)) {
            m_logger.Warning("Push failed at export: " + exportErr);
            QMessageBox::warning(nullptr, kPluginName, "Push failed:\n\n" + exportErr);
            return;
        }
        m_logger.Info(QString("Exported %1 channel(s) from the layer project.")
                          .arg(exportedFilesList.size()));

        // 4. Build pbrType -> absolute file path from the exported stems and
        //    apply the opacity gate (WBC's include_opacity_map).
        QHash<QString, QString> exportedFiles;
        for (const QString& filename : exportedFilesList) {
            const QString stem = QFileInfo(filename).completeBaseName().toLower(); // "albedo"
            const QString abs = QDir::cleanPath(QDir(exportDir).filePath(filename));
            if (QFileInfo::exists(abs)) exportedFiles.insert(stem, abs);
        }
        if (!settings.value("IncludeOpacityMap", false).toBool()) {
            exportedFiles.remove("opacity");
        }

        if (exportedFiles.isEmpty()) {
            QMessageBox::warning(nullptr, kPluginName,
                QString("Push failed:\n\nThe export produced no pushable channel files in:\n  %1")
                    .arg(exportDir));
            return;
        }

        // 4b. COLLAPSE GUARD — never overwrite a good Remix texture with a 1x1.
        //     The standalone GPU renderer intermittently emits 1x1 (blank) output
        //     for the color channels while 'Normal' renders full; this happens
        //     under GPU contention (a running game, the RTX Remix viewport, or a
        //     driver state degraded by rapid renders). Ingesting those would
        //     replace the user's painted textures in Remix with a single pixel.
        //     If the worker flagged a collapse OR any non-height channel is 1x1
        //     on disk (also catches the all-channels-collapsed case the worker's
        //     relative check can't), abort the whole push — atomic and
        //     non-destructive — rather than push best-effort garbage.
        QStringList collapsedChannels;
        for (auto it = exportedFiles.constBegin(); it != exportedFiles.constEnd(); ++it) {
            if (it.key() == "height") continue; // height is legitimately flat/1x1
            const QSize dim = QImageReader(it.value()).size();
            if (dim.isValid() && (dim.width() <= 1 || dim.height() <= 1)) {
                collapsedChannels << it.key();
            }
        }
        if (m_exportHadCollapse || !collapsedChannels.isEmpty()) {
            const QString chans = collapsedChannels.isEmpty()
                ? QStringLiteral("one or more channels")
                : collapsedChannels.join(", ");
            m_logger.Warning("Push aborted (collapse guard): 1x1 channel(s) [" + chans +
                             "] at size " + m_lastExportSize +
                             " — Remix material left unchanged.");
            QMessageBox::warning(nullptr, kPluginName,
                "Push stopped to protect your Remix textures.\n\n"
                "The renderer produced blank 1x1 output for: " + chans + ".\n"
                "This happens when the GPU is busy with another graphics-heavy app "
                "(a running game, the RTX Remix viewport, or a driver state left "
                "degraded by earlier renders).\n\n"
                "Nothing in Remix was changed. To get a clean push:\n"
                "  1. Close other GPU-heavy apps (games, other 3D tools).\n"
                "  2. Push To Remix again.\n"
                "  3. If it still collapses, restart your PC to reset the GPU "
                "driver, then Push.\n\n"
                "Details in the log:\n  " + GetLogFilePath());
            return;
        }

        // 5. Stage every exported file into %TEMP%/InstaMAT2Remix_PreIngest/
        //    under a material-hash filename root (<hash>_<pbr>.<ext> — WBC's
        //    on-disk naming; also busts Remix's source-filename cache). Force
        //    Push picks a root that collides with nothing already ingested.
        //    Staging in a transient dir is required: Remix's TextureImporter
        //    only copies the source next to its DDS when it is temp-rooted.
        const QString outputSubfolder = settings.value("RemixOutputSubfolder",
                                                       "Textures/InstaMAT2Remix_Ingested").toString();
        const QString targetIngestDirAbs = QDir(remixDirAbs).filePath(outputSubfolder);
        QDir().mkpath(targetIngestDirAbs);

        QString fileRoot = DeriveDesiredRootFromPrim(materialPrim);
        if (fileRoot.isEmpty()) fileRoot = "instamat";
        if (forceRelinkAndRename) {
            const QString chosen = ChooseNonOverwritingRoot(fileRoot, targetIngestDirAbs);
            if (chosen != fileRoot) {
                m_logger.Info(QString("Force Push: using non-overwriting root '%1' (desired '%2').")
                                  .arg(chosen, fileRoot));
            }
            fileRoot = chosen;
        }

        const QString stageRoot = PreIngestStageDir();
        if (QDir(stageRoot).exists()) {
            QDir(stageRoot).removeRecursively();
        }
        QHash<QString, QString> stagedFiles;
        for (auto it = exportedFiles.constBegin(); it != exportedFiles.constEnd(); ++it) {
            const QString destName = QString("%1_%2.%3")
                                         .arg(fileRoot, it.key(), QFileInfo(it.value()).suffix());
            QString stageErr;
            const QString staged = StageSourceForIngest(it.value(), destName, stageErr);
            if (staged.isEmpty()) {
                m_logger.Warning(QString("Pre-ingest stage failed for %1: %2 — falling back to original path %3")
                                     .arg(it.key(), stageErr, it.value()));
                stagedFiles.insert(it.key(), it.value());
            } else {
                stagedFiles.insert(it.key(), staged);
            }
        }
        m_logger.Info(QString("Staged %1 source texture(s) into %2 for ingest (root '%3').")
                          .arg(stagedFiles.size()).arg(stageRoot, fileRoot));

        // 6. Ingest each texture into the Remix project.
        QProgressDialog progress("Pushing textures to RTX Remix...", "Cancel", 0, stagedFiles.size(), nullptr);
        progress.setWindowTitle(kPluginName);
        progress.setWindowModality(Qt::WindowModal);
        progress.setMinimumDuration(0);
        progress.show();
        QCoreApplication::processEvents();

        QHash<QString, QString> ingestedPaths;
        QStringList ingestErrors;
        int ingestIdx = 0;
        for (auto it = stagedFiles.begin(); it != stagedFiles.end(); ++it) {
            if (progress.wasCanceled()) break;
            progress.setLabelText(QString("Ingesting %1...").arg(it.key()));
            progress.setValue(ingestIdx++);
            QCoreApplication::processEvents();

            QString ingested;
            QString ingestErr;
            if (IngestTextureToRemix(it.key(), it.value(), targetIngestDirAbs, ingested, ingestErr)) {
                ingestedPaths.insert(it.key(), ingested);
            } else {
                ingestErrors << QString("%1: %2").arg(it.key(), ingestErr);
            }
        }

        if (progress.wasCanceled()) {
            QMessageBox::information(nullptr, kPluginName, "Push cancelled.");
            return;
        }
        if (ingestedPaths.isEmpty()) {
            QMessageBox::warning(nullptr, kPluginName,
                "Push failed:\n\nIngestion failed for all textures:\n" + ingestErrors.join("\n"));
            return;
        }

        // 7. Batch-update the Remix material's texture inputs. Use kDefaultPbrSpecs
        //    to map pbrType -> mdlInput (Shader USD attribute name).
        QJsonArray texturePairs;
        for (const auto& spec : kDefaultPbrSpecs) {
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
            QMessageBox::warning(nullptr, kPluginName,
                "Push failed:\n\nTexture update failed:\n" + updateErr);
            return;
        }

        // 8. Save the current layer (best-effort).
        QString layerId;
        {
            QString a1, a2;
            const QJsonDocument d1 = RequestJson("GET", "/stagecraft/layers/target", {}, nullptr, &a1);
            if (d1.isObject()) layerId = d1.object().value("layer_id").toString();
            if (layerId.isEmpty()) {
                const QJsonDocument d2 = RequestJson("GET", "/stagecraft/project/", {}, nullptr, &a2);
                if (d2.isObject()) layerId = d2.object().value("layer_id").toString();
            }
            if (layerId.isEmpty()) {
                qWarning() << "[InstaMAT2Remix] Could not determine layer to save:" << a1 << a2;
            }
        }
        if (!layerId.isEmpty()) {
            const QString encLayer = UrlEncodeKeepColonAndSlashes(NormalizePathSlashes(layerId));
            QString saveErr;
            const QJsonDocument saveResp = RequestJson("POST",
                                                       QString("/stagecraft/layers/%1/save").arg(encLayer),
                                                       {}, nullptr, &saveErr);
            if (saveResp.isNull()) qWarning() << "[InstaMAT2Remix] Layer save failed:" << saveErr;
        }

        // 9. Summary.
        QString summary = QString("Push complete.\n\nUpdated %1 texture(s) on:\n  %2")
                              .arg(ingestedPaths.size())
                              .arg(materialPrim);
        summary += QString("\n\nExported %1 channel(s) from your project (file root '%2')%3.")
                       .arg(exportedFiles.size())
                       .arg(fileRoot)
                       .arg(m_lastExportSize.isEmpty() ? QString()
                                                       : QString(" at %1").arg(m_lastExportSize));
        if (m_exportHadCollapse) {
            summary += "\n\nNote: some channels rendered at 1x1. This can happen when "
                       "the GPU is busy with other graphics-heavy applications during "
                       "export. Close those apps and Push again for a clean render.";
        }
        if (!ingestErrors.isEmpty()) {
            summary += "\n\nIngest warnings:\n" + ingestErrors.join("\n");
        }
        QMessageBox::information(nullptr, kPluginName, summary);
    }
}

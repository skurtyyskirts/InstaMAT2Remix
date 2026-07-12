#include "RemixConnector.h"
#include "MeshData.h"
#include "PbrChannelMap.h"
#include "PluginInfo.h"
#include "PluginPaths.h"
#include "ProjectTemplates.h"

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
#include <QImage>
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

        // Canonical PBR channel -> AperturePBR shader input, per material kind.
        // Ground truth: dxvk-remix AperturePBR_Opacity.mdl /
        // AperturePBR_Translucent.mdl (2026-07-12 research). An empty input
        // means that material kind has NO consumed texture input for the
        // channel — the push skips it (with a summary note) instead of
        // authoring a dead attribute. There is deliberately NO row for
        // opacity (Remix reads opacity from the diffuse texture's ALPHA — the
        // push merges opacity.png into albedo's alpha), nor for ao/ior
        // (AperturePBR has no such texture inputs).
        struct PbrSpec {
            QString pbrType;
            QString mdlInputOpaque;
            QString mdlInputTranslucent;
        };

        const QList<PbrSpec> kDefaultPbrSpecs = {
            {"albedo",         "diffuse_texture",                      ""},
            {"normal",         "normalmap_texture",                    "normalmap_texture"},
            {"roughness",      "reflectionroughness_texture",          ""},
            {"metallic",       "metallic_texture",                     ""},
            {"emissive",       "emissive_mask_texture",                "emissive_mask_texture"},
            {"height",         "height_texture",                       ""},
            // "transmittance" is dual-routed: opaque materials take it as the
            // SSS transmittance color; translucent (glass) materials as the
            // transmittance/albedo color.
            {"transmittance",  "subsurface_transmittance_texture",     "transmittance_texture"},
            {"sss_thickness",  "subsurface_thickness_texture",         ""},
            // NOTE: the texture input is ..._single_scattering_texture — the
            // "_albedo" spelling only exists for the scalar constant.
            {"sss_scattering", "subsurface_single_scattering_texture", ""},
            {"sss_radius",     "subsurface_radius_texture",            ""},
            // Valid input + ingest type, but the runtime currently reads the
            // scalar anisotropy constant; pushed anyway with a summary caveat.
            {"anisotropy",     "anisotropy_texture",                   ""},
        };

        // NOTE: the Studio-output-name → canonical-PBR-channel mapping
        // ("Base Color" → albedo, …) lives in PbrChannelMap.h
        // (MapStudioOutputToCanonicalPbr), shared with the out-of-process
        // exporter — the only place graph outputs are walked.

        // Canonical channel -> RTX Remix ingest TextureTypes enum NAME. The
        // mass-validator accepts exactly these 13 names (toolkit-remix
        // omni.flux.asset_importer.core data_models/enums.py): DIFFUSE,
        // ROUGHNESS, ANISOTROPY, METALLIC, EMISSIVE, NORMAL_OGL, NORMAL_DX,
        // NORMAL_OTH, HEIGHT, TRANSMITTANCE, MEASUREMENT_DISTANCE,
        // SINGLE_SCATTERING, OTHER. (The old AO/OPACITY/IOR/SUBSURFACE
        // strings inherited from WBC were never valid.) "normal" is resolved
        // dynamically by ResolveIngestValidationType from the
        // NormalMapEncoding setting.
        const QHash<QString, QString> kPbrToIngestValidation = {
            {"albedo",         "DIFFUSE"},
            {"height",         "HEIGHT"},
            {"roughness",      "ROUGHNESS"},
            {"metallic",       "METALLIC"},
            {"emissive",       "EMISSIVE"},
            {"transmittance",  "TRANSMITTANCE"},
            {"sss_thickness",  "MEASUREMENT_DISTANCE"},
            {"sss_scattering", "SINGLE_SCATTERING"},
            {"sss_radius",     "OTHER"},
            {"anisotropy",     "ANISOTROPY"},
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
            // --- Remix SSS / translucency extension (deliberate deviation
            // from the WBC lock-step table above; suffixes verified against
            // dxvk-remix's AperturePBR MDLs 2026-07-12) ---
            {"transmittance_texture",              "transmittance"},
            {"subsurface_transmittance_texture",   "transmittance"},
            {"subsurface_thickness_texture",       "sss_thickness"},
            {"subsurface_single_scattering_texture","sss_scattering"},
            {"subsurface_radius_texture",          "sss_radius"},
            {"anisotropy_texture",                 "anisotropy"},
        };

        QString NormalizeSpacesUnderscoresLower(QString s) {
            s = s.toLower();
            s.replace(' ', "");
            s.replace('_', "");
            return s;
        }

        // Maps a Remix *instance* prim path to its mesh *definition* prim
        // (/instances/inst_<HASH> -> /meshes/mesh_<HASH>), which is what the
        // /material and /file-paths endpoints answer for. Shared by Pull and
        // by the material-from-selection fallback (Import / Force Push).
        QString ExtractMeshDefinitionPath(const QString& primPath) {
            const QString p = QString(primPath).replace('\\', '/');
            static const QRegularExpression re1(
                R"(^(.*)/instances/inst_([A-Z0-9]{16}(?:_[0-9]+)?)(?:_[0-9]+)?(?:/.*)?$)");
            const auto m1 = re1.match(p);
            if (m1.hasMatch())
                return QString("%1/meshes/mesh_%2").arg(m1.captured(1), m1.captured(2));

            static const QRegularExpression re2(
                R"(^(.*(?:/meshes|/Mesh|/Geom)/mesh_[A-Z0-9]{16}(?:_[0-9]+)?)(?:/.*)?$)");
            const auto m2 = re2.match(p);
            if (m2.hasMatch()) return m2.captured(1);

            return {};
        }

        // RAII busy-flag for the reentrancy guard.
        struct BusyGuard {
            bool& flag;
            explicit BusyGuard(bool& f) : flag(f) { flag = true; }
            ~BusyGuard() { flag = false; }
        };

        // True when a second menu action arrived while a flow is running;
        // shows the notice and tells the caller to bail.
        bool WarnIfBusy(bool busy) {
            if (!busy) return false;
            QMessageBox::information(nullptr, kPluginName,
                "Another RTX Remix Connector operation is still running.\n\n"
                "Wait for it to finish, then try again.");
            return true;
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
            // Match in strict precedence passes: exact filename, exact stem,
            // then substring. A single-pass per-item check let an early item
            // that merely CONTAINS the stem ("chair_old") win over a later
            // exact match ("chair") and silently picked the wrong mesh.
            QListWidgetItem* hit = nullptr;
            int hitRow = -1;
            auto findBy = [&](const std::function<bool(const QString&)>& pred) {
                for (int i = 0; i < list->count(); ++i) {
                    QListWidgetItem* it = list->item(i);
                    if (it && pred(it->text())) { hit = it; hitRow = i; return true; }
                }
                return false;
            };
            findBy([&](const QString& t) {
                return t.compare(filenameWithExt, Qt::CaseInsensitive) == 0;
            })
            || (!filenameNoExt.isEmpty() && findBy([&](const QString& t) {
                    return t.compare(filenameNoExt, Qt::CaseInsensitive) == 0;
                }))
            || (!filenameNoExt.isEmpty() && findBy([&](const QString& t) {
                    return t.contains(filenameNoExt, Qt::CaseInsensitive);
                }));
            if (hit) {
                // QListWidget::itemActivated is a protected signal — emit via meta call.
                const bool ok = QMetaObject::invokeMethod(
                    list, "itemActivated", Qt::DirectConnection,
                    Q_ARG(QListWidgetItem*, hit));
                qInfo().noquote() << "[InstaMAT2Remix] Asset list match: row" << hitRow
                    << "text='" << hit->text() << "' invoked itemActivated ->" << ok;
                return ok;
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
        // names supplied by the InstaMAT engineering team) end-to-end, for the
        // template described by cfg:
        //   1. Click the template's IMProjectTypeSelectionButton (matched by
        //      MatchProjectTypeTile: exact title first, guarded contains second).
        //   2. (picker templates) Open the asset PickButton -> deferred work runs
        //      inside its modal QMenu; select targetAssetAbs (mesh or image) in
        //      the asset popup by filename.
        //      (pickerless templates, e.g. Element Graph) skip straight to 3.
        //   3. Optionally force Up Axis = Z, then click Create -> dialog closes,
        //      project loads.
        // On any failure: logs the cause and calls CloseAnyVisibleDialog so the
        // outer QDialog::exec() unblocks (no stuck modal).
        // ---------------------------------------------------------------------------
        bool RunNewProjectRecipe(QWidget* dialogRoot,
                                 const TemplateRecipeConfig& cfg,
                                 const QString& targetAssetAbs,
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
            log(QString("Recipe START dialog=%1 name='%2' template=%3 target=%4")
                .arg(dialogRoot->metaObject()->className(),
                     dialogRoot->objectName(),
                     QString::fromUtf8(cfg.displayName),
                     targetAssetAbs.isEmpty() ? QStringLiteral("(none)") : targetAssetAbs));

            QPointer<QWidget> dialogGuard(dialogRoot);
            const QFileInfo targetInfo(targetAssetAbs);
            const QString targetFileName = targetInfo.fileName();
            const QString targetBaseName = targetInfo.completeBaseName();

            // Step 1: pick the template's IMProjectTypeSelectionButton.
            // The buttons have empty text/tooltip/accessibleName at the
            // QAbstractButton level (confirmed by Step 8 diagnostic), so the
            // label lives in either:
            //   - a child QLabel,
            //   - a child widget's accessibleName,
            //   - a dynamic QObject property, or
            //   - a static meta-property (likely a name-like one).
            // All four feed MatchProjectTypeTile (exact-title pass first, then
            // a guarded contains pass — see ProjectTemplates.cpp). Falling back
            // to typeButtons.first() historically picked the Layering tile, so
            // only Asset Texturing (where any mesh project still works) allows
            // it; other templates fail hard with the introspection dump.
            const QList<QObject*> typeButtons = FindAllDescendantsByClassName(
                dialogRoot, "InstaMAT::UI::IMProjectTypeSelectionButton");
            log(QString("Recipe Step1: project type buttons found = %1").arg(typeButtons.size()));
            if (typeButtons.isEmpty())
                return fail("No IMProjectTypeSelectionButton found");

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

            QList<QStringList> perButtonStrings;
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
                // Property entries are "name=value" — match on the value part.
                QStringList combined = labels + childAcc;
                for (const QString& kv : dynProps + staticProps)
                    combined << kv.section('=', 1);
                perButtonStrings.append(combined);
            }

            QObject* templateBtn = nullptr;
            const int tileIdx = MatchProjectTypeTile(perButtonStrings, cfg);
            if (tileIdx >= 0) {
                templateBtn = typeButtons.at(tileIdx);
                log(QString("Recipe Step1: matched '%1' tile at index %2 via "
                            "introspection.")
                        .arg(QString::fromUtf8(cfg.displayName)).arg(tileIdx));
            } else if (cfg.allowFirstTileFallback) {
                log(QString("Recipe Step1: no '%1' tile match — falling back to "
                            "typeButtons.first(); inspect dump above to find the "
                            "correct identifier.")
                        .arg(QString::fromUtf8(cfg.displayName)));
                // The first tile has been observed to be the Material Layering
                // project type on some Studio versions. The workflow still
                // completes, but the caller surfaces a note in the Pull summary.
                if (outProjectTypeUncertain) *outProjectTypeUncertain = true;
                templateBtn = typeButtons.first();
            } else {
                return fail(QString("Could not identify the '%1' project type tile "
                                    "(no exact or unambiguous match — see the "
                                    "per-button dump above).")
                                .arg(QString::fromUtf8(cfg.displayName)));
            }

            if (!ClickButtonViaQAbstractButton(templateBtn, "ProjectTemplateTileBtn"))
                return fail(QString("Could not click the '%1' project type button")
                                .arg(QString::fromUtf8(cfg.displayName)));

            // Step 2 (picker templates only): locate the inline asset picker
            // widget. The dialog needs a moment to swap to the project-config
            // view after the type click, so poll up to 5 s. The picker is an
            // InstaMAT::UI::IMGraphObjectPickerGroupWidget whose objectName is
            // "WIDGET_Mesh<index>" / "WIDGET_Image<index>" (one ILGroupWidget
            // per form field). The Materialize prefix is unverified until the
            // first live run, so discovery is tiered — preferred prefix
            // (visible, then hidden), then any WIDGET_* picker — and every
            // candidate is dumped so a wrong guess is diagnosable from the log.
            QToolButton* pickButton = nullptr;
            if (cfg.hasPickerStep) {
                const QString preferredPrefix = QString::fromUtf8(cfg.pickerPreferredPrefix);
                auto findAssetPicker = [&preferredPrefix](QObject* root) -> QObject* {
                    if (!root) return nullptr;
                    QObject* preferredHidden = nullptr;
                    QObject* anyVisible = nullptr;
                    QObject* anyHidden = nullptr;
                    const auto all = root->findChildren<QObject*>();
                    for (QObject* o : all) {
                        if (!o) continue;
                        if (qstrcmp(o->metaObject()->className(),
                                    "InstaMAT::UI::IMGraphObjectPickerGroupWidget") != 0)
                            continue;
                        const QString name = o->objectName();
                        if (!name.startsWith(QStringLiteral("WIDGET_"))) continue;
                        auto* w = qobject_cast<QWidget*>(o);
                        const bool visible = w && w->isVisible();
                        const bool preferred = !preferredPrefix.isEmpty()
                                               && name.startsWith(preferredPrefix);
                        if (preferred && visible) return o;
                        if (preferred && !preferredHidden) preferredHidden = o;
                        if (!preferred && visible && !anyVisible) anyVisible = o;
                        if (!preferred && !visible && !anyHidden) anyHidden = o;
                    }
                    if (preferredHidden) return preferredHidden;
                    if (anyVisible) return anyVisible;
                    return anyHidden;
                };
                QObject* pickerFrame = nullptr;
                const qint64 step2Deadline = QDateTime::currentMSecsSinceEpoch() + 5000;
                while (QDateTime::currentMSecsSinceEpoch() < step2Deadline) {
                    if (!dialogGuard) return fail("Dialog disappeared during Step 2 wait");
                    pickerFrame = findAssetPicker(dialogGuard);
                    if (pickerFrame) break;
                    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
                }
                // One-shot candidate dump (prefix discovery for new templates).
                if (dialogGuard) {
                    for (QObject* o : dialogGuard->findChildren<QObject*>()) {
                        if (!o || qstrcmp(o->metaObject()->className(),
                                          "InstaMAT::UI::IMGraphObjectPickerGroupWidget") != 0)
                            continue;
                        auto* w = qobject_cast<QWidget*>(o);
                        logDebug(QString("  picker candidate: name='%1'%2")
                                     .arg(o->objectName(),
                                          (w && w->isVisible()) ? " [vis]" : " [hid]"));
                    }
                }
                log(QString("Recipe Step2: pickerFrame = %1 name='%2' (wanted prefix '%3')")
                    .arg(pickerFrame ? pickerFrame->metaObject()->className() : QStringLiteral("null"),
                         pickerFrame ? pickerFrame->objectName() : QString(),
                         preferredPrefix));

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
                    return fail("Asset picker frame not found");
                }

                pickButton = pickerFrame->findChild<QToolButton*>(
                    "PickButton", Qt::FindChildrenRecursively);
                if (!pickButton) return fail("PickButton (QToolButton) not found in picker frame");
            }

            // Steps 3-9 deferred: for picker templates pickButton->click() below
            // enters QMenu::exec(), so the rest of the recipe is scheduled as a
            // queued event that fires inside that nested loop while the menu is
            // open. Pickerless templates (Element Graph) schedule the same
            // callback — it skips straight to the Create stage — and the outer
            // wait loop below pumps until it completes.
            auto stepResult = std::make_shared<std::pair<bool, QString>>(false, QString());
            const bool doPickerSteps = cfg.hasPickerStep;
            const bool doUpAxis = cfg.setUpAxis;

            QTimer::singleShot(0, dialogRoot, [stepResult, diagLines, log, logDebug,
                                               dialogGuard, doPickerSteps, doUpAxis,
                                               targetFileName, targetBaseName]() {
                auto setErr = [&](const QString& msg) {
                    diagLines->append("FAIL: " + msg);
                    stepResult->second = msg;
                    qWarning().noquote() << "[InstaMAT2Remix] Recipe (deferred) FAILED:" << msg;
                };

                if (doPickerSteps) {
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
                // The popup is host-owned and can be destroyed while we pump
                // events below — track it through a QPointer, never raw.
                QPointer<QWidget> popupGuard(assetPopup);
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

                // Studio fills the picker list from an ASYNC asset scan, so a
                // just-downloaded image (Materialize source) may not be listed
                // on the first look — observed live 2026-07-12 ("Asset
                // '<hash>_materialize.png' not found in asset list", fine on a
                // manual re-pull). Re-scan a few times before failing; the
                // popup can be destroyed while we pump, so every round goes
                // through popupGuard and re-finds the QListWidget.
                bool activated = false;
                bool listEverFound = false;
                for (int scan = 0; scan < 5 && !activated; ++scan) {
                    if (scan > 0) {
                        log(QString("Recipe Step7: asset '%1' not listed yet — waiting for "
                                    "the asset scan (retry %2/4)")
                                .arg(targetFileName).arg(scan));
                        PumpEventsFor(750);
                    }
                    if (!popupGuard) {
                        setErr("Asset popup closed while waiting for the asset list");
                        return;
                    }
                    QListWidget* list = popupGuard->findChild<QListWidget*>(
                        QString(), Qt::FindChildrenRecursively);
                    if (!list) continue;
                    listEverFound = true;
                    log(QString("Recipe Step7: list count = %1").arg(list->count()));
                    activated = ActivateListWidgetItemByText(list, targetFileName, targetBaseName);
                }
                if (!activated) {
                    setErr(listEverFound
                               ? QString("Asset '%1' not found in asset list").arg(targetFileName)
                               : QString("QListWidget not found in asset popup"));
                    if (popupGuard) popupGuard->close();
                    return;
                }

                const qint64 popupDeadline = QDateTime::currentMSecsSinceEpoch() + 3000;
                while (QDateTime::currentMSecsSinceEpoch() < popupDeadline) {
                    if (!popupGuard || !popupGuard->isVisible()) break;
                    QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
                }
                } // end doPickerSteps

                if (!dialogGuard) { setErr("Dialog vanished before template button click"); return; }

                // Step 7.5: Force the wizard's "Up Axis" combo box to Z (mesh
                // imports only — image/pickerless templates have no such field).
                // RTX Remix captures are Z-up by convention; leaving the wizard
                // at its default (typically Y) imports them sideways and breaks
                // downstream baking. We locate the field by its objectName
                // ("WIDGET_Up Axis4" in the current SDK; the trailing digit is
                // the form index, so we tolerate any suffix) and pick the
                // item whose visible text starts with "Z".
                if (doUpAxis) {
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

            if (pickButton) {
                log("Recipe Step3: clicking PickButton (enters QMenu::exec)");
                pickButton->click();
                log("Recipe: PickButton click() returned (menu closed)");
            } else {
                log("Recipe Step3: template has no asset picker — the deferred "
                    "callback goes straight to the Create stage.");
            }

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
        // Orchestrator: Attempts to automatically create a project of the given
        // template with the given target asset (mesh or image; empty for
        // pickerless templates) by driving InstaMAT's New Project dialog via
        // the class names provided by the InstaMAT engineering team.
        //
        // Two cases:
        //   1) The new-project dialog is already visible (e.g. start screen) ->
        //      run the recipe directly on it.
        //   2) Need to trigger File -> New, which opens a blocking QDialog::exec().
        //      Schedule the recipe via QTimer to fire inside that nested event loop.
        // ---------------------------------------------------------------------------
        bool TryCreateProjectFromTemplate(const TemplateRecipeConfig& cfg,
                                          const QString& targetAssetAbs,
                                          QString* outError,
                                          bool* outProjectTypeUncertain,
                                          const std::function<void(const QString&)>& fileLog) {
            if (outError) outError->clear();
            if (outProjectTypeUncertain) *outProjectTypeUncertain = false;

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
                return RunNewProjectRecipe(existing, cfg, targetAssetAbs, outError,
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
            const QString targetAsset = targetAssetAbs;
            QTimer::singleShot(500, win, [state, win, &cfg, targetAsset, deferredFileLog]() {
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

                state->success = RunNewProjectRecipe(recipeDlg, cfg, targetAsset, &state->error,
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
                             "DDS->PNG conversion will fail, so 'Import Textures from "
                             "Remix' cannot convert Remix's DDS textures. Set "
                             "'texconv.exe' in Settings > Paths to fix.");
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
        // Deliberately short (5 s, single attempt): this runs synchronously on
        // the UI thread from the Settings dialog and Diagnostics, so it must
        // never sit through the user's full PollTimeoutSec x 3-retry budget.
        QString err;
        const QJsonDocument doc = RequestJson(
            "GET", "/stagecraft/assets/default-directory", {}, nullptr, &err,
            /*timeoutSecOverride=*/5.0, /*maxAttemptsOverride=*/1);
        if (doc.isObject()) {
            const QJsonObject data = doc.object();
            const QString dirRaw = data.value("directory_path").toString(
                data.value("asset_path").toString());
            if (!dirRaw.isEmpty()) {
                outMessage = QString("Remix default directory: %1")
                                 .arg(QDir::cleanPath(QFileInfo(dirRaw).absoluteFilePath()));
                return true;
            }
            outMessage = "Connected, but RTX Remix returned no project directory. "
                         "Open a project in the RTX Remix Toolkit.";
            return false;
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
                            return r > 0 ? QString::number(r) : QString("Auto (project's baked resolution)");
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
                // Pump (user input excluded) instead of msleep: this runs on
                // the UI thread and a blind sleep freezes Studio for the
                // whole 2-4 s backoff.
                const qint64 backoffEnd = QDateTime::currentMSecsSinceEpoch() + retryDelaysMs[delayIdx];
                while (QDateTime::currentMSecsSinceEpoch() < backoffEnd) {
                    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 25);
                    QThread::msleep(10);
                }
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
                // setTransferTimeout() aborts the reply with
                // OperationCanceledError, so a plain timeout surfaces here
                // (not via the manual timer above). Report it as what it is
                // and keep it retriable.
                const bool isTimeout = (errCode == QNetworkReply::OperationCanceledError ||
                                        errCode == QNetworkReply::TimeoutError);
                if (isTimeout) {
                    lastError = QString("RTX Remix API request timed out after %1 s.")
                                    .arg(resolvedTimeoutSec);
                } else {
                    QString details = reply->errorString();
                    if (!bytes.isEmpty()) details += " | " + QString::fromUtf8(bytes.left(600));
                    lastError = QString("HTTP %1: %2").arg(status).arg(details);
                }
                m_logger.Warning(QString("%1 %2 failed: %3").arg(m, endpoint, lastError));
                reply->deleteLater();

                // Only retry on network/connection errors, not on HTTP 4xx/5xx.
                if (isTimeout ||
                    errCode == QNetworkReply::ConnectionRefusedError ||
                    errCode == QNetworkReply::RemoteHostClosedError ||
                    errCode == QNetworkReply::HostNotFoundError ||
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

        // All retries exhausted. For connectivity-shaped failures, tell the
        // user what to actually check instead of leaving a bare socket error.
        if (outError) {
            *outError = lastError.isEmpty() ? "Request failed after retries." : lastError;
            if (lastError.contains("refused", Qt::CaseInsensitive) ||
                lastError.contains("timed out", Qt::CaseInsensitive) ||
                lastError.contains("Host", Qt::CaseInsensitive)) {
                *outError += QString("\n\nIs the RTX Remix Toolkit running with its REST API "
                                     "enabled on %1? (Settings > Connection)").arg(base);
            }
        }
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

        QString meshPrim;
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

            const bool meshLike = lower.contains("/instances/inst_") || lower.contains("/meshes/") ||
                                  lower.contains("/mesh/") || lower.contains("/geom/");
            if (meshLike && meshPrim.isEmpty()) meshPrim = p;
        }

        // A mesh is selected instead of a material: resolve its bound material
        // (same fallback Pull From Remix uses), so Import Textures and Force
        // Push work from a mesh selection too.
        if (!meshPrim.isEmpty()) {
            const QString defPath = ExtractMeshDefinitionPath(meshPrim);
            QString matErr;
            if (GetMaterialFromMeshPrim(defPath.isEmpty() ? meshPrim : defPath,
                                        outMaterialPrim, matErr)) {
                return true;
            }
            outError = "A mesh is selected, but its bound material could not be "
                       "resolved: " + matErr;
            return false;
        }

        outError = "Could not identify a material prim from the current selection.\n"
                   "Select a mesh or material in RTX Remix, then try again.";
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
        QString absMesh; // absolute path that itself looks like a mesh file

        auto consider = [&](const QString& s) {
            const QString p = NormalizePathSlashes(s);
            const QString lower = p.toLower();
            const bool meshExt =
                lower.endsWith(".usd") || lower.endsWith(".usda") || lower.endsWith(".usdc") ||
                lower.endsWith(".obj") || lower.endsWith(".fbx") || lower.endsWith(".gltf") ||
                lower.endsWith(".glb");
            if (QDir::isAbsolutePath(p)) {
                absContext = p;
                // A USD can be the capture's *context layer* rather than the
                // mesh, so only treat an absolute USD as the mesh when its
                // path says so; non-USD mesh formats are unambiguous.
                const bool usdExt = lower.endsWith(".usd") || lower.endsWith(".usda") ||
                                    lower.endsWith(".usdc");
                const bool looksLikeMeshFile =
                    !usdExt || lower.contains("/meshes/") ||
                    QFileInfo(lower).fileName().startsWith(QLatin1String("mesh_"));
                if (meshExt && looksLikeMeshFile) absMesh = p;
                return;
            }
            if (meshExt) relMesh = p;
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

        // Remix can also reference the mesh by an absolute path only; accept
        // that when no relative path was offered (the caller handles absolute
        // mesh paths directly).
        if (relMesh.isEmpty() && !absMesh.isEmpty()) {
            outMeshPath = absMesh;
            outContextAbs = absContext;
            return true;
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

        if (!meshPrimInitial.isEmpty() && materialPrim.isEmpty()) {
            const QString defPath = ExtractMeshDefinitionPath(meshPrimInitial);
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
            const QString defPath = ExtractMeshDefinitionPath(meshPrimInitial);
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
        if (WarnIfBusy(m_operationInProgress)) return;
        const BusyGuard busy(m_operationInProgress);

        QSettings settings("InstaMAT2Remix", "Config");
        const bool autoUnwrap = settings.value("AutoUnwrap", false).toBool();

        // Which InstaMAT template to create: remembered setting, or the
        // chooser dialog ("ask" is the default). Cancelling the chooser
        // cancels the whole Pull, before any network traffic.
        ProjectTemplate tpl = ProjectTemplate::AssetTexturing;
        {
            const QString tplSetting = settings.value("PullProjectTemplate", "ask").toString();
            if (!TemplateFromSettingsValue(tplSetting, tpl)) {
                bool remember = false;
                if (!AskPullTemplate(FindHostMainWindow(), tpl, remember)) {
                    m_logger.Info("Pull cancelled at the template chooser.");
                    return;
                }
                if (remember) {
                    settings.setValue("PullProjectTemplate",
                                      QLatin1String(GetTemplateConfig(tpl).settingsValue));
                    m_logger.Info("Pull template remembered (change in Settings > Pull).");
                }
            }
        }
        const TemplateRecipeConfig& tplCfg = GetTemplateConfig(tpl);
        const bool isAssetTexturing = (tpl == ProjectTemplate::AssetTexturing);
        m_logger.Info(QString("Pull template: %1").arg(QString::fromUtf8(tplCfg.displayName)));

        QProgressDialog progress("Querying RTX Remix for selection...", "Cancel", 0, 3,
                                 FindHostMainWindow());
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

        // Tiling substitute and Blender unwrap are mesh-project concerns; the
        // Element Graph / Materialize templates skip them.
        const bool useTilingMesh =
            isAssetTexturing && settings.value("UseTilingMeshOnPull", false).toBool();
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
        } else if (isAssetTexturing) {
            progress.close();
            QMessageBox::warning(nullptr, kPluginName, "Mesh path is relative, but no absolute context file was provided by Remix.");
            return;
        } else {
            m_logger.Warning("Pull: could not resolve the capture mesh locally — "
                             "continuing (the chosen template does not require it).");
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

        if (!meshAbs.isEmpty() && !QFileInfo::exists(meshAbs)) {
            if (isAssetTexturing) {
                progress.close();
                QMessageBox::warning(nullptr, kPluginName, "Mesh file does not exist locally:\n\n" + meshAbs);
                return;
            }
            m_logger.Warning("Pull: capture mesh missing locally (" + meshAbs +
                             ") — continuing without it (template does not require it).");
            meshAbs.clear();
        }

        // Step 1: Auto-unwrap with Blender (if enabled).
        if (progress.wasCanceled()) { progress.close(); return; }
        progress.setLabelText("Preparing mesh...");
        progress.setValue(1);
        QCoreApplication::processEvents();

        QString finalMesh = meshAbs;
        QString unwrapNote; // surfaced in the Pull summary when unwrap didn't run

        if (autoUnwrap && !useTilingMesh && isAssetTexturing && !meshAbs.isEmpty()) {
            if (m_tools.GetBlenderExecutable().empty()) {
                unwrapNote = "Auto-unwrap was skipped: no Blender executable is "
                             "configured (Settings > Paths).";
                m_logger.Warning(unwrapNote);
            } else {
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
                    // Move the unwrapped OBJ out of %TEMP% into the persistent
                    // MeshCache: this path is saved as LinkedMeshPath and Push
                    // needs it to still exist days later (a temp-dir copy
                    // vanishes on cleanup and would break the export bind).
                    const QString tempMesh = QString::fromStdString(unwrapped);
                    const QString cacheDir = MeshCacheDirFor(meshAbs);
                    QDir().mkpath(cacheDir);
                    const QString cached = QDir(cacheDir).filePath(
                        QFileInfo(meshAbs).completeBaseName() + "_unwrapped.obj");
                    QFile::remove(cached);
                    if (QFile::copy(tempMesh, cached)) {
                        QFile::remove(tempMesh);
                        finalMesh = cached;
                    } else {
                        m_logger.Warning("Could not move the unwrapped mesh into MeshCache; "
                                         "using the temp copy: " + tempMesh);
                        finalMesh = tempMesh;
                    }
                } else {
                    unwrapNote = "Auto-unwrap with Blender failed; the original mesh "
                                 "(with its original UVs) was used instead. Details "
                                 "are in the plugin log.";
                    m_logger.Warning(unwrapNote);
                }
            }
        }

        // Materialize Image: download the material's current Remix texture so
        // the wizard can select it as the image to materialize. Any failure
        // aborts BEFORE the wizard opens, with an actionable message.
        QString materializeImage;
        if (tpl == ProjectTemplate::MaterializeImage) {
            progress.setLabelText("Downloading the material's texture from Remix...");
            QCoreApplication::processEvents();
            QString imgErr;
            if (!PrepareMaterializeSourceImage(materialPrim, materializeImage, imgErr)) {
                progress.close();
                QMessageBox::warning(nullptr, kPluginName,
                                     "Pull From Remix failed:\n\n" + imgErr);
                return;
            }
            m_logger.Info("Materialize source image ready: " + materializeImage);
        }

        // Cancel check BEFORE persisting anything: cancelling here must not
        // silently relink Push to a new material while the old project is open.
        if (progress.wasCanceled()) { progress.close(); return; }

        // Cache link state.
        m_linkedMaterialPrim = materialPrim.toStdString();
        m_linkedMeshPath = finalMesh.toStdString();
        settings.setValue("LinkedMaterialPrim", materialPrim);
        settings.setValue("LinkedMeshPath", finalMesh);
        settings.setValue("LinkedProjectTemplate", QLatin1String(tplCfg.settingsValue));
        if (tpl == ProjectTemplate::MaterializeImage) {
            settings.setValue("LinkedImagePath", materializeImage);
        } else {
            settings.remove("LinkedImagePath");
        }

        // Register external folder so the mesh is easy to find in InstaMAT's
        // library picker (the Materialize image folder is registered by
        // PrepareMaterializeSourceImage).
        if (!finalMesh.isEmpty()) {
            m_instaMAT.RegisterExternalAssetFolder(
                QFileInfo(finalMesh).dir().absolutePath().toStdString().c_str());
        }

        const QString clipboardPath =
            (tpl == ProjectTemplate::MaterializeImage) ? materializeImage : finalMesh;
        if (!clipboardPath.isEmpty() && QGuiApplication::clipboard())
            QGuiApplication::clipboard()->setText(clipboardPath);

        // Step 2: Drive InstaMAT's New Project dialog so the chosen template
        // opens automatically (Substance2Remix-style, extended to templates).
        if (progress.wasCanceled()) { progress.close(); return; }
        progress.setLabelText("Creating InstaMAT project...");
        progress.setValue(2);
        QCoreApplication::processEvents();

        const QString recipeTarget =
            (tpl == ProjectTemplate::MaterializeImage) ? materializeImage
            : (tpl == ProjectTemplate::ElementGraph)   ? QString()
                                                       : finalMesh;
        QString recipeErr;
        bool projectTypeUncertain = false;
        auto recipeFileLog = [this](const QString& s) { m_logger.Info("Recipe: " + s); };
        const bool projectCreated = TryCreateProjectFromTemplate(
            tplCfg, recipeTarget, &recipeErr, &projectTypeUncertain, recipeFileLog);

        progress.setValue(3);
        progress.close();

        if (projectCreated) {
            m_logger.Info(QString("Auto-create '%1' project succeeded (target: %2)")
                              .arg(QString::fromUtf8(tplCfg.displayName),
                                   recipeTarget.isEmpty() ? QStringLiteral("(none)") : recipeTarget));
            if (projectTypeUncertain) {
                m_logger.Warning(QString("Project type could not be confirmed as %1 — "
                                         "the recipe fell back to the first project-type tile "
                                         "(possibly Material Layering).")
                                     .arg(QString::fromUtf8(tplCfg.displayName)));
            }

            QStringList summary;
            summary << QString("%1 project created and linked to Remix.")
                           .arg(QString::fromUtf8(tplCfg.displayName));
            summary << "";
            summary << QString("Material: %1").arg(materialPrim);
            if (!finalMesh.isEmpty())
                summary << QString("Mesh: %1").arg(QFileInfo(finalMesh).fileName());
            if (tpl == ProjectTemplate::MaterializeImage)
                summary << QString("Image: %1").arg(QFileInfo(materializeImage).fileName());
            summary << "";
            summary << "Use 'Import Textures from Remix' to pull this material's "
                       "textures when you want them.";
            if (tpl == ProjectTemplate::ElementGraph) {
                summary << "";
                summary << "Tip: name your graph outputs after PBR channels (Base "
                           "Color, Roughness, Metallic, Normal, Height, Emissive, "
                           "Transmittance, ...) so Push To Remix can find them. The "
                           "pulled mesh folder is registered in the Asset Browser.";
            }
            if (!unwrapNote.isEmpty()) {
                summary << "";
                summary << "Note: " + unwrapNote;
            }
            if (projectTypeUncertain) {
                summary << "";
                summary << QString("Note: the project type could not be auto-confirmed as "
                                   "'%1'. InstaMAT may have created a different project "
                                   "type. Painting and Push To Remix still work as normal.")
                               .arg(QString::fromUtf8(tplCfg.displayName));
            }
            QMessageBox::information(nullptr, kPluginName, summary.join("\n"));
        } else {
            m_logger.Warning(QString("Auto-create project failed: %1").arg(recipeErr));

            // Fallback: the target path is already on the clipboard; open its
            // folder so the user can create the project manually.
            const QString fallbackAsset =
                recipeTarget.isEmpty() ? finalMesh : recipeTarget;
            QMessageBox::warning(
                nullptr, kPluginName,
                QString("Pull From Remix: the %1 project could not be created "
                        "automatically:\n\n%2\n\n"
                        "%3"
                        "Create the project manually (File > New), then "
                        "use 'Import Textures from Remix' as usual. "
                        "Details are in the plugin log.")
                    .arg(QString::fromUtf8(tplCfg.displayName),
                         recipeErr.section('\n', 0, 0),
                         fallbackAsset.isEmpty()
                             ? QString()
                             : QString("The asset path was copied to your clipboard and its "
                                       "folder was opened in Explorer:\n%1\n\n").arg(fallbackAsset)));
            if (!fallbackAsset.isEmpty()) {
                QDesktopServices::openUrl(QUrl::fromLocalFile(
                    QFileInfo(fallbackAsset).dir().absolutePath()));
            }
        }
    }

    bool RemixConnector::PrepareMaterializeSourceImage(const QString& materialPrim,
                                                       QString&       outImageAbs,
                                                       QString&       outErr) {
        outImageAbs.clear();
        outErr.clear();

        QString remixDirAbs;
        QString dirErr;
        if (!GetRemixDefaultDirectory(remixDirAbs, dirErr)) {
            outErr = "Could not determine the Remix project directory:\n" + dirErr;
            return false;
        }

        const QString encodedMat = UrlEncodeKeepSlashes(NormalizePathSlashes(materialPrim));
        QString apiErr;
        const QJsonDocument doc = RequestJson(
            "GET", QString("/stagecraft/assets/%1/textures").arg(encodedMat),
            {}, nullptr, &apiErr);
        if (!doc.isObject()) {
            outErr = "Could not list the material's textures:\n" + apiErr;
            return false;
        }
        const QJsonArray textures = doc.object().value("textures").toArray();

        // Prefer the albedo/diffuse texture; fall back to transmittance (glass
        // materials have no diffuse), then to the first texture at all.
        QJsonValue chosen;
        for (const QString& wanted : {QStringLiteral("albedo"), QStringLiteral("transmittance")}) {
            for (const QJsonValue& entry : textures) {
                if (!entry.isArray()) continue;
                const QJsonArray pair = entry.toArray();
                if (pair.size() < 2 || !pair.at(0).isString()) continue;
                if (ResolveCanonicalChannel(pair.at(0).toString()) == wanted) {
                    chosen = entry;
                    break;
                }
            }
            if (!chosen.isNull() && !chosen.isUndefined()) break;
        }
        if ((chosen.isNull() || chosen.isUndefined()) && !textures.isEmpty())
            chosen = textures.first();
        if (chosen.isNull() || chosen.isUndefined()) {
            outErr = QString("The material has no textures to materialize:\n%1\n\n"
                             "Assign a texture in RTX Remix first, or pull with the "
                             "Asset Texturing template instead.").arg(materialPrim);
            return false;
        }

        // Download/convert just that one texture into the Pulled Textures dir.
        const QString destDir = GetPulledTexturesDir(remixDirAbs);
        QJsonArray single;
        single.append(chosen);
        QVector<ChannelEntry> entries;
        int pulled = 0, converted = 0;
        DownloadAndConvertTextureList(single, remixDirAbs, destDir,
                                      NamingPolicy::CanonicalChannelOrOriginal,
                                      nullptr, entries, pulled, converted);
        if (pulled == 0 || entries.isEmpty()) {
            outErr = QString("Could not download/convert the material's texture. ");
            if (m_tools.GetTexconvExecutable().empty()) {
                outErr += "texconv.exe was not found — set it in Settings > Paths. ";
            }
            outErr += "Details are in the plugin log:\n" + GetLogFilePath();
            return false;
        }

        // Rename to a material-specific stem: the wizard's asset list is
        // matched BY FILENAME, and a generic "albedo.png" could collide with
        // files from other registered folders.
        QString root = DeriveDesiredRootFromPrim(materialPrim);
        if (root.isEmpty()) root = "pulled";
        const QString srcPath = QDir(destDir).filePath(entries.first().filename);
        const QFileInfo srcInfo(srcPath);
        const QString distinct = QDir(destDir).filePath(
            QString("%1_materialize.%2").arg(root, srcInfo.suffix().isEmpty()
                                                       ? QStringLiteral("png")
                                                       : srcInfo.suffix()));
        QFile::remove(distinct);
        if (QFile::rename(srcPath, distinct)) {
            outImageAbs = QDir::cleanPath(distinct);
        } else {
            m_logger.Warning("Could not rename the materialize image to a distinct "
                             "name; using " + srcPath);
            outImageAbs = QDir::cleanPath(srcPath);
        }

        // Make the folder pickable in the wizard's asset popup.
        m_instaMAT.RegisterExternalAssetFolder(destDir.toStdString().c_str());
        return true;
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

        // Two textures can map to the same canonical channel (e.g. two albedo
        // inputs); keep the first under the canonical name and fall back to
        // the original filename for later ones instead of silently
        // overwriting.
        QSet<QString> usedNames;
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
                    if (!canonicalChannel.isEmpty()
                        && !usedNames.contains(canonicalChannel + ".png")) {
                        const QString desired = canonicalChannel + ".png";
                        const QString desiredPath = QDir(destDir).filePath(desired);
                        if (desiredPath != writtenPath) {
                            QFile::remove(desiredPath);
                            if (QFile::rename(writtenPath, desiredPath)) {
                                writtenName = desired;
                            } else {
                                m_logger.Warning(QString("Could not rename %1 -> %2").arg(writtenPath, desiredPath));
                            }
                        } else {
                            writtenName = desired;
                        }
                    }
                    usedNames.insert(writtenName);
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
            if (usedNames.contains(destName)) destName = origName; // keep both files
            usedNames.insert(destName);
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
        if (WarnIfBusy(m_operationInProgress)) return false;
        const BusyGuard busy(m_operationInProgress);

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

        QProgressDialog progress("Importing textures from RTX Remix...", "Cancel", 0, textures.size(),
                                 FindHostMainWindow());
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
            QString msg = "Import cancelled.";
            if (pulledCount > 0) {
                msg += QString("\n\n%1 texture(s) were already saved to:\n%2")
                           .arg(pulledCount).arg(destDir);
            }
            QMessageBox::information(nullptr, kPluginName, msg);
            return false;
        }

        // Nothing imported: this is a failure, not a success with a zero in it.
        if (pulledCount == 0) {
            QString msg = QString("No textures could be imported for the material:\n%1\n\n")
                              .arg(materialPrim);
            if (m_tools.GetTexconvExecutable().empty()) {
                msg += "texconv.exe was not found, so Remix's DDS textures cannot be "
                       "converted. Set 'texconv.exe' in Settings > Paths, then try "
                       "again.\n\n";
            }
            msg += "Details are in the plugin log:\n" + GetLogFilePath();
            QMessageBox::warning(nullptr, kPluginName, msg);
            return false;
        }

        QDesktopServices::openUrl(QUrl::fromLocalFile(destDir));

        QString summary =
            QString("Imported %1 texture(s) from RTX Remix (%2 converted from DDS).\n\n"
                    "Saved to:\n%3\n\n"
                    "The folder is registered in InstaMAT's Asset Browser and was opened "
                    "in Explorer. InstaMAT's plugin SDK has no API to assign textures "
                    "into an open project automatically — drag each map onto the "
                    "matching channel (albedo → Base Color, normal → Normal, ...).")
                .arg(pulledCount)
                .arg(convertedCount)
                .arg(destDir);
        if (pulledCount < textures.size()) {
            summary += QString("\n\nNote: %1 of %2 texture(s) could not be imported — "
                               "see the plugin log for the reasons.")
                           .arg(textures.size() - pulledCount)
                           .arg(textures.size());
        }
        QMessageBox::information(nullptr, kPluginName, summary);
        return true;
    }

    QString RemixConnector::PreIngestStageDir() {
        return QDir::cleanPath(QDir(QDir::tempPath()).filePath("InstaMAT2Remix_PreIngest"));
    }

    bool RemixConnector::IsSafeToWipe(const QString& dirPath) {
        const QString p = QDir::cleanPath(dirPath.trimmed());
        // QDir("") means the process working directory — never wipe that.
        if (p.isEmpty() || !QDir::isAbsolutePath(p)) return false;
        if (QDir(p).isRoot()) return false;

        QStringList protectedDirs;
        protectedDirs << QDir::homePath()
                      << QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
                      << QStandardPaths::writableLocation(QStandardPaths::DesktopLocation)
                      << QStandardPaths::writableLocation(QStandardPaths::DownloadLocation)
                      << QStandardPaths::writableLocation(QStandardPaths::PicturesLocation)
                      << QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        for (const QString& prot : protectedDirs) {
            if (prot.isEmpty()) continue;
            if (QString::compare(QDir::cleanPath(prot), p, Qt::CaseInsensitive) == 0)
                return false;
        }
        return true;
    }

    QString RemixConnector::DefaultLayerProjectDir() {
        // Resolve Documents via the shell known folder — on Windows 11 the
        // Documents folder is commonly OneDrive-redirected and is NOT
        // %USERPROFILE%\Documents; Studio writes its Library to the redirected
        // location.
        QString docs = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
        if (docs.isEmpty()) docs = QDir::homePath() + "/Documents";
        return QDir::cleanPath(docs + "/InstaMAT/Library");
    }

    QString RemixConnector::ResolveIngestValidationType(const QString& pbrType,
                                                        const QString& normalEncoding) {
        const QString t = pbrType.trimmed().toLower();
        if (t == QLatin1String("normal")) {
            const QString e = normalEncoding.trimmed().toLower();
            if (e == QLatin1String("ogl")) return QStringLiteral("NORMAL_OGL");
            if (e == QLatin1String("oth")) return QStringLiteral("NORMAL_OTH");
            return QStringLiteral("NORMAL_DX");
        }
        // OTHER = raw colorspace, the safe default for unrecognized channels
        // (the old DIFFUSE default sRGB-converted linear data).
        return kPbrToIngestValidation.value(t, QStringLiteral("OTHER"));
    }

    RemixConnector::RemixMaterialKind
    RemixConnector::ClassifyMaterialFromTextureAttrs(const QStringList& usdAttrs) {
        bool sawOpaqueMarker = false;
        for (const QString& attr : usdAttrs) {
            const QString suffix = attr.section(':', -1).trimmed().toLower();
            // Exact equality: the opaque SSS input is
            // subsurface_transmittance_texture and must NOT classify as glass.
            if (suffix == QLatin1String("transmittance_texture")) {
                return RemixMaterialKind::Translucent;
            }
            if (suffix == QLatin1String("diffuse_texture") ||
                suffix.startsWith(QLatin1String("albedo")) ||
                suffix == QLatin1String("reflectionroughness_texture") ||
                suffix == QLatin1String("metallic_texture") ||
                suffix == QLatin1String("height_texture") ||
                suffix.startsWith(QLatin1String("subsurface_"))) {
                sawOpaqueMarker = true;
            }
        }
        return sawOpaqueMarker ? RemixMaterialKind::Opaque : RemixMaterialKind::Unknown;
    }

    QJsonArray RemixConnector::BuildTexturePutPairs(const QString& materialPrim,
                                                    const QHash<QString, QString>& ingestedPaths,
                                                    bool translucent,
                                                    QStringList* outSkipped) {
        if (outSkipped) outSkipped->clear();
        QJsonArray pairs;
        for (const auto& spec : kDefaultPbrSpecs) {
            if (!ingestedPaths.contains(spec.pbrType)) continue;
            const QString& mdl = translucent ? spec.mdlInputTranslucent : spec.mdlInputOpaque;
            if (mdl.isEmpty()) {
                if (outSkipped) outSkipped->append(spec.pbrType);
                continue;
            }
            const QString usdAttr = NormalizePathSlashes(materialPrim) + "/Shader.inputs:" + mdl;
            QJsonArray pair;
            pair.append(usdAttr);
            pair.append(NormalizePathSlashes(ingestedPaths.value(spec.pbrType)));
            pairs.append(pair);
        }
        return pairs;
    }

    bool RemixConnector::MergeOpacityIntoAlbedoAlpha(const QString& albedoPath,
                                                     const QString& opacityPath,
                                                     QString&       outMergedPngPath,
                                                     QString&       outErr) {
        outMergedPngPath.clear();
        outErr.clear();

        QImage albedo(albedoPath);
        if (albedo.isNull()) {
            outErr = "Could not load albedo image: " + albedoPath;
            return false;
        }
        QImage opacity(opacityPath);
        if (opacity.isNull()) {
            outErr = "Could not load opacity image: " + opacityPath;
            return false;
        }

        albedo = albedo.convertToFormat(QImage::Format_ARGB32);
        if (opacity.size() != albedo.size()) {
            opacity = opacity.scaled(albedo.size(), Qt::IgnoreAspectRatio,
                                     Qt::SmoothTransformation);
        }
        opacity = opacity.convertToFormat(QImage::Format_ARGB32);

        for (int y = 0; y < albedo.height(); ++y) {
            QRgb* dst = reinterpret_cast<QRgb*>(albedo.scanLine(y));
            const QRgb* src = reinterpret_cast<const QRgb*>(opacity.constScanLine(y));
            for (int x = 0; x < albedo.width(); ++x) {
                dst[x] = qRgba(qRed(dst[x]), qGreen(dst[x]), qBlue(dst[x]),
                               qGray(src[x]));
            }
        }

        // Always PNG: jpg has no alpha and QImage cannot write tga. The ingest
        // converts to DDS regardless of the source container.
        const QFileInfo fi(albedoPath);
        const QString merged = fi.dir().filePath(fi.completeBaseName() + "_merged.png");
        if (!albedo.save(merged, "PNG")) {
            outErr = "Could not write merged albedo PNG: " + merged;
            return false;
        }
        outMergedPngPath = merged;
        return true;
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

        QSettings ingestSettings("InstaMAT2Remix", "Config");
        const QString validationType = ResolveIngestValidationType(
            pbrType, ingestSettings.value("NormalMapEncoding", "dx").toString());
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

    RemixConnector::WorkerRunParse RemixConnector::ParseWorkerStdout(const QString& mergedOutput) {
        WorkerRunParse parse;
        for (const QString& rawLine : mergedOutput.split('\n')) {
            const QString line = rawLine.trimmed();
            if (line.isEmpty()) continue;
            if (!line.startsWith("IM2RX ")) {
                parse.engineTail << line;
                while (parse.engineTail.size() > 15) parse.engineTail.removeFirst();
                continue;
            }
            const QString msg = line.mid(6);
            parse.protocolLines << msg;
            if (msg.startsWith("CHANNEL=")) {
                const QString value = msg.mid(8); // canonical:filename
                const int colon = value.indexOf(':');
                if (colon > 0) parse.channelFiles.append(value.mid(colon + 1).trimmed());
            } else if (msg.startsWith("COLLAPSED=")) {
                parse.collapsed = (msg.mid(10).trimmed() == "1");
            } else if (msg.startsWith("FINALSIZE=")) {
                parse.finalSize = msg.mid(10).trimmed();
            } else if (msg.startsWith("GRAPHTYPE=")) {
                parse.graphType = msg.mid(10).trimmed();
            } else if (msg.startsWith("FATAL=")) {
                parse.fatalReason = msg.mid(6).trimmed();
            } else if (msg.startsWith("OUTPUTFALLBACK ")) {
                // OUTPUTFALLBACK name='<name>' canonical=albedo
                const int start = msg.indexOf("name='");
                const int end = msg.lastIndexOf("' canonical=");
                if (start >= 0 && end > start + 6)
                    parse.fallbackOutputName = msg.mid(start + 6, end - (start + 6));
            } else if (msg.startsWith("ERROR=")) {
                parse.error = msg.mid(6);
            }
        }
        return parse;
    }

    bool RemixConnector::ShouldRetryWorkerFailure(const QString& fatalReason) {
        // No FATAL= line (legacy worker, crash, timeout) → possibly the
        // per-process render race → a fresh process is an independent draw.
        return fatalReason.isEmpty();
    }

    QString RemixConnector::WorkerErrorTip(const QString& fatalReason) {
        if (fatalReason == "outputs") {
            return QString("Tip: name your graph outputs after PBR channels (%1), "
                           "save (Ctrl+S) and push again.")
                .arg(QString::fromUtf8(InstaMAT2Remix::PbrOutputNamesHint()));
        }
        return QString();
    }

    bool RemixConnector::ExportActiveLayeringProject(const QString& outDir,
                                                     QStringList&   outChannelFiles,
                                                     QString&       outError) {
        outChannelFiles.clear();
        outError.clear();
        m_exportHadCollapse = false;
        m_lastExportSize.clear();
        m_exportGraphTypeNote.clear();
        m_exportAlbedoFallbackNote.clear();

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
        // The worker MUST bind the mesh from raw file bytes: the package's own
        // pkg:///file:// mesh bindings do not resolve in a standalone SDK
        // process and Execute then "succeeds" with blank outputs (see
        // CLAUDE.md). A missing mesh is fatal only for mesh-based templates
        // (Asset Texturing); Element Graph / Materialize projects may have no
        // ElementMesh input at all — the worker's Inherit rung passes those
        // through and refuses inherited pkg:// binds when a mesh input exists.
        const QString linkedTemplate =
            settings.value("LinkedProjectTemplate", "asset_texturing").toString().trimmed().toLower();
        const bool meshRequired =
            linkedTemplate.isEmpty() || linkedTemplate == QLatin1String("asset_texturing");
        const QString meshAbs = QString::fromStdString(m_linkedMeshPath);
        if (!meshAbs.isEmpty() && QFileInfo::exists(meshAbs)) {
            baseArgs << "--mesh" << meshAbs;
        } else if (meshRequired) {
            outError = meshAbs.isEmpty()
                ? QString("No linked mesh is recorded for this project. Use 'Pull From "
                          "Remix' to link a mesh, then push again.")
                : QString("The linked mesh file no longer exists:\n  %1\n\n"
                          "Use 'Pull From Remix' to relink it, then push again. "
                          "(Exporting without the mesh would produce blank textures.)")
                      .arg(meshAbs);
            return false;
        } else {
            m_logger.Info(QString("ExportActive: no mesh file for template '%1' — the "
                                  "graph binds its own inputs.").arg(linkedTemplate));
        }
        // Materialize projects: bytes-bind the pulled source image too, in
        // case the package's own image binding doesn't resolve standalone.
        const QString imageAbs = settings.value("LinkedImagePath", "").toString();
        if (!imageAbs.isEmpty() && QFileInfo::exists(imageAbs)) {
            baseArgs << "--image" << imageAbs;
        }

        // One worker invocation at the given size (0,0 = worker resolves the
        // baked size). Fills channelFiles; sets outCollapsed when the worker
        // reported a render race (some channels came out 1x1).
        QString workerGraphType;     // "layer" | "element" | "materialize" (from GRAPHTYPE=)
        QString fallbackOutputName;  // OUTPUTFALLBACK: lone output exported as albedo
        auto runWorkerOnce = [&](int reqW, int reqH, QStringList& channelFiles,
                                 bool& outCollapsed, QString& outFinalSize,
                                 QString& runError, QString& outFatalReason) -> bool {
            channelFiles.clear();
            outCollapsed = false;
            outFinalSize.clear();
            outFatalReason.clear();

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
            const WorkerRunParse parsed = ParseWorkerStdout(output);
            for (const QString& msg : parsed.protocolLines) {
                m_logger.Info("ExportWorker: " + msg);
            }
            channelFiles = parsed.channelFiles;
            outCollapsed = parsed.collapsed;
            outFinalSize = parsed.finalSize;
            outFatalReason = parsed.fatalReason;
            if (!parsed.graphType.isEmpty()) workerGraphType = parsed.graphType;
            if (!parsed.fallbackOutputName.isEmpty())
                fallbackOutputName = parsed.fallbackOutputName;

            const bool exitedOk =
                (proc.exitStatus() == QProcess::NormalExit && proc.exitCode() == 0);
            if (!exitedOk || channelFiles.isEmpty()) {
                for (const QString& line : parsed.engineTail) {
                    m_logger.Warning("ExportWorker(engine): " + line);
                }
                QString workerError = parsed.error;
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
        // FIRST clean render; if none is clean we keep the last attempt's files
        // and set m_exportHadCollapse — PushToRemix's collapse guard then
        // aborts the push. The outDir is wiped between attempts.
        auto parseSize = [](const QString& sz) -> QSize {
            const int x = sz.indexOf('x');
            if (x <= 0) return QSize();
            return QSize(sz.left(x).toInt(), sz.mid(x + 1).toInt());
        };

        QList<QSize> sizeLadder;
        // Prefer the user's actual baked/requested size — the collapse odds are
        // roughly size-independent (~30% clean per fresh process on a healthy
        // GPU, measured), so spending draws at the baked size delivers the
        // intended resolution rather than a needlessly smaller one. Step-downs
        // are appended only as a last resort once the baked-size draws are
        // exhausted (scaled proportionally so non-square exports keep their
        // aspect ratio). Loop is capped at 6 attempts to bound the worst-case
        // push time (each worker is ~13 s); it stops at the first clean render,
        // so a healthy GPU pays only one attempt.
        for (int i = 0; i < 3; ++i) sizeLadder << exportSize; // may be 0x0 = Auto

        QString lastError;
        QString lastFatalReason;
        bool lastCollapsed = true;
        QString finalSize;
        bool gotClean = false;
        int steppedFloor = 0; // largest step-down already queued
        for (int attempt = 0; attempt < sizeLadder.size() && attempt < 6; ++attempt) {
            const QSize req = sizeLadder.at(attempt);
            if (QDir(outDir).exists()) QDir(outDir).removeRecursively();
            QDir().mkpath(outDir);

            QStringList channelFiles;
            bool collapsed = false;
            QString runError;
            QString fatalReason;
            const bool ok = runWorkerOnce(req.width(), req.height(), channelFiles, collapsed, finalSize, runError, fatalReason);
            if (!ok) {
                lastError = runError;
                m_logger.Warning(QString("ExportActive: worker attempt %1 failed: %2")
                                     .arg(attempt + 1).arg(runError));
                if (!ShouldRetryWorkerFailure(fatalReason)) {
                    // FATAL= — a fresh process would fail identically (bad
                    // output naming, missing graph, licensing…). Retrying
                    // burned 3 spawns (~36 s) on this before 0.0.2.
                    lastFatalReason = fatalReason;
                    m_logger.Warning(QString("ExportActive: deterministic worker failure "
                                             "('%1') — skipping the remaining retries.")
                                         .arg(fatalReason));
                    break;
                }
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

            // Queue step-downs once, based on the resolution actually rendered,
            // scaling both dimensions so the aspect ratio survives.
            if (steppedFloor == 0) {
                QSize base = parseSize(finalSize);
                if (base.width() <= 0 || base.height() <= 0) base = QSize(2048, 2048);
                const int maxDim = qMax(base.width(), base.height());
                for (int s = (maxDim > 2048 ? 2048 : maxDim / 2); s >= 512; s /= 2) {
                    const double scale = double(s) / maxDim;
                    sizeLadder << QSize(qMax(1, qRound(base.width() * scale)),
                                        qMax(1, qRound(base.height() * scale)));
                }
                steppedFloor = 512;
            }
        }

        if (outChannelFiles.isEmpty()) {
            QString cause = lastError.isEmpty() ? QString("no channels were produced")
                                                : lastError;
            // Worker messages end with '.' — chop so the sentence join below
            // doesn't render "written.. See".
            while (cause.endsWith(QLatin1Char('.'))) cause.chop(1);
            outError = QString("Live export failed: %1.\n\nSee the plugin log for details:\n%2")
                           .arg(cause, GetLogFilePath());
            const QString tip = WorkerErrorTip(lastFatalReason);
            if (!tip.isEmpty()) outError += "\n\n" + tip;
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

        if (!fallbackOutputName.isEmpty()) {
            m_exportAlbedoFallbackNote =
                QString("Note: the graph's only output ('%1') is not named after a PBR "
                        "channel, so it was pushed as the Base Color (albedo) map. Rename "
                        "the output (e.g. Base Color, Roughness, Normal) if it should be a "
                        "different channel.")
                    .arg(fallbackOutputName);
            m_logger.Info(m_exportAlbedoFallbackNote);
        }

        // Newest-.IMP discovery is type-agnostic: warn when the graph the
        // worker actually executed doesn't match the template Pull created
        // (e.g. the user saved a different project since the pull).
        if (!workerGraphType.isEmpty()) {
            static const QHash<QString, QString> kTemplateToGraphType = {
                {"asset_texturing",   "layer"},
                {"element_graph",     "element"},
                {"materialize_image", "materialize"},
            };
            const QString expected = kTemplateToGraphType.value(linkedTemplate);
            if (!expected.isEmpty() && workerGraphType != expected) {
                m_exportGraphTypeNote =
                    QString("Note: the exported project is a '%1' graph, but the last Pull "
                            "created a '%2' project. Push always exports the most recently "
                            "saved project — make sure the right project was saved last.")
                        .arg(workerGraphType, linkedTemplate);
                m_logger.Warning(m_exportGraphTypeNote);
            }
        }
        if (m_exportHadCollapse) {
            m_logger.Warning("ExportActive: some channels still rendered at 1x1 after all "
                             "size attempts (GPU/driver race — worse under GPU contention).");
        }
        return true;
    }

    void RemixConnector::PushToRemix(bool forceRelinkAndRename) {
        if (WarnIfBusy(m_operationInProgress)) return;
        const BusyGuard busy(m_operationInProgress);

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
        const QString defaultExportDir =
            QDir::cleanPath(QDir::tempPath() + "/InstaMAT2Remix_Export");
        QString exportDir = settings.value("ExportFolder", defaultExportDir).toString().trimmed();
        if (exportDir.isEmpty()) exportDir = defaultExportDir; // QDir("") is the CWD!
        exportDir = QDir::cleanPath(exportDir);
        // The export folder is EMPTIED before every push. Refuse to wipe
        // anything that is clearly not a dedicated folder (drive roots, home,
        // Documents, ...) — a mis-set path here must never delete user data.
        if (!IsSafeToWipe(exportDir)) {
            QMessageBox::warning(nullptr, kPluginName,
                QString("Push stopped:\n\nThe configured Export Folder is not safe to "
                        "use:\n  %1\n\nThis folder is emptied on every push, so it must "
                        "be a dedicated folder (not a drive root, your home folder, "
                        "Documents, Desktop, Downloads or Pictures).\n\n"
                        "Choose a different folder in Settings > Paths, or clear the "
                        "field to restore the default:\n  %2")
                    .arg(exportDir, defaultExportDir));
            return;
        }
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

        // 4a. Opacity: Remix reads opacity from the diffuse texture's ALPHA
        //     channel (AperturePBR has no consumed opacity_texture input).
        //     When enabled and both maps exist, merge opacity into albedo's
        //     alpha; the standalone opacity file is never pushed.
        if (settings.value("IncludeOpacityMap", false).toBool()
            && exportedFiles.contains("opacity") && exportedFiles.contains("albedo")) {
            QString mergedPath;
            QString mergeErr;
            if (MergeOpacityIntoAlbedoAlpha(exportedFiles.value("albedo"),
                                            exportedFiles.value("opacity"),
                                            mergedPath, mergeErr)) {
                exportedFiles.insert("albedo", mergedPath);
                m_logger.Info("Merged the opacity map into the albedo alpha channel for push.");
            } else {
                m_logger.Warning("Opacity merge failed (" + mergeErr +
                                 ") — pushing albedo without merged opacity.");
            }
        }
        exportedFiles.remove("opacity");

        // 4b. Channels Remix has no texture input for at all (ao, ...): the
        //     exported file stays on disk but is not pushed.
        QStringList skippedNoRemixInput;
        {
            QSet<QString> pushable;
            for (const auto& spec : kDefaultPbrSpecs) pushable.insert(spec.pbrType);
            for (auto it = exportedFiles.begin(); it != exportedFiles.end();) {
                if (!pushable.contains(it.key())) {
                    skippedNoRemixInput << it.key();
                    it = exportedFiles.erase(it);
                } else {
                    ++it;
                }
            }
        }
        if (!skippedNoRemixInput.isEmpty()) {
            m_logger.Info("Not pushed (no Remix texture input): " + skippedNoRemixInput.join(", "));
        }

        // 4c. Opaque vs translucent target: AperturePBR_Translucent consumes
        //     only transmittance/emissive/normal, so skip the rest up front
        //     (each ingest is a long-running call — don't waste them).
        bool materialIsTranslucent = false;
        {
            const QString encodedMat = UrlEncodeKeepSlashes(NormalizePathSlashes(materialPrim));
            QString texErr;
            const QJsonDocument texDoc = RequestJson(
                "GET", QString("/stagecraft/assets/%1/textures").arg(encodedMat),
                {}, nullptr, &texErr);
            if (texDoc.isObject()) {
                QStringList attrs;
                const QJsonArray textures = texDoc.object().value("textures").toArray();
                for (const QJsonValue& entry : textures) {
                    if (!entry.isArray()) continue;
                    const QJsonArray pair = entry.toArray();
                    if (pair.size() >= 1 && pair.at(0).isString()) attrs << pair.at(0).toString();
                }
                const RemixMaterialKind kind = ClassifyMaterialFromTextureAttrs(attrs);
                materialIsTranslucent = (kind == RemixMaterialKind::Translucent);
                if (kind == RemixMaterialKind::Unknown) {
                    m_logger.Info("Material kind not identifiable from its texture attrs — treating as opaque.");
                } else if (materialIsTranslucent) {
                    m_logger.Info("Linked material is translucent (glass): only transmittance/emissive/normal are pushed.");
                }
            } else {
                m_logger.Warning("Could not query the material's textures to detect its kind (" +
                                 texErr + ") — assuming opaque.");
            }
        }
        QStringList skippedForMaterialKind;
        for (auto it = exportedFiles.begin(); it != exportedFiles.end();) {
            QString mdlInput;
            for (const auto& spec : kDefaultPbrSpecs) {
                if (spec.pbrType == it.key()) {
                    mdlInput = materialIsTranslucent ? spec.mdlInputTranslucent
                                                     : spec.mdlInputOpaque;
                    break;
                }
            }
            if (mdlInput.isEmpty()) {
                skippedForMaterialKind << it.key();
                it = exportedFiles.erase(it);
            } else {
                ++it;
            }
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
            // Uniform grayscale channels legitimately render 1x1.
            if (ChannelMayLegitimatelyRenderFlat(it.key().toStdString())) continue;
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
        QProgressDialog progress("Pushing textures to RTX Remix...", "Cancel", 0, stagedFiles.size(),
                                 FindHostMainWindow());
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

        // 7. Batch-update the Remix material's texture inputs, routed to the
        //    shader inputs the target material kind actually consumes.
        const QJsonArray texturePairs =
            BuildTexturePutPairs(materialPrim, ingestedPaths, materialIsTranslucent);

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
        // (No collapse note here: the collapse guard above aborts the push
        // outright when any channel rendered 1x1, so a summary implies clean.)
        if (!skippedNoRemixInput.isEmpty()) {
            summary += QString("\n\nNot pushed — RTX Remix has no texture input for: %1. "
                               "The exported file(s) remain in the export folder.")
                           .arg(skippedNoRemixInput.join(", "));
        }
        if (!skippedForMaterialKind.isEmpty()) {
            summary += QString("\n\nNot pushed — the target material is translucent (glass), "
                               "which only takes transmittance, emissive and normal: %1.")
                           .arg(skippedForMaterialKind.join(", "));
        }
        if (ingestedPaths.contains("anisotropy")) {
            summary += "\n\nNote: an anisotropy texture was pushed, but current RTX Remix "
                       "runtimes read the material's anisotropy constant rather than the texture.";
        }
        if (!m_exportGraphTypeNote.isEmpty()) {
            summary += "\n\n" + m_exportGraphTypeNote;
        }
        if (!m_exportAlbedoFallbackNote.isEmpty()) {
            summary += "\n\n" + m_exportAlbedoFallbackNote;
        }
        if (!ingestErrors.isEmpty()) {
            summary += QString("\n\nWarning — %1 channel(s) could NOT be ingested and "
                               "kept their previous texture in Remix:\n%2")
                           .arg(ingestErrors.size())
                           .arg(ingestErrors.join("\n"));
        }
        QMessageBox::information(nullptr, kPluginName, summary);
    }
}

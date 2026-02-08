#include "GuiManager.h"
#include "RemixConnector.h"
#include "ExternalTools.h"
#include "DiagnosticsDialog.h"
#include "PluginInfo.h"
#include "SettingsDialog.h"
#include <QCoreApplication>
#include <QMessageBox>
#include <QDebug>
#include <QPushButton>
#include <QTimer>
#include <QThread>
#include <QMetaObject>

namespace InstaMAT2Remix {

    namespace {
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
    }

    GuiManager::GuiManager(RemixConnector* connector, QObject* parent)
        : QObject(parent), m_connector(connector), m_remixMenu(nullptr) {
    }

    GuiManager::~GuiManager() {
        Teardown();
        // IMPORTANT:
        // Menus are owned by the host window/menu bar. During shutdown Qt will delete them.
        // Calling deleteLater() here can race with host teardown and cause use-after-free.
        // We rely on Qt's parent ownership instead.
        m_remixMenu = nullptr;
    }

    void GuiManager::Initialize() {
        qDebug() << "[InstaMAT2Remix] Initializing GUI Manager...";

        // Fail-soft: if Qt isn't ready yet, don't crash the host.
        QCoreApplication* app = QCoreApplication::instance();
        if (!app) {
            qWarning() << "[InstaMAT2Remix] No QCoreApplication instance yet; skipping menu injection.";
            return;
        }

        // Ensure we run UI work on the main Qt thread.
        if (this->thread() != app->thread()) {
            this->moveToThread(app->thread());
        }

        // Schedule inject attempts on the UI thread (menu bar may appear late).
        QMetaObject::invokeMethod(
            this,
            [this]() {
                QTimer::singleShot(500, this, &GuiManager::InjectMenu);
                QTimer::singleShot(2000, this, &GuiManager::InjectMenu);
                QTimer::singleShot(5000, this, &GuiManager::InjectMenu);
                QTimer::singleShot(10000, this, &GuiManager::InjectMenu);
            },
            Qt::QueuedConnection);
    }

    QMainWindow* GuiManager::GetMainWindow() {
        // Log all top level widgets to help debugging
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

        if (bestWithMenus) return bestWithMenus;
        return visibleFallback;
    }

    void GuiManager::InjectMenu() {
        if (m_remixMenu) return; // Already injected

        QMainWindow* win = GetMainWindow();
        if (!win) {
            qWarning() << "[InstaMAT2Remix] Could not find QMainWindow yet. Will retry.";
            return;
        }

        QMenuBar* menuBar = win->findChild<QMenuBar*>();
        if (!menuBar) {
            qWarning() << "[InstaMAT2Remix] Window found but no QMenuBar yet; waiting for host to finish UI." << win->windowTitle();
            return;
        }
        if (menuBar->actions().isEmpty()) {
            qWarning() << "[InstaMAT2Remix] MenuBar exists but host menus not ready; will retry.";
            return;
        }

        // Prefer an existing "RTX Remix" menu if another plugin (or the host) already created one.
        QMenu* rootMenu = nullptr;
        bool hadExistingRoot = false;
        for (QAction* a : menuBar->actions()) {
            if (!a) continue;
            const QString text = NormalizeActionText(a->text());
            if (text.compare("RTX Remix", Qt::CaseInsensitive) == 0 && a->menu()) {
                rootMenu = a->menu();
                hadExistingRoot = true;
                break;
            }
        }

        if (!rootMenu) {
            // Insert next to the standard top toolbar menus (File/Edit/Help).
            // If a Help menu exists, place RTX Remix right before it; otherwise append.
            QAction* helpAction = nullptr;
            for (QAction* a : menuBar->actions()) {
                if (!a) continue;
                const QString text = NormalizeActionText(a->text());
                if (text.compare("Help", Qt::CaseInsensitive) == 0) {
                    helpAction = a;
                    break;
                }
            }

            QMenu* newMenu = new QMenu("RTX Remix", menuBar);
            if (helpAction) {
                menuBar->insertMenu(helpAction, newMenu);
            } else {
                menuBar->addMenu(newMenu);
            }
            rootMenu = newMenu;
            menuBar->repaint();
        }

        // If the root menu already existed, put our actions into a dedicated submenu to avoid collisions.
        if (hadExistingRoot) {
            QMenu* subMenu = nullptr;
            for (QAction* a : rootMenu->actions()) {
                if (!a) continue;
                if (a->text().compare(kPluginName, Qt::CaseInsensitive) == 0 && a->menu()) {
                    subMenu = a->menu();
                    break;
                }
            }
            if (!subMenu) subMenu = rootMenu->addMenu(kPluginName);
            m_remixMenu = subMenu;
        } else {
            m_remixMenu = rootMenu;
        }

        auto ensureAction = [&](const QString& text, auto slot) {
            for (QAction* a : m_remixMenu->actions()) {
                if (!a) continue;
                if (a->text().compare(text, Qt::CaseInsensitive) == 0) return a;
            }
            QAction* a = m_remixMenu->addAction(text);
            connect(a, &QAction::triggered, this, slot);
            return a;
        };

        ensureAction("Pull from Remix", &GuiManager::onPullFromRemix);
        ensureAction("Import Textures from Remix", &GuiManager::onImportTexturesFromRemix);
        ensureAction("Push to Remix", &GuiManager::onPushToRemix);
        ensureAction("Force Push to Remix", &GuiManager::onForcePushToRemix);

        m_remixMenu->addSeparator();
        ensureAction("Settings", &GuiManager::onSettings);
        ensureAction("Diagnostics", &GuiManager::onDiagnostics);
        ensureAction("About", &GuiManager::onAbout);
        
        qDebug() << "[InstaMAT2Remix] SUCCESS: Menu injected into" << win->windowTitle();
    }

    void GuiManager::Teardown() {
        if (!m_remixMenu) return;
        if (QAction* act = m_remixMenu->menuAction()) {
            if (auto parentMenu = qobject_cast<QMenu*>(m_remixMenu->parentWidget())) {
                parentMenu->removeAction(act);
            } else if (auto parentBar = qobject_cast<QMenuBar*>(m_remixMenu->parentWidget())) {
                parentBar->removeAction(act);
            }
        }
        m_remixMenu->clear();
        m_remixMenu = nullptr;
    }

    void GuiManager::onPullFromRemix() {
        if (!m_connector) return;
        QSettings settings("InstaMAT2Remix", "Config");
        bool autoUnwrap = settings.value("AutoUnwrap", false).toBool();

        // Let the user choose tiling vs non-tiling at click-time (mirrors Substance2Remix UX).
        QMessageBox box(GetMainWindow());
        box.setIcon(QMessageBox::Question);
        box.setWindowTitle(kPluginName);
        box.setText("Pull from RTX Remix and create a new InstaMAT texturing project.\n\nChoose how you want to create the project:");
        auto* withoutTiling = box.addButton("Without tiling (use selected mesh)", QMessageBox::AcceptRole);
        auto* withTiling = box.addButton("With tiling (use tiling plane mesh)", QMessageBox::AcceptRole);
        auto* cancel = box.addButton(QMessageBox::Cancel);
        box.setDefaultButton(withoutTiling);
        box.exec();
        if (box.clickedButton() == cancel) return;

        const bool tiling = (box.clickedButton() == withTiling);
        m_connector->PullFromRemix(autoUnwrap, tiling ? RemixConnector::PullMeshMode::TilingMesh
                                                     : RemixConnector::PullMeshMode::SelectedMesh);
    }

    void GuiManager::onImportTexturesFromRemix() {
        if (!m_connector) return;
        m_connector->ImportTexturesFromRemix();
    }

    void GuiManager::onPushToRemix() {
        if (!m_connector) return;
        m_connector->PushToRemix(false);
    }

    void GuiManager::onForcePushToRemix() {
        if (!m_connector) return;
        m_connector->PushToRemix(true);
    }

    void GuiManager::onSettings() {
        if (!m_connector) return;
        SettingsDialog dlg(m_connector, GetMainWindow());
        dlg.exec();
    }

    void GuiManager::onDiagnostics() {
        if (!m_connector) return;
        const QString report = m_connector->BuildDiagnosticsReport();
        DiagnosticsDialog dlg(report, GetMainWindow());
        dlg.exec();
    }

    void GuiManager::onAbout() {
        QMessageBox::information(
            GetMainWindow(),
            kPluginName,
            QString("<b>%1</b><br/>v%2<br/><br/>%3")
                .arg(kPluginName)
                .arg(kPluginVersion)
                .arg(kPluginDescription));
    }
}

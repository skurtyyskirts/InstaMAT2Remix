#include "InstaMATAPI.h"
#include "PluginInfo.h"
#include "RemixConnector.h"
#include "SettingsDialog.h"
#include "DiagnosticsDialog.h"

#include <QApplication>
#include <QMainWindow>
#include <QMessageBox>
#include <QSettings>

#include <memory>
#include <mutex>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace {

std::mutex g_mutex;

QMainWindow* FindMainWindow() {
    if (QWidget* active = QApplication::activeWindow()) {
        if (auto* win = qobject_cast<QMainWindow*>(active)) return win;
    }

    const auto widgets = QApplication::topLevelWidgets();
    for (QWidget* widget : widgets) {
        if (auto* win = qobject_cast<QMainWindow*>(widget)) {
            if (win->isVisible() && win->windowTitle().contains("InstaMAT", Qt::CaseInsensitive)) {
                return win;
            }
        }
    }

    for (QWidget* widget : widgets) {
        if (auto* win = qobject_cast<QMainWindow*>(widget)) {
            if (win->isVisible()) return win;
        }
    }

    return nullptr;
}

InstaMAT2Remix::RemixConnector* GetConnector(QString& outError) {
    std::lock_guard<std::mutex> lock(g_mutex);

    if (auto* instance = InstaMAT2Remix::RemixConnector::GetInstance()) {
        return instance;
    }

    // Fallback: This path should ideally not be taken if PluginMain initializes correctly.
    // However, if we are running in a context where PluginMain hasn't run yet (unlikely for a plugin),
    // we return nullptr to avoid creating a second instance or crashing.
    outError = "Connector instance not initialized. Ensure the plugin is loaded by InstaMAT.";
    return nullptr;
}

void ShowHostError(const QString& msg) {
    QMessageBox::critical(FindMainWindow(), InstaMAT2Remix::kPluginName, msg);
}

bool PullFromRemixWithMode(InstaMAT2Remix::RemixConnector::PullMeshMode mode) {
    QString err;
    auto* connector = GetConnector(err);
    if (!connector) {
        ShowHostError(err);
        return false;
    }

    connector->ReloadSettings();

    QSettings settings("InstaMAT2Remix", "Config");
    const bool autoUnwrap = settings.value("AutoUnwrap", false).toBool();

    connector->PullFromRemix(autoUnwrap, mode);
    return true;
}

} // namespace

#if defined(_WIN32)
#define IM2R_EXPORT extern "C" __declspec(dllexport)
#else
#define IM2R_EXPORT extern "C"
#endif

IM2R_EXPORT bool PullFromRemix() {
    // When called from external/Python bridge, respect settings (no interactive prompts here).
    return PullFromRemixWithMode(InstaMAT2Remix::RemixConnector::PullMeshMode::UseSettings);
}

IM2R_EXPORT bool PullFromRemixSelectedMesh() {
    return PullFromRemixWithMode(InstaMAT2Remix::RemixConnector::PullMeshMode::SelectedMesh);
}

IM2R_EXPORT bool PullFromRemixTilingMesh() {
    return PullFromRemixWithMode(InstaMAT2Remix::RemixConnector::PullMeshMode::TilingMesh);
}

IM2R_EXPORT bool EnsureInitialized() {
    QString err;
    auto* connector = GetConnector(err);
    if (!connector) return false;
    connector->ReloadSettings();
    return true;
}

IM2R_EXPORT bool ImportTextures() {
    QString err;
    auto* connector = GetConnector(err);
    if (!connector) {
        ShowHostError(err);
        return false;
    }

    connector->ReloadSettings();
    connector->ImportTexturesFromRemix();
    return true;
}

IM2R_EXPORT bool PushToRemix() {
    QString err;
    auto* connector = GetConnector(err);
    if (!connector) {
        ShowHostError(err);
        return false;
    }

    connector->ReloadSettings();
    connector->PushToRemix(false);
    return true;
}

IM2R_EXPORT bool ForcePushToRemix() {
    QString err;
    auto* connector = GetConnector(err);
    if (!connector) {
        ShowHostError(err);
        return false;
    }

    connector->ReloadSettings();
    connector->PushToRemix(true);
    return true;
}

IM2R_EXPORT bool OpenSettingsDialog() {
    QString err;
    auto* connector = GetConnector(err);
    if (!connector) {
        ShowHostError(err);
        return false;
    }

    connector->ReloadSettings();
    InstaMAT2Remix::SettingsDialog dlg(connector, FindMainWindow());
    dlg.exec();
    return true;
}

IM2R_EXPORT bool OpenDiagnosticsDialog() {
    QString err;
    auto* connector = GetConnector(err);
    if (!connector) {
        ShowHostError(err);
        return false;
    }

    const QString report = connector->BuildDiagnosticsReport();
    InstaMAT2Remix::DiagnosticsDialog dlg(report, FindMainWindow());
    dlg.exec();
    return true;
}



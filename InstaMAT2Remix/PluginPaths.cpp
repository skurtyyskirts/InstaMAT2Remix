#include "PluginPaths.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QStringList>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace InstaMAT2Remix {

QString GetPluginDirPath() {
#if defined(_WIN32)
    HMODULE hModule = nullptr;
    const BOOL ok = GetModuleHandleExW(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&GetPluginDirPath),
        &hModule);

    if (ok && hModule) {
        wchar_t path[MAX_PATH] = {};
        const DWORD len = GetModuleFileNameW(hModule, path, MAX_PATH);
        if (len > 0) {
            const QString dllPath = QString::fromWCharArray(path, int(len));
            return QFileInfo(dllPath).absolutePath();
        }
    }
#endif

    // Fallback: host application directory.
    return QCoreApplication::applicationDirPath();
}

// The plugin DLL is usually extracted to a temp dir (.IMP loader) where
// bundled tools don't ship, so probe the install locations the packaging
// script writes to (Documents\InstaMAT\Plugins, %APPDATA%, and
// %ProgramFiles%\InstaMAT Studio\Environment).
static QString DetectBundledTool(const QString& fileName) {
    QStringList candidates;

    candidates << QDir(GetPluginDirPath()).filePath(fileName);

    const QString docs = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    if (!docs.isEmpty()) {
        candidates << QDir(docs).filePath("InstaMAT/Plugins/InstaMAT2Remix/" + fileName);
        candidates << QDir(docs).filePath("InstaMAT/Plugins/" + fileName);
    }

    const QString appdata = QString::fromLocal8Bit(qgetenv("APPDATA"));
    if (!appdata.isEmpty()) {
        candidates << QDir(appdata).filePath("InstaMAT Studio/Plugins/InstaMAT2Remix/" + fileName);
        candidates << QDir(appdata).filePath("InstaMAT Studio/Plugins/" + fileName);
    }

    const QString exeDir = QCoreApplication::applicationDirPath();
    candidates << QDir(exeDir).filePath("Environment/" + fileName);
    candidates << QDir(exeDir).filePath(fileName);

    for (const QString& p : candidates) {
        if (QFileInfo::exists(p)) return QDir::cleanPath(p);
    }
    return QString();
}

QString DetectTexconvPath() {
    return DetectBundledTool(QStringLiteral("texconv.exe"));
}

QString DetectExportWorkerPath() {
    return DetectBundledTool(QStringLiteral("InstaMAT2RemixExport.exe"));
}

} // namespace InstaMAT2Remix



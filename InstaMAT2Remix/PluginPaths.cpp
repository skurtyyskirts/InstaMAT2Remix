#include "PluginPaths.h"

#include <QCoreApplication>
#include <QFileInfo>

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

} // namespace InstaMAT2Remix



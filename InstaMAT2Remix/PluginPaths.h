#pragma once

#include <QString>

namespace InstaMAT2Remix {

// Returns the folder that contains this plugin DLL (best effort).
QString GetPluginDirPath();

// Returns the first texconv.exe found in known InstaMAT install locations
// (plugin dir, Documents\InstaMAT\Plugins, %APPDATA%\InstaMAT Studio\Plugins,
// %ProgramFiles%\InstaMAT Studio\Environment, host EXE dir). Empty if none.
QString DetectTexconvPath();

// Same search for InstaMAT2RemixExport.exe, the out-of-process layer-project
// exporter Push runs (executing a layer graph inside Studio's process hits a
// render-thread GL-affinity qFatal on Studio 3.1+). Empty if none.
QString DetectExportWorkerPath();

} // namespace InstaMAT2Remix



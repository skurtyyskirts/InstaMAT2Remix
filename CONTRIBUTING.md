# Contributing to InstaMAT2Remix

Thanks for your interest! Bug reports, reproductions, and pull requests are all
welcome. This is a **Windows-only C++/Qt6 plugin** built against the InstaMAT
Plugin SDK, so contributing code needs a local Windows toolchain.

## Reporting bugs

Please open an issue and include:

- The output of **RTX Remix Connector > Diagnostics… > Copy to Clipboard**.
- The relevant tail of `Documents\InstaMAT2Remix\logs\remix_connector.log`
  (the `ExportWorker:` and `Recipe:` lines are the most useful).
- Your InstaMAT Studio version, RTX Remix Toolkit version, and Windows version.

The issue form will prompt for these.

## Development setup

Toolchain: Visual Studio 2022 or 2026, Qt 6.5.3/6.6.3 (MSVC x64), CMake —
`build_plugin.ps1` auto-detects all three.

```powershell
cd InstaMAT2Remix
.\build_plugin.ps1              # build + dev-install (junction + user plugin dirs + .IMP)
.\build_plugin.ps1 -InstallMode None   # build only
```

Run the tests before submitting:

```powershell
cd InstaMAT2Remix
cmake -S . -B build_test -G "Visual Studio 18 2026" -A x64 -DCMAKE_PREFIX_PATH="C:/Qt/6.6.3/msvc2019_64"
cmake --build build_test --target TestRemixConnector --config Debug
ctest --test-dir build_test -C Debug -V
python -m pytest InstaMAT2Remix/tests/ -v     # from the repo root
```

> On machines with Visual Studio 18 / 2026 the CMake generator string **must**
> be `"Visual Studio 18 2026"` — `"Visual Studio 17 2022"` fails there.

## House rules (important)

These constraints are load-bearing — please keep them:

- **No mouse/keyboard event simulation** (`QMouseEvent`/`QKeyEvent`). The New
  Project automation identifies widgets by class name, not synthetic input.
- **Do not link `Qt6Quick.dll` / `Qt6Qml.dll`** — it breaks `.IMP` plugin
  loading from a temp directory.
- **Do not define `INSTAMAT_LIB_DYNAMIC`** — it marks SDK interfaces
  `dllimport` and breaks deriving from `IGraphMesh`.
- **Rendering must stay out-of-process** (`InstaMAT2RemixExport.exe`). An
  in-process `IElementExecution::Execute` from plugin code crashes Studio 3.1+
  via a GL-render-thread fail-fast that no SEH guard can catch.
- Keep user data under `%USERPROFILE%\Documents\InstaMAT2Remix\`, never inside
  the plugin install directory.

## Pull requests

- Build cleanly via `build_plugin.ps1`; C++ + Python tests pass.
- Update `CHANGELOG.md` (Unreleased section) and `docs/wbc_parity_audit.md` when
  behavior changes.
- Keep new code consistent with the surrounding style.

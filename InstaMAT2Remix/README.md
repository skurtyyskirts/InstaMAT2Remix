# InstaMAT2Remix — plugin source

This directory holds the C++/Qt6 source for the **RTX Remix Connector for
InstaMAT** plugin (built against the InstaMAT Plugin SDK).

- **Project overview, features, install, and usage:** see the
  [root README](../README.md).
- **End-user guide** (shipped in the release zip): `dist_template/README.md`.
- **Contributing / build notes:** [../CONTRIBUTING.md](../CONTRIBUTING.md).

## Build & test

```powershell
.\build_plugin.ps1                     # build + dev-install to this machine
.\build_plugin.ps1 -InstallMode None   # build only
.\package_release.ps1                  # produce dist\InstaMAT2Remix-v<ver>-win64.zip
```

Toolchain: Visual Studio 2022/2026, Qt 6.5.3/6.6.3 (MSVC x64), CMake — all
auto-detected by `build_plugin.ps1`. C++ tests build as the `TestRemixConnector`
target; Python bridge tests live in `tests/`.

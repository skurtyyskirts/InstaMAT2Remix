# RTX Remix Connector for InstaMAT

[![Build](https://github.com/skurtyyskirts/InstaMAT2Remix/actions/workflows/build.yml/badge.svg)](https://github.com/skurtyyskirts/InstaMAT2Remix/actions/workflows/build.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![Release](https://img.shields.io/github/v/release/skurtyyskirts/InstaMAT2Remix?include_prereleases&sort=semver)](https://github.com/skurtyyskirts/InstaMAT2Remix/releases)
![Platform](https://img.shields.io/badge/platform-Windows%20x64-lightgrey.svg)

**Pull a captured mesh from NVIDIA RTX Remix into a paint-ready InstaMAT
project, paint your PBR material, and push the finished textures straight back
— one click each way.**

InstaMAT2Remix is a C++/Qt6 plugin for [InstaMAT Studio](https://instamaterial.com)
that bridges it with the [NVIDIA RTX Remix Toolkit](https://github.com/NVIDIAGameWorks/rtx-remix).
It is the InstaMAT counterpart of the
[Substance2Remix](https://github.com/skurtyyskirts/Substance2Remix) plugin for
Adobe Substance Painter, and behaves 1:1 with it. Rendering happens in a
separate helper process, so a Push can never crash Studio.

> ⚠️ **Early alpha (v0.0.2-alpha).** It works, but expect rough edges and bugs.
> If something misbehaves, please open an issue with your
> **Diagnostics → Copy to Clipboard** output. Feedback is very welcome.

<!--
  Drop screenshots into docs/images/ and uncomment. A menu shot and a
  before/after make the biggest difference for a bridge tool.
  ![RTX Remix Connector menu](docs/images/menu.png)
-->

## Features

- **Pull From Remix** — resolves your current Remix selection and automatically
  creates a linked InstaMAT project from the mesh. Zero prompts. Optional
  simple tiling-plane substitute and optional Blender Smart-UV auto-unwrap.
- **Import Textures from Remix** — downloads the linked material's current
  textures (DDS→PNG via bundled `texconv`, canonical names) for reference/reuse.
- **Push To Remix** — auto-saves, renders your painted PBR outputs at the
  resolution you **baked** the project at, ingests them through Remix's
  ingestcraft pipeline, retargets the material, and saves the layer.
- **Force Push to Remix** — relinks to whatever is selected in Remix now and
  writes under a fresh, non-overwriting filename root.
- **Out-of-process render worker** (`InstaMAT2RemixExport.exe`) isolates
  rendering so it can never take down InstaMAT Studio.
- Settings (5 tabs), a one-click **Diagnostics** health report with
  copy-to-clipboard, and persistent file logging.

## Requirements

| Component | Requirement | Notes |
|---|---|---|
| OS | Windows 10/11 **x64** | |
| InstaMAT Studio | **3.x**, activated | Push renders through the same license |
| RTX Remix Toolkit | REST API on `http://localhost:8011`, a project open | default host/port |
| Blender | 3.6+ (optional) | auto-UV-unwrap on Pull; set path in Settings > Paths |
| Bundled | texconv, InstaMAT2RemixExport.exe, Qt runtime, MSVC runtime | shipped in the zip — nothing else to install |

## Install (from the release zip)

1. Download the latest `InstaMAT2Remix-v*-win64.zip` from
   [Releases](https://github.com/skurtyyskirts/InstaMAT2Remix/releases) and unzip it.
2. Close InstaMAT Studio if it's running.
3. Run the installer:
   `powershell -ExecutionPolicy Bypass -File .\install.ps1`
4. Start RTX Remix Toolkit (with a project open), then InstaMAT Studio.
5. An **RTX Remix Connector** menu appears in Studio's menu bar.

The zip is self-contained (Qt runtime, `texconv`, the render helper, and the
MSVC runtime are all included). The in-zip `README.md` has the full walkthrough
plus a manual, no-script install path. To remove the plugin later, run
`uninstall.ps1`.

## Usage

| Menu action | What it does |
|---|---|
| **Pull From Remix** | Reads your Remix selection, resolves its mesh + material, and auto-creates a linked InstaMAT project — choose **Asset Texturing** (default), **Element Graph**, or **Materialize Image** (the material's Remix texture is downloaded and loaded as the source image). "Remember my choice" restores zero-prompt pulls. |
| **Import Textures from Remix** | Downloads the linked material's textures (DDS→PNG, canonical names) into `Documents\InstaMAT2Remix\Pulled Textures\<project>\`, registers the folder, and opens Explorer. Drag each map onto the matching channel. |
| **Push To Remix** | Auto-saves → renders PBR outputs at the baked resolution → ingests → retargets the linked material → saves the Remix layer. Pushes **every texture type Remix supports** — incl. subsurface scattering (transmittance/thickness/single-scattering/radius) and anisotropy; opacity is merged into albedo's alpha; glass materials get only their supported channels. Renders out-of-process. |
| **Force Push to Remix** | Like Push, but relinks to the currently selected Remix material and writes under a fresh, non-overwriting filename root. |
| **Settings…** | Connection / Paths / Pull / Export / Advanced. |
| **Diagnostics…** | One-click health report (versions, path checks, link state, live connectivity), Copy to Clipboard. |
| **About…** | Plugin name, version, and repository link. |

### Typical session

1. In **Remix Toolkit**, select the mesh (or its material) you want to retexture.
2. **RTX Remix Connector > Pull From Remix** — an InstaMAT project opens with the mesh loaded.
3. Paint / drag library materials onto it. Set your bake resolution (e.g. 4K) for a high-res push.
4. **RTX Remix Connector > Push To Remix** — Push saves, renders at the baked resolution, and updates the material in Remix.

<!-- ![After Pull](docs/images/pull.png) · ![Push summary](docs/images/push.png) -->

## Known limitations

- **Intermittent 1×1 render collapse under GPU load.** The InstaMAT standalone
  renderer occasionally emits a 1×1 map for some channels, especially when
  another GPU-heavy app is running. The plugin automatically retries with fresh
  worker processes; if none renders clean it **stops the push and leaves your
  Remix textures untouched** — close other GPU apps (reboot if it persists)
  and Push again.
- **Single selected mesh only.** Multi-mesh material-group bundling on Pull
  (present in Substance2Remix) is not yet ported.
- **Import is reference-only.** InstaMAT's plugin SDK has no API to auto-assign
  textures into an open project, so imported maps must be dragged onto their
  channels manually.
- **Element Graph pushes read your graph output parameters** — name them after
  PBR channels (Base Color, Roughness, Normal, …). A single generically-named
  image output is pushed as Base Color with a note.
- **Live in-Studio smoke** (2026-07-12): Asset Texturing pull + full push,
  Element Graph pull + push (incl. the lone-output Base Color fallback), and
  Materialize Image pull + full push are user-verified end-to-end. Remaining
  paths (SSS/glass pushes, opacity-merge visual check) are still being
  validated by the community — please file an issue with your Diagnostics
  output if anything misbehaves.

## Building from source

```powershell
cd InstaMAT2Remix
.\build_plugin.ps1                     # build + dev-install to this machine
.\build_plugin.ps1 -InstallMode None   # build only
.\package_release.ps1                  # produce dist\InstaMAT2Remix-v<ver>-win64.zip
```

Toolchain: Visual Studio 2022 or 2026, Qt 6.5.3/6.6.3 (MSVC x64), CMake.
`build_plugin.ps1` auto-detects all three.

Tests:

```powershell
cd InstaMAT2Remix
cmake -S . -B build_test -G "Visual Studio 18 2026" -A x64 -DCMAKE_PREFIX_PATH="C:/Qt/6.6.3/msvc2019_64"
cmake --build build_test --target TestRemixConnector --config Debug
ctest --test-dir build_test -C Debug -V
python -m pytest InstaMAT2Remix/tests/ -v     # from the repo root
```

> Note: on machines with Visual Studio 18 / 2026 the CMake generator string
> **must** be `"Visual Studio 18 2026"` (`"Visual Studio 17 2022"` fails there).
> Qt 6.6.3/msvc2019_64 is ABI-compatible with both.

## Troubleshooting

- **Log file:** `Documents\InstaMAT2Remix\logs\remix_connector.log`.
- **RTX Remix Connector > Diagnostics…** gives a one-click health report
  (paths, settings, live Remix connectivity) with Copy to Clipboard.
- **Settings > Connection > Test Connection** verifies the Remix REST API.
- **"Push stopped to protect your Remix textures"** — see *Known limitations*;
  close GPU-heavy apps and Push again. Nothing in Remix was changed.
- **"Live export failed"** — check the `ExportWorker:` lines in the log and
  confirm InstaMAT Studio is activated (the helper renders through the license).

## Repository layout

| Path | What |
|---|---|
| `InstaMAT2Remix/` | Plugin source (C++/Qt6 against the InstaMAT Plugin SDK) |
| `InstaMAT2Remix/ExportWorker.cpp` | `InstaMAT2RemixExport.exe`, the out-of-process render worker |
| `InstaMAT2Remix/tools/imp_packaging.ps1` | `.IMP` (MAT container) package builder |
| `InstaMAT2Remix/dist_template/` | End-user installer + docs staged into the release zip |
| `docs/wbc_parity_audit.md` | Feature-parity tracker vs Substance2Remix |
| `CONTRIBUTING.md` · `CHANGELOG.md` | Contributor guide · release history |

## License

MIT — see [LICENSE](LICENSE). Third-party components (texconv/MIT, Qt/LGPL-3.0,
the Microsoft Visual C++ runtime, and the InstaMAT Plugin SDK) are documented in
[THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md).

## Credits

- Behavioral inspiration: [Substance2Remix](https://github.com/skurtyyskirts/Substance2Remix)
  / WholeBodyCapture (WBC) for Adobe Substance Painter.
- Built on the InstaMAT Plugin SDK (© InstaMaterial GmbH).
- Interfaces with the NVIDIA RTX Remix Toolkit REST API.
- Bundles `texconv.exe` from [Microsoft DirectXTex](https://github.com/microsoft/DirectXTex) (MIT).
- Qt 6 used under LGPL-3.0. Ships the Microsoft Visual C++ runtime.

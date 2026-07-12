# Changelog

All notable changes to InstaMAT2Remix are documented in this file. The format
is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this
project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

_Nothing yet._

## [0.0.2-alpha] - 2026-07-12

Second alpha. Pull grows a project-template chooser, Push covers the full RTX
Remix PBR input set (and now works from all three project templates), and two
user-reported bugs are fixed: the Element Graph push failure and the garbled
text in the Pull chooser.

### Added

- **Pull project templates**: choose **Asset Texturing** (default), **Element
  Graph**, or **Materialize Image** on every Pull — with "remember my choice"
  and a Settings > Pull > Project Template override. Element Graph uses a
  pickerless auto-create recipe; Materialize Image downloads the material's
  current texture and feeds it to the wizard automatically.
- **Full Remix PBR push set**: subsurface scattering (transmittance /
  thickness / single-scattering / radius) and anisotropy channels; translucent
  (glass) target materials are detected and receive only the channels they
  consume; opacity is merged into the albedo alpha (Remix reads opacity from
  there); a Normal-map encoding setting (DirectX / OpenGL / Octahedral).
- **Push from any template**: the render worker now executes layer, element,
  or materialize graphs (previously layer projects only).
- **Element Graph push conveniences**: a graph whose single image output has a
  generic name (e.g. "Output") is pushed as the Base Color map with a summary
  note — matching how InstaMAT's own Blender add-on treats generic outputs.
  Output names "Output"/"Default", "Metal" and "Transparency" now map to
  albedo, metallic and opacity respectively.
- Blender auto-unwrap now imports OBJ, USD, FBX and glTF meshes and tolerates
  both old and new Blender `smart_project` APIs.

### Fixed

- **Element Graph push failed with "no recognized PBR channel outputs"**: the
  worker now reports every output it saw (`OUTPUTCOUNT`/`OUTPUTSKIP`), tells
  you exactly which names it found and what to rename them to, and the plugin
  fails fast (one worker spawn instead of three, saving ~24 s) on this and
  other deterministic errors via the new `FATAL=` protocol line.
- **Garbled text in the Pull template chooser** ("â " instead of an em-dash):
  config strings are now decoded as UTF-8, and all targets compile with MSVC
  `/utf-8` so text stops depending on the system codepage.
- **Push-failure dialog polish**: no more doubled period, clearer layout, and
  an actionable "name your outputs after PBR channels" tip when that is the fix.
- **Empty Export Folder setting no longer wipes the working directory**: every
  recursive delete is gated by the unit-tested `IsSafeToWipe` (refuses empty/
  relative paths, drive roots, and well-known user folders).
- Menu clicks during a running Pull/Import/Push no longer interleave flows
  (reentrancy guard).
- Materialize Image pull no longer fails intermittently with "Asset … not
  found in asset list": the picker now waits for Studio's asynchronous asset
  scan and re-checks the list before giving up.
- Installer: resolves OneDrive-redirected Documents, warns when Studio is
  running, and keeps its window open; packaging finds the VS 2026 (VC145)
  runtime for app-local bundling.

### Changed

- **Render-collapse handling is now a hard guard**: when no clean render can be
  obtained after fresh-worker retries and step-downs, the push **aborts** with
  nothing ingested and Remix untouched (0.0.1 pushed the degraded textures and
  only warned in the summary).

## [0.0.1-alpha] - 2026-07-07

First public **alpha** release — the InstaMAT counterpart of the Substance2Remix
plugin, bridging InstaMAT Studio with the NVIDIA RTX Remix Toolkit. Early build:
works end-to-end, but expect rough edges — please report issues.

### Added

- **Out-of-process render worker** (`InstaMAT2RemixExport.exe`): Push renders in
  a separate process, so a rendering failure can no longer crash InstaMAT Studio.
  (Resolves the Studio 3.1+ GL-render-thread fail-fast that killed Studio on an
  in-process `Execute`.)
- **Pull From Remix**: zero-prompt resolution of the current Remix selection with
  automatic InstaMAT project creation from the mesh and persistent link state.
  Optional simple tiling-plane substitute and optional Blender Smart-UV auto-unwrap.
- **Import Textures from Remix**: downloads the linked material's textures with
  DDS→PNG conversion (bundled `texconv`) and canonical names, and registers the
  folder as an external asset folder.
- **Push To Remix**: auto-saves the project, renders PBR outputs at the resolution
  the project was **baked** at, ingests via Remix ingestcraft, retargets the
  material (`PUT /stagecraft/textures/`), and saves the layer. Export failure
  fails the push cleanly.
- **Force Push to Remix**: relinks to the currently selected Remix material and
  writes under a non-overwriting filename root.
- **Render-collapse mitigation**: on the intermittent standalone-renderer 1×1
  glitch, the plugin automatically respawns fresh worker processes (with
  resolution step-downs) and warns in the Push summary if a clean render can't
  be obtained.
- **Settings** dialog (Connection / Paths / Pull / Export / Advanced),
  **Diagnostics** health report with copy-to-clipboard, an **About** dialog, and
  persistent file logging at `Documents/InstaMAT2Remix/logs/remix_connector.log`.
- **Self-contained release packaging** (`package_release.ps1`) bundling the
  plugin DLL, the Qt runtime, `texconv`, the export worker, the Microsoft Visual
  C++ runtime (app-local), the generated `.IMP` package, and install/uninstall
  scripts. Optional env-gated Python bridge.

### Known limitations

- Intermittent 1×1 render collapse under GPU contention (mitigated by
  fresh-worker respawn + a Push-summary warning).
- Single selected mesh only — multi-mesh material-group bundling is not yet ported.
- Imported textures must be dragged onto their channels manually (the SDK has no
  auto-assign API).

[Unreleased]: https://github.com/skurtyyskirts/InstaMAT2Remix/compare/v0.0.2-alpha...HEAD
[0.0.2-alpha]: https://github.com/skurtyyskirts/InstaMAT2Remix/releases/tag/v0.0.2-alpha
[0.0.1-alpha]: https://github.com/skurtyyskirts/InstaMAT2Remix/releases/tag/v0.0.1-alpha

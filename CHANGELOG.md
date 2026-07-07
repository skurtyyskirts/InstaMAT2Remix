# Changelog

All notable changes to InstaMAT2Remix are documented in this file. The format
is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this
project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

_Nothing yet._

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

[Unreleased]: https://github.com/skurtyyskirts/InstaMAT2Remix/compare/v0.0.1-alpha...HEAD
[0.0.1-alpha]: https://github.com/skurtyyskirts/InstaMAT2Remix/releases/tag/v0.0.1-alpha

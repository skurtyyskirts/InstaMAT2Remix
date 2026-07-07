# InstaMAT2Remix v0.0.1-alpha

> ⚠️ **Early alpha — first public release.** This build works end-to-end but is
> new; expect rough edges and bugs. Please file issues with your
> **Diagnostics → Copy to Clipboard** output — feedback shapes the next release.

The **RTX Remix Connector for InstaMAT** — pull a captured mesh from NVIDIA RTX
Remix into a paint-ready InstaMAT project, paint your PBR material, and push the
finished textures straight back. It's the InstaMAT counterpart of the
Substance2Remix plugin for Substance Painter.

## Highlights

- **Pull From Remix** — one click resolves your Remix selection and auto-creates
  a linked InstaMAT project from the mesh (optional tiling-plane + Blender
  auto-unwrap).
- **Import Textures from Remix** — pulls the material's textures (DDS→PNG) for
  reference.
- **Push / Force Push To Remix** — auto-saves, renders your PBR outputs at the
  resolution you baked at, ingests, retargets the material, and saves the layer.
- **Crash-proof rendering** — Push renders in a separate helper process, so it
  can never take down InstaMAT Studio.
- Settings, a one-click Diagnostics health report, and persistent logging.

## Install

1. Download `InstaMAT2Remix-v0.0.1-alpha-win64.zip` below and unzip it.
2. Close InstaMAT Studio.
3. Run: `powershell -ExecutionPolicy Bypass -File .\install.ps1`
4. Start RTX Remix Toolkit (with a project open), then InstaMAT Studio.
5. Use the **RTX Remix Connector** menu. (Uninstall with `uninstall.ps1`.)

The zip is self-contained — Qt runtime, `texconv`, the render helper, and the
Microsoft Visual C++ runtime are all bundled. Nothing else to install.

## Requirements

- Windows 10/11 x64
- InstaMAT Studio 3.x (activated)
- NVIDIA RTX Remix Toolkit with its REST API on `http://localhost:8011` and a
  project open
- Optional: Blender 3.6+ for auto-UV-unwrap on Pull

## Known limitations

- Some channels can intermittently render at 1×1 when the GPU is busy; the
  plugin retries automatically and warns you in the Push summary — close
  GPU-heavy apps (reboot if needed) and Push again.
- Single selected mesh only (multi-mesh bundling not yet ported).
- Imported textures must be dragged onto their channels manually.

Please file issues with your **Diagnostics → Copy to Clipboard** output.
Licensed under MIT.

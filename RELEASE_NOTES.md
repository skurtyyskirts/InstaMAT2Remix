# InstaMAT2Remix v0.0.2-alpha

> ⚠️ **Early alpha.** This build works end-to-end but is new; expect rough
> edges and bugs. Please file issues with your
> **Diagnostics → Copy to Clipboard** output — feedback shapes the next release.

The **RTX Remix Connector for InstaMAT** — pull a captured mesh from NVIDIA RTX
Remix into a paint-ready InstaMAT project, paint your PBR material, and push the
finished textures straight back. It's the InstaMAT counterpart of the
Substance2Remix plugin for Substance Painter.

## What's new in 0.0.2-alpha

- **Pull project templates** — choose **Asset Texturing** (default), **Element
  Graph**, or **Materialize Image** when pulling, with "remember my choice" and
  a Settings override. Materialize Image auto-downloads the material's current
  texture into the wizard.
- **Full Remix PBR push set** — subsurface scattering + anisotropy channels,
  translucent (glass) material routing, opacity merged into albedo alpha, and a
  Normal-map encoding setting.
- **Push works from all three templates** — the render worker now executes
  layer, element, and materialize graphs.
- **Element Graph push fixed** — pushes no longer fail silently on generic
  output names: a lone unnamed image output is pushed as Base Color (with a
  note), and real naming problems produce one fast, actionable error telling
  you exactly which outputs it saw and what to rename them to.
- **Fixed garbled text** in the Pull template chooser.
- **Safer than 0.0.1** — a render that cannot come out clean now aborts the
  push with Remix untouched; the Export Folder can never wipe an unintended
  directory; menu double-clicks can't interleave operations.

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

1. Download `InstaMAT2Remix-v0.0.2-alpha-win64.zip` below and unzip it.
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
  plugin retries with fresh helper processes and **aborts the push** (Remix
  untouched) if no clean render is possible — close GPU-heavy apps (reboot if
  needed) and Push again.
- Element Graph pushes read your **graph output parameters**: name them after
  PBR channels (Base Color, Roughness, Metallic, Normal, Height, Emissive,
  Opacity…). A single generically-named image output is pushed as Base Color.
- Single selected mesh only (multi-mesh bundling not yet ported).
- Imported textures must be dragged onto their channels manually.

Please file issues with your **Diagnostics → Copy to Clipboard** output.
Licensed under MIT.

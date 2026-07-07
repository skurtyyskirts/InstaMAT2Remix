# WBC ↔ InstaMAT2Remix feature-parity audit

Re-baselined **2026-07-06** against the v0.0.1-alpha parity work (zero-prompt Pull,
live-export Push via the out-of-process `InstaMAT2RemixExport.exe` worker,
Export settings tab, Force Push root renaming, release packaging). The
reference plugin is
WholeBodyCapture ("Substance2Remix") for Adobe Substance Painter at
`c:\Users\skurtyy\Documents\Adobe\Adobe Substance 3D Painter\python\plugins\WholeBodyCapture\`
(reference-only — never modified).

Legend: ✅ parity | 🟨 parity with an intentional platform deviation | ➕ InstaMAT-only superset

## Menu surface

| WBC action | InstaMAT2Remix | Status |
|---|---|---|
| Pull From Remix | Pull From Remix (`GuiManager.cpp`) | ✅ exact label/order |
| Import Textures from Remix | Import Textures from Remix | ✅ |
| Push To Remix | Push To Remix | ✅ |
| Force Push to Remix | Force Push to Remix | ✅ |
| Settings... / Diagnostics... / About... | same | ✅ |
| *(none)* | menu separator before Settings... | ➕ cosmetic |
| *(WBC has no Relink item)* | *(standalone Relink Material removed)* | ✅ |

Menu title: WBC uses PLUGIN_NAME ("RTX Remix Connector"); InstaMAT2Remix now
titles its top-level menu the same.

## Pull From Remix

| Behavior | WBC | InstaMAT2Remix | Status |
|---|---|---|---|
| Zero prompts | ✅ | ✅ (click-time tiling chooser removed) | ✅ |
| Resolve selection → mesh + material | `/stagecraft/assets/` + `/material` + `/file-paths` | same endpoints (`GetSelectedRemixAssetDetails`) | ✅ |
| Simple-tiling-mesh option | setting | `UseTilingMeshOnPull` setting + bundled `plane_tiling.usd` | ✅ |
| Blender Smart-UV auto-unwrap | setting, background | `AutoUnwrap` setting, headless Blender | ✅ |
| Auto-create project from mesh | `substance_painter.project.create()` | UI-automation recipe (`RunNewProjectRecipe`) — the InstaMAT SDK has **no** project-creation API | 🟨 |
| Link material to project | Painter project metadata | QSettings `LinkedMaterialPrim`/`LinkedMeshPath` — the SDK has no per-project metadata API (link is global, last-pull-wins) | 🟨 |
| No texture download on Pull | ✅ | ✅ (mesh-only; Import Textures is the download path) | ✅ |
| Completion message | toast "Project created and linked…" | summary dialog with the same wording + Import hint | ✅ |
| Multi-mesh material-group bundling (wrapper .usda) | ✅ | ❌ not ported (single selected mesh only) | deviation — candidate for a future release |
| Wrong-project-type note | n/a (Painter has no equivalent failure mode) | warns in log + Pull summary when the Asset Texturing tile couldn't be confirmed | ➕ |

## Import Textures from Remix

| Behavior | WBC | InstaMAT2Remix | Status |
|---|---|---|---|
| Fetch material textures + DDS→PNG (texconv) | ✅ | ✅ | ✅ |
| Destination | `PLUGIN_DIR/Pulled Textures/<project>/` | `Documents/InstaMAT2Remix/Pulled Textures/<project>/` | ✅ (relocated to the plugin's user-data root) |
| Auto-assign into the open project's channels | ✅ (Painter API) | ❌ impossible — the InstaMAT SDK has no channel-assignment or active-project API. Instead: canonical filenames (`albedo.png`, …), Asset Browser registration, Explorer open, and a summary explaining the manual drag | 🟨 |

## Push To Remix

| Behavior | WBC | InstaMAT2Remix | Status |
|---|---|---|---|
| Export painted maps before ingest | `export_project_textures` to `%TEMP%/RemixConnector_Export` | SDK render of the saved layer project to `ExportFolder` (default `%TEMP%/InstaMAT2Remix_Export`) in the out-of-process `InstaMAT2RemixExport.exe` worker (`ExportActiveLayeringProject` spawns it; in-process `Execute` fail-fasts on Studio 3.1+ — GL render-thread affinity qFatal) | ✅ |
| Export failure fails the push (no stale fallback) | ✅ | ✅ (actionable dialog; nothing ingested) | ✅ |
| Requires saved state | Painter exports the live document | InstaMAT SDK can only execute a saved package → newest Library `.IMP`; when >60 s stale the plugin **auto-saves** (drives File > Save) and re-polls, falling back to a manual Ctrl+S/Retry dialog only if no Save action is reachable | 🟨 |
| Filenames `<materialHash>_<pbr>.<ext>` | ✅ | ✅ (`DeriveDesiredRootFromPrim` at the staging step) | ✅ |
| Ingest POST `/ingestcraft/mass-validator/queue/material`, 600 s, 1 attempt | ✅ | ✅ | ✅ |
| `PUT /stagecraft/textures/` `{force:true}` + layer save | ✅ | ✅ | ✅ |
| Opacity gated by setting | `include_opacity_map` | `IncludeOpacityMap` | ✅ |
| Baked-size export (Auto) | exports the project's working resolution | `ExportResolution` Auto → worker renders at the project's **baked** resolution, read from the `.IMP`'s `BakeSettings` JSON (`ReadBakeResolutionFromImp`) — e.g. a 4K bake pushes 4K. NOT the original Remix DDS dims (a Remix capture can be tiny, 66×66 — the "pushed the pulled size" bug) | ✅ |
| Aspect restore for fixed resolutions | texconv resize | aspect-corrected `SetFormat` (`RestoreAspectOnExport`) | ✅ |

## Force Push to Remix

| Behavior | WBC | InstaMAT2Remix | Status |
|---|---|---|---|
| Silent relink to current Remix selection (persisted) | ✅ | ✅ (no confirm dialog) | ✅ |
| Non-overwriting root `<hash>_1/_2/…` (boundary-aware .dds scan) | ✅ | ✅ (`ChooseNonOverwritingRoot`, unit-tested) | ✅ |

## Settings

Tabs (both): Connection / Paths / Pull / Export / Advanced. Storage: WBC uses
`settings.json`; InstaMAT2Remix uses `QSettings("InstaMAT2Remix","Config")`
(HKCU registry) — 🟨 platform-conventional deviation.

| WBC key (default) | InstaMAT2Remix key (default) |
|---|---|
| `api_base_url` (`http://localhost:8011`) | `RemixApiBaseUrl` (same) |
| `poll_timeout` (60) | `PollTimeoutSec` (60) |
| `log_level` (info) | `LogLevel` (info) |
| `use_simple_tiling_mesh_on_pull` (false) | `UseTilingMeshOnPull` (false) |
| `simple_tiling_mesh_path` | `TilingMeshPath` (auto-detected bundled plane) |
| `auto_unwrap_with_blender_on_pull` (false) | `AutoUnwrap` (false) |
| `blender_executable_path` | `BlenderPath` |
| `blender_smart_uv_*` (66 / 0.003 / 0 / false) | `BlenderSmartUV*` (same) |
| `painter_export_path` (`%TEMP%/RemixConnector_Export`) | `ExportFolder` (`%TEMP%/InstaMAT2Remix_Export`) |
| `export_file_format` (png/tga/jpg) | `ExportFileFormat` (png/tga/jpg — native WritePNG/TGA/JPEG) |
| `include_opacity_map` (false) | `IncludeOpacityMap` (false) |
| `restore_original_aspect_ratio_on_export` (true) | `RestoreAspectOnExport` (true) |
| `remix_output_subfolder` (`Textures/PainterConnector_Ingested`) | `RemixOutputSubfolder` (`Textures/InstaMAT2Remix_Ingested`) |
| `texconv_path` (bundled) | `TexconvPath` (bundled + multi-location auto-detect) |
| `blender_unwrap_script_path` / `blender_unwrap_output_suffix` | ❌ not ported — the unwrap script is embedded; suffix is internal (no user demand) |
| `painter_import_template_path` | ❌ N/A — Painter template concept doesn't exist in InstaMAT |
| *(none)* | `ExportResolution` (Auto/512/1024/2048/4096) — ➕ InstaMAT needs an explicit render size |

Both dialogs: Test Connection button, log path + Open Folder, Reset to
Defaults / Cancel / OK. InstaMAT2Remix adds a remote-HTTP security warning ➕.

## Diagnostics / About / logging

| Behavior | WBC | InstaMAT2Remix | Status |
|---|---|---|---|
| Read-only report + Copy to Clipboard | ✅ | ✅ | ✅ |
| Path validity OK/MISSING | ✅ | ✅ (texconv/Blender/tiling mesh) | ✅ |
| Ping / connectivity check | ✅ | ✅ | ✅ |
| Link state + repo URL | ✅ | ✅ | ✅ |
| About with name/version/description/repo | ✅ | ✅ | ✅ |
| Persistent log file | `PLUGIN_DIR/logs/remix_connector.log` | `Documents/InstaMAT2Remix/logs/remix_connector.log` | ✅ |

## Async / progress model

WBC runs long ops on a QThreadPool with an indeterminate progress dialog
(cancel = "Hide"). InstaMAT2Remix runs synchronously on the UI thread with a
modal `QProgressDialog` (real Cancel where safe) — 🟨 deliberate: the InstaMAT
SDK requires execution on the thread that initialized it, and Pull's UI
automation must run on the UI thread anyway.

## InstaMAT-only superset

- RTX Remix Element node (graph-triggerable Pull/Import), C-ABI exports,
  optional Python bridge (`rtx_remix_connector.py`, env-gated)
- QtTest suite (16 cases) + pytest suite (5) + CI
- Release packaging: `package_release.ps1` → installable zip with
  `install.ps1`/`uninstall.ps1`, self-contained `.IMP` generation
  (`tools/imp_packaging.ps1` — no template file needed)
- Out-of-process export worker (Studio can never be crashed by a failed
  export — the layer graph executes in `InstaMAT2RemixExport.exe`, which
  hosts the SDK itself and binds the linked mesh by raw bytes)

## Known intentional deviations (summary)

1. **No in-project texture auto-assign** (Import) — SDK limitation, documented in the summary dialog.
2. **Link state is global** (QSettings), not per-project — SDK has no project metadata.
3. **Push reads the last saved state** (Ctrl+S + Retry dialog) — SDK cannot execute the live document.
4. **No multi-mesh material-group bundling on Pull** — future work.
5. **QSettings instead of settings.json**; progress is synchronous-modal.

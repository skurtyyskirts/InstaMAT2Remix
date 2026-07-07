# InstaMAT2Remix — LLM Assistant Bootstrap

C++/Qt6 plugin for InstaMAT Studio that bridges it with NVIDIA RTX Remix
Toolkit, behaving 1:1 with the user's Substance Painter plugin
"Substance2Remix" (WholeBodyCapture, WBC). Source is in `InstaMAT2Remix/`.

Docs map: user-facing `README.md` (root) + `InstaMAT2Remix/dist_template/README.md`
(shipped in the release zip); feature-parity tracker `docs/wbc_parity_audit.md`;
historical session logs `docs/history.md` (archived — read for archaeology only;
superseded CLAUDE.md handoffs live in this file's git history).

## Commands

```powershell
# Full build + dev install (auto-detects VS 18 2026 / VS 17 2022; Qt
# 6.6.3/6.5.3 MSVC; installs dev junction, user plugin dirs, startup
# script, and generates the Library .IMP):
cd InstaMAT2Remix; .\build_plugin.ps1

# Build only (no install) — used by packaging:
cd InstaMAT2Remix; .\build_plugin.ps1 -InstallMode None

# Release zip (dist\InstaMAT2Remix-v<ver>-win64.zip with installer + docs):
cd InstaMAT2Remix; .\package_release.ps1

# C++ tests. NOTE: this machine has VS 18 / 2026 — the generator string
# MUST match ("Visual Studio 17 2022" fails here). Qt 6.6.3/msvc2019_64
# is ABI-compatible with both.
cd InstaMAT2Remix
cmake -S . -B build_test -G "Visual Studio 18 2026" -A x64 `
      -DCMAKE_PREFIX_PATH="C:/Qt/6.6.3/msvc2019_64"
cmake --build build_test --target TestRemixConnector --config Debug
ctest --test-dir build_test -C Debug -V

# Python connector tests (what CI runs):
python -m pytest InstaMAT2Remix/tests/ -v
```

Logs land at `Documents\InstaMAT2Remix\logs\remix_connector.log`.

## Current architecture (v0.0.1-alpha, 2026-07-07)

Menu ("RTX Remix Connector", exact WBC labels/order): Pull From Remix /
Import Textures from Remix / Push To Remix / Force Push to Remix /
Duplicate Material to Remix / — / Settings... / Diagnostics... / About...
(Duplicate has no WBC/Substance2Remix equivalent — InstaMAT-only, see below
and `docs/wbc_parity_audit.md`.)

- **Pull From Remix** (`RemixConnector::PullFromRemix`, zero prompts):
  resolve Remix selection (`/stagecraft/assets/` + `/material` +
  `/file-paths`) → optional tiling-plane substitute (`UseTilingMeshOnPull`
  setting) → optional Blender Smart-UV unwrap (`AutoUnwrap`) → persist link
  state (`LinkedMaterialPrim`/`LinkedMeshPath` in QSettings) → auto-create the
  project via the UI-automation recipe (`TryCreateTexturingProjectFromMesh` →
  `RunNewProjectRecipe`, class-name driven; logs through the file Logger; sets
  a `projectTypeUncertain` flag when the Asset Texturing tile couldn't be
  matched, surfaced in the Pull summary). No texture download, no manifest —
  strict WBC behavior.
- **Import Textures from Remix**: downloads the linked material's textures
  (DDS→PNG via texconv) with canonical names into
  `Documents/InstaMAT2Remix/Pulled Textures/<project>/`, registers the folder
  as an external asset folder, opens Explorer. The SDK has no API to assign
  textures into an open project — the summary says so.
- **Push To Remix** (`PushToRemix(false)`): target = `LinkedMaterialPrim`
  from QSettings → live export via `ExportActiveLayeringProject` into the
  wiped `ExportFolder` (default `%TEMP%/InstaMAT2Remix_Export`) → stage into
  `%TEMP%/InstaMAT2Remix_PreIngest` as `<materialHash>_<pbr>.<ext>` → ingest
  each channel (`/ingestcraft/mass-validator/queue/material`, 600 s, 1
  attempt) → `PUT /stagecraft/textures/` → layer save → summary. **Export
  failure fails the push cleanly** (actionable dialog, nothing ingested — no
  fallback).
- **Force Push** (`PushToRemix(true)`): silently relinks to the current Remix
  selection (persisted), then pushes under a non-overwriting filename root
  (`ChooseNonOverwritingRoot`: `<hash>_1/_2/…`, boundary-aware .dds scan).
- **Live export** (`ExportActiveLayeringProject`): discovery = newest
  non-plugin `.IMP` in `Documents/InstaMAT/Library` (the SDK has no
  active-package API). When that on-disk `.IMP` is >60 s stale, the plugin
  **auto-saves** first (`TrySaveActiveProject` drives Studio's File > Save via
  `FindBestSaveAction`, then polls ~8 s for the `.IMP` to refresh); only if no
  Save action is reachable does it fall back to the manual Ctrl+S/Retry dialog.
  Execution happens **OUT OF PROCESS** in `InstaMAT2RemixExport.exe`
  (`ExportWorker.cpp`, Qt-free console exe): the plugin spawns it via
  QProcess (event-pumped wait, 10 min cap) and parses its stdout `IM2RX
  key=value` protocol (`RUNG`/`CHANNEL`/`NATIVE`/`FINALSIZE`/`COLLAPSED`/
  `ERROR`/`DONE`). Why: on Studio 3.1+
  an in-process `IElementExecution::Execute` routes through a host GL-context
  callback that calls `QOpenGLContext::makeCurrent` on Studio's render-thread
  context → Qt qFatal "Cannot make QOpenGLContext current in a different
  thread" → `__fastfail` (0xc0000409, fail-fast code 7) which **no SEH guard
  can catch** — WinDbg-confirmed on the 2026-07-06 crash dumps; this is the
  root cause of "Push crashes Studio" (and likely the historical 3.0 Execute
  crashes). The worker hosts the SDK itself: `LoadLibrary` Studio's
  `InstaMAT.dll` → `GetInstaMATBuildDate` (major-match, host is SDK v3.4) →
  `GetInstaMAT` → `InitializeAuthorization("InstaMAT", nullptr)` (picks up
  the shared machine license in `%PROGRAMDATA%\InstaMAT\InstaMAT\1.0`) →
  `Initialize(BackendTypeGPU, BackendFlagNone)` on its main thread (~2 s;
  CPU fallback) → `LoadPackage(Environment/Library.IMP, systemLibrary=true)`
  (~2 s) → `AllocPackageFromFile(project.IMP)` → find the
  ElementLayer/ElementMaterialLayer graph → bind mesh → `SetFormat` →
  `Execute` → output walk. Mesh binding: **raw bytes first**
  (`SetResourceForGraphVariable`) — the saved pkg:// mesh URL and file:///
  URLs do NOT resolve in a fresh SDK process and Execute then "succeeds" with
  blank all-white outputs (headless-verified on a real project, 2026-07-06;
  bytes-bound export produced the correct painted maps). Output walk (in the
  worker, **two-pass**: allocate ALL output samplers first, then write, then
  dealloc — a one-pass alloc→write→dealloc loop collapsed all but the last
  ('Normal') output): composition-graph outputs sampled with `sRGB=false`
  (already colorspaced — passing true double-gammas albedo), raw by colorspace;
  format honors `ExportFileFormat` (png/tga/jpg).
- **Export size** (`ComputePushExportSize` → worker): a fixed `ExportResolution`
  setting wins (aspect-corrected vs the original Remix DDS when
  `RestoreAspectOnExport`); **Auto (0) → QSize(0,0)**, a sentinel telling the
  worker to render at the resolution the user **baked** the project at. The
  worker reads that from the `"BakeSettings"` JSON embedded in the `.IMP`
  (`ReadBakeResolutionFromImp`: `"Width"`/`"Height"`, e.g. 4096) — NOT the
  original Remix DDS dims (which for a Remix capture can be tiny, e.g. 66×66;
  that was the "pushed the original pulled size" bug). The layer graph's own
  `Resolution` input reads 0×0/auto and is unusable.
- **Render race (SDK limitation)**: the standalone renderer intermittently
  produces 1×1 outputs for every channel except 'Normal' (processed last).
  It is **per-process** (a worker whose first Execute collapses keeps
  collapsing — in-process re-execution never recovers; only a fresh process is
  an independent draw) and **nondeterministic** (~30 % clean per process on a
  healthy GPU, roughly size-independent; far worse under GPU contention from
  other apps, and degrades badly after many rapid SDK init/execute/shutdown
  cycles — likely a driver-state issue). Mitigation: the worker reports
  `COLLAPSED=1`; the plugin **re-spawns fresh workers** (up to 6: 3 at the
  baked size, then step-downs 2048/1024/512) and keeps the first clean render.
  If none is clean it pushes best-effort (the good channels + Normal) and the
  Push summary + `m_exportHadCollapse` warn the user to close GPU-heavy apps
  and re-push. A worker crash/timeout fails the push cleanly — Studio itself
  can no longer be taken down by an export.
- **Duplicate Material to Remix** (`RemixConnector::DuplicateMaterialToRemix`,
  `InstaMAT2Duplicate`): no WBC/Substance2Remix equivalent (checked
  read-only). Reuses `ExportActiveLayeringProject` (the same out-of-process
  worker path as Push — never in-process `Execute`) to bake the currently
  painted project, then publishes it to Remix as a **new, independent**
  material prim under a fresh identity, never touching the source-linked
  prim. Identity: `GenerateMaterialHash()` — `XXH3_64bits` (vendored
  `vendor/xxhash.h`, `XXH_INLINE_ALL`, BSD-2-Clause) of a fresh random UUID,
  16 uppercase hex chars → `/RootNode/Looks/mat_<hash>`. Channels: the same
  seven WBC-parity PBR types Push uses (Albedo/Normal/Roughness/Metallic/
  Emissive/Height/Opacity, opacity gated by `IncludeOpacityMap`). BCn
  encoding happens server-side via Remix's `ConvertToDDS` ingest
  check-plugin (same as Push — no local texconv step). Ingest is
  deliberately **async**, unlike Push's single blocking `/queue/material`
  call: `POST /ingestcraft/mass-validator/validate` (≤8 concurrent,
  `kMaxDuplicateIngestWorkers`, `DuplicateIngestChannelsAsync`) then poll
  `GET /ingestcraft/mass-validator/completed_schemas` until every submitted
  job resolves or 300 s elapse — no blind wait. MDL parameter preservation
  is scoped to **texture bindings only** (`GET
  /stagecraft/assets/<source>/textures`, for channels the source material
  carries but this export did not re-bake) — the Remix REST surface has no
  generic non-texture MDL-scalar endpoint anywhere in this codebase or in
  Substance2Remix. Writes a local `.usda` sidecar (`AperturePBR_Translucency`
  bindings) next to the baked set — Remix has no ingest endpoint to upload
  one through. `PUT /stagecraft/textures/` `{force:true}` on the new prim +
  best-effort layer save, mirroring Push's tail. See
  `docs/wbc_parity_audit.md` for the full behavior table.
- **Settings** (QSettings "InstaMAT2Remix"/"Config", 5 tabs matching WBC):
  Connection / Paths (texconv, Blender, Export Folder, Remix output
  subfolder, log) / Pull (tiling, unwrap) / Export (format, resolution,
  opacity, aspect) / Advanced (log level, Smart-UV numbers).
- **Packaging**: `tools/imp_packaging.ps1` synthesizes the `.IMP` MAT
  container (header = `MAT` + 0x01 + zero padding to 0x8C; entries =
  `[u32 nameLen][u32 0][name][u64 dataLen][data]`) — no sample.IMP template
  needed. `package_release.ps1` builds the shareable zip (now stages
  `InstaMAT2RemixExport.exe` beside texconv);
  `dist_template/` holds `install.ps1`/`uninstall.ps1`/user README/licenses.

## Key files

- `InstaMAT2Remix/RemixConnector.cpp/.h` — all flows (Pull ~1980, Import
  ~2150, worker-driven export ~2680-2850, Push ~2860, Duplicate ~3255-3780),
  REST helpers, Force Push statics, recipe (anon namespace ~270-1330).
- `InstaMAT2Remix/vendor/xxhash.h` — vendored xxHash (BSD-2-Clause,
  upstream `Cyan4973/xxHash`), included with `XXH_INLINE_ALL` from
  `RemixConnector.cpp` only, for `GenerateMaterialHash()`.
- `InstaMAT2Remix/ExportWorker.cpp` — `InstaMAT2RemixExport.exe`, the
  out-of-process exporter (standalone SDK host; Qt-free; `IM2RX` stdout
  protocol; two-pass output walk; `ReadBakeResolutionFromImp` for Auto size;
  `--probe` for auth/init diagnostics, plus `--diag`/`--rung`/`--only`/
  `--rawout`/`--noretry`/`--sleepms`/`--retries`/`--no-plugins` debug flags
  used in the 2026-07-06 render-race forensics).
- `InstaMAT2Remix/MeshData.cpp/.h` — OBJ loader, `GraphMeshAdapter :
  IGraphMesh`, `ReadDdsDimensions`. Unit-tested.
- `InstaMAT2Remix/ExternalTools.cpp/.h` — Blender unwrap + mesh→OBJ convert,
  texconv wrappers.
- `InstaMAT2Remix/GuiManager.cpp`, `SettingsDialog.cpp`,
  `DiagnosticsDialog.cpp`, `PluginPaths.cpp`, `Logger.cpp`,
  `RemixNodes.cpp` (Element node), `CExports.cpp` (C ABI),
  `rtx_remix_connector.py` (optional Python bridge, env-gated).
- `InstaMAT2Remix/tests/TestRemixConnector.cpp` — QtTest cases (channel
  table, layer-package finder, staging incl. forced names, MeshData, Force
  Push root chooser, InstaMAT2Duplicate hash/USDA/ingest-payload helpers).
  `tests/test_rtx_remix_connector.py` — 5 pytest cases.
- `docs/wbc_parity_audit.md` — parity tracker; read before adding features.

## Build/toolchain gotchas

1. **VS 18 / 2026 on this machine** — CMake generator must be
   `"Visual Studio 18 2026"`; `build_plugin.ps1` auto-detects and falls back
   to the VS-bundled CMake when the system CMake is too old.
2. **Do NOT define `INSTAMAT_LIB_DYNAMIC`** — the official SDK sample defines
   only `INSTAMATPLUGIN_LIB`. `INSTAMAT_LIB_DYNAMIC` marks every SDK
   interface `__declspec(dllimport)`, which breaks deriving from `IGraphMesh`
   (unresolvable ctor/dtor imports). The plugin talks to the SDK purely
   through host-provided vtables.
3. **MSVC `#define private public` mangling trap** — member functions a test
   calls directly must be `public:` in `RemixConnector` (MSVC encodes access
   into mangled names).
4. **PowerShell + BinaryWriter**: passing a PS-unrolled array to
   `BinaryWriter.Write` binds `Write(Boolean)` and writes one 0x01 byte —
   always cast `[byte[]]` first (see `tools/imp_packaging.ps1`).

## InstaMAT::UI class names the recipe depends on

Stable class names from InstaMAT engineering (contract — if a future SDK
renames them, the recipe falls back to its diagnostic dump and
`CloseAnyVisibleDialog`): `InstaMAT::UI::IMProjectTypeSelectionDialog`,
`IMProjectTypeSelectionButton`, `IMGraphObjectPickerGroupWidget`
(objectName `WIDGET_Mesh*`), `IMGraphObjectPickerPopupFrame`,
`IMTemplateSelectionButton`, plus `ILToolButton` named `CreateProject`.

## Things That WILL NOT Work

- `QMouseEvent`/`QKeyEvent` simulation (removed by user request).
- `QAccessible` without `QT_ACCESSIBILITY=1` before app launch (removed).
- `findChildren<QLineEdit*>()` on QML dialogs.
- SDK access to the live in-memory project: there is **no**
  `GetActivePackage`/`GetOpenPackages`/project-creation/channel-assignment
  API (verified byte-identical against the newest official SDK headers).
  Hence the UI-automation recipe for Pull and the Library-tail discovery for
  Push.
- **Any in-process `IElementExecution::Execute` from plugin code (Studio
  3.1+)** — the engine's `GPUBackend::MakeContextCurrent` calls back into
  Studio, which runs `QOpenGLContext::makeCurrent` on a context owned by the
  render thread → qFatal → `__fastfail(7)`. Fail-fast exceptions bypass SEH
  entirely (`__try/__except` never fires); Studio dies before the plugin can
  log. Export must stay out-of-process (`InstaMAT2RemixExport.exe`).
- Executing a graph the host owns (`GetGraphByName` on the live copy) —
  collided with Studio's viewport render historically; always
  `AllocPackageFromFile` a private copy.
- Loading `C:/Program Files/InstaMAT Studio/Environment/Library.IMP` via SDK
  **inside Studio** (thousands of ID-collision warnings — it is already
  loaded). In the standalone worker it is required and clean:
  `LoadPackage(..., persistentResources=true, systemLibrary=true)`.
- `file:///` or foreign-package `pkg://` resource URLs in a standalone-hosted
  SDK (`SetResourceURLForGraphVariable` returns true but the engine logs
  "Failed to read file system resource" at Execute and renders blank
  outputs). Bind meshes with `SetResourceForGraphVariable(raw bytes)`.
- Generating `.IMP` **graph content** from scratch (the plugin-package
  container via `imp_packaging.ps1` is fine — it holds only DLL/meta/assets).

## Known issues / next steps

- **Duplicate Material to Remix live smoke test pending** (2026-07-07):
  `DuplicateMaterialToRemix` reuses the same headless-verified
  `ExportActiveLayeringProject` path Push uses, and its async ingest
  (`DuplicateIngestChannelsAsync`) and USDA/hash helpers are unit-tested, but
  a click-through against a running Remix Toolkit (new prim appears,
  textures resolve, source material unchanged) has not been user-confirmed.
- **Live in-Studio smoke test pending** (2026-07-06): the worker export,
  baked-size resolution, two-pass walk, and auto-save are headless-verified,
  but a click-through of Push To Remix from Studio's menu (auto-save → worker
  spawn(s) → stage → ingest → PUT → Remix shows the textures at the baked
  size) has not been user-confirmed. Also unverified live: the wrong-type
  note, hash-root staged pushes, installer zip on a clean machine.
- **Render-race verification blocked by GPU state (2026-07-06)**: the 1×1
  collapse could not be cleanly re-verified at end of session because ~90
  rapid worker launches + a running game degraded the GPU/driver state until
  even 256 px collapsed (57 % GPU util with no worker running). Early-session
  runs on a fresh GPU rendered every channel full at 512/2048, proving the
  two-pass code is correct. **A reboot likely restores clean rendering.** If
  collapses persist for the user on a fresh GPU with no other GPU apps, it is
  an InstaMAT SDK bug to raise with them (standalone `Execute` of a layer
  graph intermittently yields 1×1 for all non-'Normal' outputs).
- If the worker ever fails only in Studio's context, run it by hand:
  `InstaMAT2RemixExport.exe --probe` (auth/init), then the full command the
  plugin logs as `ExportActive: spawning worker: …` (append `--diag` to dump
  per-output composition/raw dims).
- Multi-mesh material-group bundling on Pull (WBC has it) — not ported.
- Published (2026-07-07) as **v0.0.1-alpha** (prerelease) at
  `github.com/skurtyyskirts/InstaMAT2Remix` (matches `kPluginRepoUrl`). The old
  repo was renamed to `InstaMAT2RemixArchieve` (private) to free the name. The
  public repo is a clean single-commit snapshot; local dev history is unpushed.

## Important Constraints

- DO NOT add mouse/keyboard event simulation (user requirement).
- DO NOT link `Qt6Quick.dll`/`Qt6Qml.dll` (breaks `.IMP` plugin loading).
- Plugin DLL may load from a temp dir when installed via `.IMP` — dependency
  resolution is tricky; always test with `build_plugin.ps1`.

## Workspace rules

1. **WholeBodyCapture is reference-only — never modified.** The Substance
   Painter plugin at `c:\Users\skurtyy\Documents\Adobe\Adobe Substance 3D
   Painter\python\plugins\WholeBodyCapture` is the behavioral
   source-of-truth. Read it; do not edit it.
2. **Target API is InstaMAT, not Substance Painter** (`InstaMATAPI.h`,
   `InstaMATPluginAPI.h`). No `substance_painter.*` calls.
3. **User data** goes under `%USERPROFILE%\Documents\InstaMAT2Remix\`
   (`logs\`, `Pulled Textures\`, `MeshCache\`). Never inside the plugin
   install directory.
4. **Vendor SDK headers**: the repo's `InstaMATAPI.h` is the 2026 SDK
   (verified byte-identical to the latest official download). Take
   2026-or-newer when regenerating.
5. **Feature parity tracking** in `docs/wbc_parity_audit.md`; check it before
   adding features and update it when behavior changes.
6. **Always rebuild + reinstall after source changes**: run
   `cd InstaMAT2Remix; .\build_plugin.ps1` before reporting a task complete
   (Program Files copy may warn without admin — that's fine). Skip only for
   pure documentation edits.

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

## Current architecture (v0.0.2-alpha, 2026-07-12)

Menu ("RTX Remix Connector", exact WBC labels/order): Pull From Remix /
Import Textures from Remix / Push To Remix / Force Push to Remix / — /
Settings... / Diagnostics... / About...

- **Pull From Remix** (`RemixConnector::PullFromRemix`): template chooser
  first (Asset Texturing default / Element Graph / Materialize Image;
  "remember my choice" persists to `PullProjectTemplate`, "ask" default —
  `ProjectTemplates.h/.cpp` holds the enum, per-template
  `TemplateRecipeConfig`, the unit-tested tile matcher, and the chooser
  dialog) → resolve Remix selection (`/stagecraft/assets/` + `/material` +
  `/file-paths`) → template branch: Asset Texturing keeps the WBC path
  (optional tiling-plane substitute `UseTilingMeshOnPull`, optional Blender
  Smart-UV unwrap `AutoUnwrap`; unwrapped OBJ moved into the persistent
  `MeshCache` dir because Push needs `LinkedMeshPath` later); Element Graph
  skips tiling/unwrap and runs the pickerless recipe (tile → Create);
  Materialize Image downloads the material's texture
  (`PrepareMaterializeSourceImage` → `<hash>_materialize.png` in Pulled
  Textures, folder registered) and the wizard selects it → persist link
  state (`LinkedMaterialPrim`/`LinkedMeshPath`/`LinkedProjectTemplate`/
  `LinkedImagePath`) → auto-create via the UI-automation recipe
  (`TryCreateProjectFromTemplate` → `RunNewProjectRecipe`, class-name driven;
  tile matching = exact-title pass then guarded contains pass, immune to the
  "nPass Element Graph"/Atom-description traps; only Asset Texturing may
  fall back to the first tile, setting `projectTypeUncertain`). No texture
  download on the Asset Texturing path — WBC behavior.
- **Import Textures from Remix**: downloads the linked material's textures
  (DDS→PNG via texconv) with canonical names into
  `Documents/InstaMAT2Remix/Pulled Textures/<project>/`, registers the folder
  as an external asset folder, opens Explorer. The SDK has no API to assign
  textures into an open project — the summary says so.
- **Push To Remix** (`PushToRemix(false)`): target = `LinkedMaterialPrim`
  from QSettings → live export via `ExportActiveLayeringProject` into the
  wiped `ExportFolder` (default `%TEMP%/InstaMAT2Remix_Export`) → opacity
  merged into albedo's ALPHA when `IncludeOpacityMap` (Remix has no consumed
  opacity_texture — `MergeOpacityIntoAlbedoAlpha`) → channels with no Remix
  input (ao) dropped with a summary note; translucent (glass) targets
  detected via `ClassifyMaterialFromTextureAttrs` keep only
  transmittance/emissive/normal → stage into `%TEMP%/InstaMAT2Remix_PreIngest`
  as `<materialHash>_<pbr>.<ext>` → ingest each channel
  (`/ingestcraft/mass-validator/queue/material`, 600 s, 1 attempt; types from
  `ResolveIngestValidationType` — ONLY the 13 valid TextureTypes names;
  normal encoding from `NormalMapEncoding`) → `PUT /stagecraft/textures/`
  pairs from `BuildTexturePutPairs` (opaque vs translucent routing;
  transmittance→`subsurface_transmittance_texture` opaque /
  `transmittance_texture` glass; SSS single-scattering input is
  `subsurface_single_scattering_texture`, NOT `..._albedo_texture`) → layer
  save → summary. **Export failure fails the push cleanly** (actionable
  dialog, nothing ingested — no fallback).
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
  `OUTPUTCOUNT`/`OUTPUTSKIP`/`OUTPUTFALLBACK`/`FATAL`/
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
  `COLLAPSED=1` (incl. the all-channels-1x1 total collapse); the plugin
  **re-spawns fresh workers** (up to 6: 3 at the baked size, then
  aspect-preserving step-downs 2048/1024/512) and keeps the first clean
  render. If none is clean the **collapse guard aborts the push** (checks
  `m_exportHadCollapse` AND re-reads every non-height channel's on-disk size)
  — nothing is ingested, Remix untouched, dialog tells the user to close
  GPU-heavy apps / reboot and re-push. A worker crash/timeout also fails the
  push cleanly — Studio itself can no longer be taken down by an export.
  The worker refuses inherit/url mesh binds when a real mesh file was passed
  (they render blank standalone), and the plugin hard-fails the push when
  `LinkedMeshPath` is missing on disk instead of exporting blanks.
- **Settings** (QSettings "InstaMAT2Remix"/"Config", 5 tabs matching WBC):
  Connection / Paths (texconv, Blender, Export Folder, Remix output
  subfolder, log) / Pull (project template, tiling, unwrap) / Export
  (format, resolution, normal encoding, opacity-merge, aspect) / Advanced
  (log level, Smart-UV numbers). New keys MUST be added to `kManagedKeys`
  in SettingsDialog.cpp (the Test Connection snapshot silently reverts any
  key missing from that list). Test Connection
  snapshots+restores QSettings (Cancel still cancels) and uses a 5 s/1-try
  timeout; OK validates the Export Folder via `IsSafeToWipe` and normalizes
  scheme-less API URLs.
- **Safety guards (2026-07-12 release-review pass)**: `IsSafeToWipe`
  (unit-tested) gates every `removeRecursively` of the Export Folder — empty
  value used to wipe the process CWD; Pull/Import/Push carry a reentrancy
  guard (`m_operationInProgress`) because RequestJson/QProcess waits pump the
  event loop and menu clicks could interleave flows.
- **Packaging**: `tools/imp_packaging.ps1` synthesizes the `.IMP` MAT
  container (header = `MAT` + 0x01 + zero padding to 0x8C; entries =
  `[u32 nameLen][u32 0][name][u64 dataLen][data]`) — no sample.IMP template
  needed. `package_release.ps1` builds the shareable zip (now stages
  `InstaMAT2RemixExport.exe` beside texconv);
  `dist_template/` holds `install.ps1`/`uninstall.ps1`/user README/licenses.

## Key files

- `InstaMAT2Remix/RemixConnector.cpp/.h` — all flows (Pull ~1980, Import
  ~2150, worker-driven export ~2680-2850, Push ~2860), REST helpers,
  Force Push statics, recipe (anon namespace ~270-1330).
- `InstaMAT2Remix/ExportWorker.cpp` — `InstaMAT2RemixExport.exe`, the
  out-of-process exporter (standalone SDK host; Qt-free; `IM2RX` stdout
  protocol; two-pass output walk; `ReadBakeResolutionFromImp` for Auto size;
  graph-tier selection layer→element→materialize with `GRAPHTYPE=` line;
  Inherit rung refuses pkg:// mesh binds unconditionally unless `--rung
  inherit`; `--image <file>` bytes-binds a Materialize source image;
  `--probe` for auth/init diagnostics, plus `--diag`/`--rung`/`--only`/
  `--rawout`/`--noretry`/`--sleepms`/`--retries`/`--no-plugins` debug flags
  used in the 2026-07-06 render-race forensics).
- `InstaMAT2Remix/PbrChannelMap.h` — Qt-free shared Studio-output→canonical
  map (+`ChannelMayLegitimatelyRenderFlat`), used by the worker, plugin and
  tests. `InstaMAT2Remix/ProjectTemplates.h/.cpp` — Pull template enum/
  configs, `MatchProjectTypeTile`, chooser dialog.
- `InstaMAT2Remix/MeshData.cpp/.h` — OBJ loader, `GraphMeshAdapter :
  IGraphMesh`, `ReadDdsDimensions`. Unit-tested.
- `InstaMAT2Remix/ExternalTools.cpp/.h` — Blender unwrap + mesh→OBJ convert,
  texconv wrappers.
- `InstaMAT2Remix/GuiManager.cpp`, `SettingsDialog.cpp`,
  `DiagnosticsDialog.cpp`, `PluginPaths.cpp`, `Logger.cpp`,
  `RemixNodes.cpp` (Element node), `CExports.cpp` (C ABI),
  `rtx_remix_connector.py` (optional Python bridge, env-gated).
- `InstaMAT2Remix/tests/TestRemixConnector.cpp` — 27 QtTest functions, "Totals:
  29 passed" with init/cleanup (channel tables incl. ingest-type validity +
  PUT-pair routing + material-kind classification + opacity merge + template
  tile matching, worker error builders + albedo-fallback guard +
  ParseWorkerStdout/FATAL retriability, layer-package finder, staging incl.
  forced names, MeshData, Force Push root chooser, IsSafeToWipe).
  `tests/test_rtx_remix_connector.py` — 5 pytest cases.
  NOTE: the QtTest exe is GUI-subsystem — it prints NOTHING to a console;
  run it with `-o <file>,txt` (or trust the exit code / ctest) to see totals.
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

- `QMouseEvent`/`QKeyEvent` simulation (removed by user request). One
  deliberate exception survives: `CloseAnyVisibleDialog` sends a synthetic
  Escape as its LAST-resort error-recovery step (after close() attempts) to
  unstick QML overlays — do not extend key simulation beyond that.
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

- **Element Graph push "no recognized PBR channel outputs" — FIXED in
  0.0.2-alpha (2026-07-12), needs a live re-test.** The 4-point handoff plan
  was implemented plus research findings:
  - SDK research verdict (headers byte-identical to newest official SDK): the
    public API is **execution-only** — no node enumeration, no connection
    reading, no graph authoring, no way to reach a canvas node's output pins.
    Graph-level output parameters are the ONLY export surface; sampling the
    "Bathroom Tiles Clean" node's pins directly is impossible. Executing the
    library material graph by name would render library defaults (ignores
    instance tweaks) — rejected.
  - Official precedent (InstaMAT for Blender add-on): exports only exposed
    graph outputs, hard-fails on zero outputs, and aliases output names
    "Output"/"Default" → Base Color. Adopted: `output`/`default`→albedo,
    `metal`→metallic, `transparency`→opacity in PbrChannelMap.h.
  - Worker: `OUTPUTCOUNT=`/`OUTPUTSKIP name='…' (unmapped)` lines; lone
    unmapped image output exports as albedo (`OUTPUTFALLBACK` line, guard
    `ShouldFallbackLoneOutputToAlbedo` — never fires when anything mapped);
    written==0 splits into zero-outputs / all-unmapped (lists seen names via
    the shared builders in PbrChannelMap.h) / --only mismatch / write-fail.
  - Deterministic failures emit `FATAL=<usage|environment|project|outputs>`
    before `ERROR=`; the plugin (`ParseWorkerStdout` static, unit-tested)
    breaks the retry ladder on FATAL — 1 spawn instead of 3.
    `AllocPackageFromFile` failure stays retriable (auto-save sharing
    violation); bind-refusals vs Execute-failures split via
    `executeAttempted` (only the latter retry).
  - Dialog: trailing-period fix, newline layout, rename tip on FATAL=outputs;
    fallback pushes add a summary note (`m_exportAlbedoFallbackNote`).

- **Live in-Studio smoke status (updated 2026-07-12, post-0.0.2-fix session)**:
  ✅ user-confirmed — Asset Texturing pull + full push (all HTTP 200); Element
  Graph pull; **Element Graph push both ways**: zero-output graph → exactly 1
  worker spawn, `FATAL=outputs`, actionable dialog; generic-name graph →
  `OUTPUTFALLBACK name='Parameter 1' canonical=albedo` → clean render → ingest
  → PUT 200 → layer save 200 + summary note (guard correctly picked the lone
  IMAGE output among 2 unmapped); **Materialize Image pull + full push**
  (picker prefix resolved to `WIDGET_Image2`; OUTPUTCOUNT=6, clean attempt 1,
  5 ingested / ao skipped, PUT + save 200). One intermittent Materialize pull
  failure ("Asset '<hash>_materialize.png' not found in asset list") was
  root-caused to Studio's async asset scan racing the recipe's single list
  scan — fixed with a bounded re-scan retry (Recipe Step7, ~4×750 ms); the
  retry itself is not yet user-verified live. Still unverified: SSS/translucent
  push (needs a glass target), opacity-merge visual check in Remix, template
  chooser "remember" flow, hash-root staged pushes, installer zip on a clean
  machine.
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
- **v0.0.2-alpha published 2026-07-12** (snapshot overlay onto the public
  `main`, tag + GitHub release with the zip). The publish flow — incl. the
  snapshot-overlay procedure that preserves the GitHub-side
  `.github/FUNDING.yml` and strips internal paths (`benchmark/` build junk,
  `.cursor/`, `.vscode/texconv.exe`, `docs/history.md`, `docs/superpowers/`,
  `docs/qml_accessibility_log.md`, `docs/TESTING_GUIDE.md`) — is documented in
  `docs/PUBLISH.md`. Releases are published WITHOUT `--prerelease` so
  `releases/latest` always serves the newest build (v0.0.1 set that
  precedent).

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
   pure documentation edits. **Hook-enforced**: a Stop hook
   (`.claude/hooks/check-plugin-freshness.ps1`) blocks finishing while the
   sources are newer than the built/installed plugin — if it fires, rebuild;
   don't work around it.

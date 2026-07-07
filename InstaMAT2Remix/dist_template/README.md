# RTX Remix Connector for InstaMAT (InstaMAT2Remix)

Bridge between **InstaMAT Studio** and **NVIDIA RTX Remix Toolkit** — pull a
captured mesh into a paint-ready InstaMAT project with one click, paint your
PBR material, and push the finished textures straight back into your Remix
project. Behaves 1:1 with the Substance2Remix plugin for Substance Painter.

Project home & source: <https://github.com/skurtyyskirts/InstaMAT2Remix>

## Requirements

- Windows 10/11 x64
- **InstaMAT Studio 3.x** (free at [instamaterial.com](https://instamaterial.com)),
  activated/licensed — Push renders through the same license
- **NVIDIA RTX Remix Toolkit** with its REST API enabled (default
  `http://localhost:8011`) and a Remix project open
- Optional: **Blender 3.6+** for auto-UV-unwrap on Pull (set the path in
  Settings > Paths)
- `texconv.exe` and the export worker (`InstaMAT2RemixExport.exe`) are
  bundled — no extra install needed

Everything the plugin needs is inside this release — the Qt runtime,
`texconv.exe`, the render helper `InstaMAT2RemixExport.exe`, **and** the
Microsoft Visual C++ runtime it links against. There is nothing else to
download or install (no separate "VC++ redistributable" required).

### Install (script)

1. Unzip this release anywhere.
2. Close InstaMAT Studio if it's running.
3. Right-click `install.ps1` > *Run with PowerShell* (or run
   `powershell -ExecutionPolicy Bypass -File .\install.ps1`). The
   `-ExecutionPolicy Bypass` form also gets past the "script is blocked"
   warning Windows adds to downloaded/zipped files.
4. Start RTX Remix Toolkit (with a project open), then InstaMAT Studio.
5. A **RTX Remix Connector** menu appears in InstaMAT's menu bar.

To remove the plugin later, run `uninstall.ps1`.

### Install (manual, no script)

The installer only copies files into your user folders — you can do the same
by hand. Close InstaMAT Studio, then:

1. Copy the whole **`plugin`** folder into
   `%USERPROFILE%\Documents\InstaMAT\Plugins\` and rename the copy to
   **`InstaMAT2Remix`** (so you have
   `…\Documents\InstaMAT\Plugins\InstaMAT2Remix\` with the DLL/exe inside).
2. Copy **`plugin\InstaMAT2RemixPlugin.dll`** into the `Plugins` folder itself
   (`%USERPROFILE%\Documents\InstaMAT\Plugins\InstaMAT2RemixPlugin.dll`).
3. Copy **`InstaMAT2Remix.IMP`** into
   `%USERPROFILE%\Documents\InstaMAT\Library\` (create it if missing) — this
   `.IMP` is what makes Studio load the plugin.

Start Remix then Studio. To uninstall manually, delete those same three items.

## Workflow

| Menu action | What it does |
|---|---|
| **Pull From Remix** | Reads your current Remix selection, resolves its mesh + material, and automatically creates a new InstaMAT project from the mesh — zero prompts. The project is "linked" to the Remix material. |
| **Import Textures from Remix** | Downloads the linked material's current textures (converted DDS→PNG with canonical names like `albedo.png`) into `Documents\InstaMAT2Remix\Pulled Textures\<project>\`, registers the folder in InstaMAT's Asset Browser, and opens it in Explorer. Drag each map onto the matching channel — InstaMAT's plugin SDK has no API to auto-assign them. |
| **Push To Remix** | Renders your painted project's outputs (albedo/normal/roughness/metallic/height/…), ingests them into your Remix project, points the linked material at the new textures, and saves the Remix layer. It **auto-saves your project first**, and by default exports at the resolution you **baked** the project at (bake at 4K → push 4K). Rendering runs in a separate helper process so it can never crash Studio. |
| **Force Push to Remix** | Like Push, but relinks to whatever material is currently selected in Remix and writes the textures under a fresh, non-overwriting filename root (`<hash>_1`, `_2`, …). |
| **Settings...** | Connection (URL, timeout, Test Connection), Paths (texconv, Blender, export folder, Remix output subfolder, log), Pull (tiling mesh, auto-unwrap), Export (format, resolution, opacity map, aspect restore), Advanced (log level, Smart-UV parameters). |
| **Diagnostics...** | One-click health report: versions, path checks (OK/MISSING), settings, link state, live Remix connectivity. Copy to Clipboard for bug reports. |

### Typical session

1. In **Remix Toolkit**: select the mesh (or its material) you want to retexture.
2. **RTX Remix Connector > Pull From Remix** — an InstaMAT project opens with
   the mesh loaded.
3. Paint / drag library materials onto the project. Set your bake resolution
   (e.g. 4K) if you want a high-resolution push.
4. **RTX Remix Connector > Push To Remix** — Push saves the project for you,
   renders the textures at the baked resolution, and updates the material in
   Remix. (Pushing takes ~15–30 s while the helper process renders.)

## Notes & known quirks

- **Export resolution**: with *Export Resolution* set to **Auto** (the
  default), Push renders at the resolution you baked the project at — so bake
  at 4K to push 4K. Pick a fixed size in Settings > Export to override.
- **Close GPU-heavy apps before a big Push**: the InstaMAT renderer has an
  intermittent glitch (worse when a game or other GPU-heavy app is running,
  or right after a long session) where some channels come out as a 1×1 pixel.
  The plugin automatically retries a few times and will **warn you in the Push
  summary** if it couldn't get a clean render. If you see that warning, close
  other GPU apps (and reboot if it keeps happening) and Push again.
- **Auto-save**: Push saves your project before rendering. If it can't find
  Studio's Save action it falls back to asking you to press Ctrl+S and Retry.
- **Project type**: on some InstaMAT Studio versions the automated New Project
  dialog may land on *Material Layering* instead of *Asset Texturing*. The
  Pull summary tells you when this could not be confirmed; painting and Push
  still work either way.
- **Tiling mode**: enable *Use simple tiling mesh* in Settings > Pull to paint
  on a flat UV-mapped plane instead of the captured mesh (handy for tiling
  materials).

## Troubleshooting

- **"Could not determine Remix project directory" / connection errors**: make
  sure Remix Toolkit is running with a project open and its REST API on port
  8011. Use Settings > Connection > *Test Connection*.
- **Push summary says some channels rendered at 1×1**: a GPU-driver race in
  the InstaMAT renderer (see the note above). Close other GPU-heavy apps,
  reboot if it persists, and Push again. Textures that rendered correctly are
  still pushed.
- **Push fails with "Live export failed" / "Could not execute the layer
  project"**: check `Documents\InstaMAT2Remix\logs\remix_connector.log` for
  the `ExportWorker:` lines. Confirm InstaMAT Studio is activated (the helper
  renders through the same license).
- **Material renders gray in Remix after Push**: verify the ingest folder
  (`<RemixProject>\assets\ingested\Textures\InstaMAT2Remix_Ingested\`)
  contains `.rtex.dds` files *and* their source images.
- For bug reports, attach **Diagnostics... > Copy to Clipboard** output and
  the log file.

## Building from source

See the repository: <https://github.com/skurtyyskirts/InstaMAT2Remix>
(CMake + Visual Studio 2022/2026 + Qt 6.5/6.6 MSVC; `build_plugin.ps1` builds
and installs a dev copy, `package_release.ps1` produces this zip).

## Credits

- Built on the InstaMAT Plugin SDK (© InstaMaterial GmbH).
- Interfaces with NVIDIA RTX Remix Toolkit's REST API.
- `texconv.exe` from Microsoft DirectXTex (MIT) — see THIRD_PARTY_LICENSES.md.

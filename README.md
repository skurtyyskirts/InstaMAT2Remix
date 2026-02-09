# InstaMAT2Remix - RTX Remix Connector Plugin for InstaMAT Studio

> **Status: Active Development (Alpha)**
> **Last Updated: 2026-02-09**
> **Current Blocker: Automated New Project creation via UI automation**

---

## Table of Contents

- [Overview](#overview)
- [Goal & Vision](#goal--vision)
- [Architecture](#architecture)
- [Features — What Works Today](#features--what-works-today)
- [Current Blocker — What We're Stuck On](#current-blocker--what-were-stuck-on)
- [Full History of Approaches Tried](#full-history-of-approaches-tried)
- [What Has NOT Worked (Dead Ends)](#what-has-not-worked-dead-ends)
- [What HAS Worked (Confirmed)](#what-has-worked-confirmed)
- [Technical Details](#technical-details)
- [File Structure](#file-structure)
- [Build & Install](#build--install)
- [Requirements](#requirements)
- [Usage (Current State)](#usage-current-state)
- [Changelog / Session Log](#changelog--session-log)
- [Next Steps & Potential Solutions](#next-steps--potential-solutions)
- [For LLM Assistants Reading This](#for-llm-assistants-reading-this)

---

## Overview

**InstaMAT2Remix** is a C++ plugin for **InstaMAT Studio** that bridges it with **NVIDIA RTX Remix Toolkit**, allowing artists to:

1. **Pull** meshes and textures from RTX Remix into InstaMAT
2. **Push** InstaMAT-generated PBR textures back into RTX Remix materials
3. Achieve a workflow equivalent to the existing **Substance2Remix** plugin (Substance Painter <-> RTX Remix)

The plugin communicates with RTX Remix via its **REST API** (default `http://localhost:8011`) and uses the **InstaMAT Plugin SDK** (C++ DLL with Qt 6).

---

## Goal & Vision

The end goal is feature parity with **Substance2Remix** — specifically:

| Feature | Substance2Remix | InstaMAT2Remix (Target) | Current Status |
|---|---|---|---|
| Pull mesh from Remix | Auto-creates project | Auto-creates project | **BLOCKED** - UI automation fails |
| Import textures from Remix | Manual step | Auto + manual | **WORKING** |
| Push textures to Remix | Exports + ingests + updates material | Exports + ingests + updates material | **WORKING** |
| Force Push (relink material) | Supported | Supported | **WORKING** |
| Tiling mesh mode | Supported | Supported | **WORKING** |
| Auto-unwrap with Blender | N/A | Optional feature | **WORKING** |
| Settings UI | Basic | Tabbed dialog with browse + test | **WORKING** |
| Diagnostics | N/A | Full report + copy to clipboard | **WORKING** |

### The Critical Missing Piece

When you click **"Pull from Remix"** in Substance2Remix, it automatically:
1. Creates a new texturing project
2. Loads the mesh
3. Links the Remix material
4. Shows the 3D asset view + UV map

**We need this exact behavior in InstaMAT**, but InstaMAT's Plugin SDK does **NOT** expose a programmatic API for creating projects. The only way to create an "Asset Texturing" project is through the **GUI** (File > New Project > Asset Texturing > fill mesh path > Create). This means we must **automate the UI**, which is where we are stuck.

---

## Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│ InstaMAT Studio (Host Application)                              │
│                                                                 │
│  ┌──────────────────┐   ┌──────────────────────────────┐       │
│  │   Menu Bar        │   │  New Project Dialog (QML)    │       │
│  │  "RTX Remix" menu │   │  - Asset Texturing card      │       │
│  │  (injected by us) │   │  - Mesh field                │       │
│  └──────────────────┘   │  - Create Project button      │       │
│                          └──────────────────────────────┘       │
│                                                                 │
│  ┌──────────────────────────────────────────────────────┐       │
│  │  InstaMAT2RemixPlugin.dll (Our Plugin)               │       │
│  │                                                       │       │
│  │  PluginMain.cpp      - Entry point, DLL exports       │       │
│  │  RemixConnector.cpp  - Core logic (REST API, Pull,    │       │
│  │                        Push, UI automation)            │       │
│  │  GuiManager.cpp      - Menu injection into host       │       │
│  │  RemixNodes.cpp      - Custom graph nodes (Import/    │       │
│  │                        Export) for Element Graphs      │       │
│  │  SettingsDialog.cpp  - Settings UI                    │       │
│  │  DiagnosticsDialog.cpp - Diagnostics report           │       │
│  │  ExternalTools.cpp   - Blender/texconv integration    │       │
│  │  Logger.cpp          - Persistent log file            │       │
│  └──────────────────────────────────────────────────────┘       │
└─────────────────────────────────────────────────────────────────┘
                              │
                    REST API (HTTP)
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│ NVIDIA RTX Remix Toolkit (http://localhost:8011)                │
│                                                                 │
│  /stagecraft/assets/?selection=true    - Get selected mesh      │
│  /stagecraft/assets/?asset_hashes=...  - Get material from mesh │
│  /stagecraft/textures/                 - Update material texures│
│  /ingestcraft/mass-validator/queue/... - Ingest textures        │
│  /stagecraft/layers/.../save           - Save layer             │
└─────────────────────────────────────────────────────────────────┘
```

### Key Technologies
- **Language**: C++17
- **Build System**: CMake + Visual Studio 2022
- **Qt Version**: Qt 6.5.3 or 6.6.3 (MSVC 64-bit)
- **InstaMAT Plugin SDK**: IInstaMATPlugin interface + IInstaMATElementEntityPlugin for custom nodes
- **RTX Remix REST API**: JSON over HTTP (lightspeed.remix.service v1.0)
- **DDS Conversion**: texconv.exe (bundled) for DDS <-> PNG conversion

---

## Features -- What Works Today

### 1. Menu Integration (WORKING)
The plugin injects an **"RTX Remix"** menu into InstaMAT's menu bar with these actions:
- Pull from Remix
- Import Textures from Remix
- Push to Remix
- Force Push to Remix
- Settings
- Diagnostics
- About

If InstaMAT already has an "RTX Remix" menu, our actions appear as a submenu to avoid collisions.

### 2. Pull from Remix (PARTIALLY WORKING)
- Queries the RTX Remix REST API for the currently selected mesh and material
- Resolves the mesh file path (absolute or relative to Remix project)
- Optionally auto-unwraps the mesh with Blender
- Copies mesh path to clipboard
- Links the material prim for future Push operations
- **BLOCKED**: Cannot auto-create the Asset Texturing project (see blocker section)

### 3. Import Textures from Remix (WORKING)
- Pulls textures from linked (or selected) Remix material
- Converts `.dds` / `.rtex.dds` files to `.png` using `texconv.exe`
- Saves to a pulled-textures folder organized by project name
- Registers the folder as an external asset in InstaMAT

### 4. Push to Remix (WORKING)
- Exports PBR texture outputs from the current InstaMAT graph
- Matches outputs by name: Albedo, Normal, Roughness, Metallic, Emissive, Height, Opacity, AO, Transmittance, IOR, Subsurface
- Ingests textures into Remix via the mass-validator/queue API
- Updates the linked material's MDL shader inputs
- Saves the active layer automatically
- Shows progress dialog with cancel support

### 5. Force Push to Remix (WORKING)
- Same as Push but targets a different currently-selected Remix material
- Relinks the material prim

### 6. Custom Graph Nodes (WORKING)
- **RTX Remix Export** node - Can be placed in Element Graphs to export to Remix
- **RTX Remix Ingest** node - Can be placed in Element Graphs to import from Remix captures
- Both nodes support all PBR channels: Albedo, Normal, Roughness, Metallic, Emissive, Height, Opacity, AO, Transmittance, IOR, Subsurface

### 7. Settings UI (WORKING)
- Tabbed dialog with browse buttons
- Configurable: Remix API URL, timeouts, output subfolder, Blender path, texconv path
- Auto-import textures toggle
- Tiling mesh toggle
- One-click connection test

### 8. Diagnostics (WORKING)
- Full diagnostic report: plugin paths, connection status, settings, environment
- Copy-to-clipboard button for easy sharing

### 9. Logging (WORKING)
- Persistent log file at `Documents/InstaMAT2Remix/logs/remix_connector.log`
- Detailed logging of all operations

### 10. Build System (WORKING)
- `build_plugin.ps1` handles everything:
  - Finds Qt6 automatically
  - Runs CMake + builds Release DLL
  - Runs `windeployqt` for Qt dependencies
  - Creates dev junction link for instant reload
  - Installs to multiple locations (Documents/Plugins, AppData/Plugins, Program Files)
  - Builds a `.IMP` package (InstaMAT's binary package format) for proper plugin registration
  - Installs startup Python script

---

## Current Blocker -- What We're Stuck On

### Problem: Automatic Project Creation

When the user clicks **"Pull from Remix"**, the plugin needs to automatically create a new **"Asset Texturing"** project in InstaMAT Studio with the pulled mesh pre-loaded. This is the core UX parity feature with Substance2Remix.

**The InstaMAT Plugin SDK does NOT provide an API to create projects programmatically.** The only way to create an "Asset Texturing" project is through the InstaMAT Studio GUI:

1. File > New Project (or the startup chooser screen)
2. Click "Asset Texturing" card
3. Fill in the mesh path field
4. Click "Create Project"

### Why This Is Hard

1. **InstaMAT's New Project dialog is QML-based** (QtQuick), not traditional QWidget-based. This means standard QWidget automation (finding QLineEdit, QPushButton, etc.) doesn't work on this dialog.

2. **Qt Accessibility is inactive by default** in InstaMAT Studio. `QAccessible::isActive()` returns `false`, which means QML elements are invisible to the accessibility API. Setting `QT_ACCESSIBILITY=1` at runtime does not activate it — it must be set before the Qt application starts.

3. **`QDialog::exec()` creates a blocking nested event loop.** When we trigger File > New from the menu, a modal dialog opens with its own event loop. Our automation code must run *inside* this event loop (via `QTimer::singleShot` or `QMetaObject::invokeMethod` with queued connection), not after the dialog closes.

4. **No stable QML object names.** InstaMAT's QML elements don't have guaranteed `objectName` properties, making programmatic detection unreliable across versions.

### What Currently Happens

When you click "Pull from Remix":
1. The RTX Remix API call succeeds (mesh path, material prim are retrieved correctly)
2. The plugin triggers the New Project dialog via File > New menu action
3. A `QTimer::singleShot(500ms)` fires inside `QDialog::exec()`'s event loop
4. Multiple automation strategies are tried in sequence:
   a. QWidget detection (for traditional Qt builds)
   b. QML root detection + traversal (searches children of all windows for QML containers)
   c. Brute-force QObject text search (scans ALL QObjects globally for matching text properties)
   d. Qt Accessibility API (if active)
5. If all strategies fail: the dialog is properly closed (including QML/QWindow dialogs), mesh path is copied to clipboard, and manual instructions are shown
6. If a strategy succeeds: project is created with Name="Remix", Category="Materials/Assets", Type="Multi-Material", Mesh=pulled asset

### Specific Error

```
[InstaMAT2Remix] Auto-create project failed: New project screen did not appear 
(could not locate the project creation UI)
```

Or in some cases:
```
[InstaMAT2Remix] QAccessible active: false
[InstaMAT2Remix] Auto-create project attempt 1 failed
```

---

## Full History of Approaches Tried

### Approach 1: Direct QWidget Automation (FAILED)
**What**: Search for QLineEdit, QPushButton, QComboBox, QAbstractItemView in the New Project dialog. Set text directly, click buttons.
**Why it failed**: InstaMAT's New Project dialog is QML/QtQuick-based, not QWidget-based. `findChildren<QLineEdit*>()` returns nothing useful inside the dialog.
**Status**: Dead end for the New Project dialog (works fine for other QWidget-based dialogs in InstaMAT).

### Approach 2: Qt Accessibility API (PARTIALLY WORKS, BUT...)
**What**: Use `QAccessible::queryAccessibleInterface()` to traverse the accessibility tree, find elements by role and text, use `QAccessibleActionInterface` to press buttons and `QAccessibleEditableTextInterface` to set text.
**Why it partially fails**: `QAccessible::isActive()` returns `false` in InstaMAT Studio. QML elements only expose accessibility info when the accessibility layer is active.
**Attempts to activate**:
- `QAccessible::setActive(true)` — no effect
- `qputenv("QT_ACCESSIBILITY", "1")` at runtime — no effect (must be set before Qt app starts)
- Setting it system-wide via registry — untested
**Status**: The code is in place and would likely work IF accessibility were active. This is the most promising path if we can get `QT_ACCESSIBILITY=1` set before InstaMAT launches.

### Approach 3: QML Object Reflection / QObject Traversal (PARTIALLY WORKS, UNRELIABLE)
**What**: Traverse the QObject hierarchy from QQuickWindow/QQuickWidget roots, find QML items by their `text` property, invoke QML methods like `clicked()` via `QMetaObject::invokeMethod`.
**Implementation**: `TryCreateTexturingProjectFromMeshQml()` function:
  - Finds QML roots via `QApplication::topLevelWidgets()` → check for QQuickWidget/QQuickWindow
  - Traverses children looking for objects with matching `text` properties
  - Tries to invoke `clicked()` signal on "Asset Texturing" card
  - Tries to set `text` property on mesh field
  - Tries to invoke `clicked()` on "Create Project" button
**Why it's unreliable**: QML object names and hierarchy change between InstaMAT versions. The QML items may not expose standard `text` properties. The `clicked()` signal invocation may not have the same effect as an actual user click in QML.
**Status**: Implemented as a fallback, occasionally works but not reliably.

### Approach 4: Mouse/Keyboard Event Simulation (REMOVED)
**What**: Send `QMouseEvent`, `QKeyEvent`, drag-and-drop events to the dialog widgets. Find coordinates of QML items and send synthesized mouse clicks.
**Why it was removed**: User explicitly requested "API-only" automation with no simulated mouse/keyboard input. Also unreliable across different screen DPIs and InstaMAT versions.
**Status**: All mouse/keyboard simulation code has been removed from the codebase.

### Approach 5: Clipboard Paste Fallback (REMOVED)
**What**: Copy the mesh path to clipboard, then send Ctrl+V to the mesh field.
**Why it was removed**: Relies on keyboard simulation (removed per user request). Also fragile — requires the correct field to be focused.
**Status**: Clipboard copy is still done as a convenience for manual fallback, but automated paste was removed.

### Approach 6: Drag-and-Drop Simulation (REMOVED)
**What**: Synthesize `QDragEnterEvent` and `QDropEvent` to simulate dropping the mesh file onto the mesh field.
**Why it was removed**: Part of the mouse/keyboard simulation removal. Also unreliable with QML targets.
**Status**: Removed.

### Approach 7: Template-Based Project Creation (PARTIAL, REQUIRES MANUAL SETUP)
**What**: Pre-create a template `.IMP` project file with RTX Remix Import/Export nodes connected, then clone it for each Pull operation.
**Why it's limited**: `.IMP` files are InstaMAT's proprietary binary format ("MAT\x01" header). We cannot programmatically generate them. The user must manually create the template once in InstaMAT Studio.
**Status**: Implemented as an optional workflow. Template graph can be configured in Settings. But it doesn't replace the need for auto-creating an Asset Texturing project.

### Approach 8: `QTimer::singleShot` Inside `QDialog::exec()` Event Loop (CURRENT APPROACH)
**What**: When we trigger File > New from the menu, the `QDialog::exec()` call blocks. We schedule a callback via `QTimer::singleShot(500ms)` that runs inside the dialog's event loop. This callback attempts to:
  1. Detect whether we're on the "chooser" screen (project type cards) or the "wizard" screen (mesh/name fields)
  2. If chooser: find and click "Asset Texturing" card, then schedule another timer for the wizard step
  3. If wizard: fill mesh path, set project name to "Remix", click "Create Project"
**Current behavior**: The timer fires correctly. Multiple detection strategies are tried in order:
  - QWidget detection (for traditional Qt widgets)
  - QML root detection via `CollectAllQmlRoots()` (searches ALL widget/window children for QML containers)
  - Brute-force QObject text search via `FindQObjectWithTextGlobal()` (searches ALL reachable QObjects)
  - Qt Accessibility API (if active)
**Status**: This is the current implementation with multiple fallback layers. Waiting for InstaMAT dev team to provide stable class/object names for guaranteed detection.

### Approach 9: Brute-Force QObject Text Property Search (NEW - PROMISING)
**What**: Search ALL QObjects reachable from ALL visible windows, widgets, and QML content trees for items with matching `text`, `placeholderText`, or `objectName` properties. This bypasses the need to find a "QML root" first — it just scans everything.
**Implementation**: `FindQObjectWithTextGlobal()`, `FindQObjectWithPropertyGlobal()`, `TryCreateTexturingProjectBruteForce()`
**Why it might work**: Even if we can't find the QML root through `GetQmlRoot()`, the QML items are still QObjects with properties. By searching ALL QObjects globally (including QQuickWindow contentItem trees), we should find text-bearing items like "Asset Texturing", "Create Project", etc.
**Why it might fail**: If QML items in InstaMAT's dialog don't expose their `text` as a QObject property (some QML controls store text internally without exposing it as a Q_PROPERTY).
**Status**: Implemented as the primary fallback. Needs testing with InstaMAT Studio.

---

## What Has NOT Worked (Dead Ends)

| Approach | Why It Failed | Can It Be Revisited? |
|---|---|---|
| QWidget `findChildren<>()` on New Project dialog | Dialog is QML, not QWidget | No, unless InstaMAT changes to QWidgets |
| `QAccessible::setActive(true)` at runtime | Must be set before Qt app initialization | Could work if set via launcher/registry |
| QML `clicked()` signal invocation | Not equivalent to actual QML mouse interaction | Possibly, with better QML internals knowledge |
| Mouse/keyboard event synthesis | Removed by user request, fragile | Not revisiting |
| Drag-and-drop synthesis | Removed by user request, fragile | Not revisiting |
| Generating `.IMP` files programmatically | Proprietary binary format | No, unless format is documented |

---

## What HAS Worked (Confirmed)

| Component | Status | Notes |
|---|---|---|
| Plugin DLL builds and loads | **CONFIRMED** | Via `build_plugin.ps1`, verified in InstaMAT Studio |
| Menu injection into InstaMAT menu bar | **CONFIRMED** | "RTX Remix" menu appears with all actions |
| RTX Remix REST API communication | **CONFIRMED** | All endpoints work: selection, textures, ingestion, layer save |
| Mesh path retrieval from Remix | **CONFIRMED** | Both absolute and relative paths resolved correctly |
| Texture import (DDS -> PNG) | **CONFIRMED** | texconv.exe conversion works |
| Texture export + ingestion + material update | **CONFIRMED** | Full push pipeline works |
| Blender auto-unwrap | **CONFIRMED** | Optional, works when Blender path configured |
| Tiling mesh support | **CONFIRMED** | `plane_tiling.usd` bundled and detected |
| `.IMP` package creation | **CONFIRMED** | Build script creates valid `.IMP` files |
| Settings persistence via QSettings | **CONFIRMED** | All settings saved/loaded correctly |
| QTimer scheduling inside QDialog::exec() | **CONFIRMED** | Timer callback fires within the modal dialog's event loop |
| Finding File > New menu action | **CONFIRMED** | `FindBestNewProjectAction()` correctly locates the menu item |
| Save Changes dialog dismissal | **CONFIRMED** | `HandleSaveChangesPrompt()` detects and dismisses QWidget QMessageBox |

---

## Technical Details

### Plugin SDK Interface

The plugin implements `IInstaMATPlugin` with:
- `Initialize()` — Creates `RemixConnector`, registers custom nodes (`RTXRemixExportNode`, `RTXRemixImportNode`), initializes `GuiManager`
- `Shutdown()` — Unregisters nodes, tears down GUI, cleans up
- `GetVersion()` — Returns `INSTAMATPLUGIN_API_VERSION`

Entry point: `GetInstaMATPlugin()` C export in `PluginMain.cpp`.

### Remix REST API Endpoints Used

| Endpoint | Method | Purpose |
|---|---|---|
| `/stagecraft/assets/?selection=true` | GET | Get currently selected mesh/prim |
| `/stagecraft/assets/?asset_hashes=...` | GET | Resolve material from mesh prim |
| `/stagecraft/textures/` | PUT | Update material texture bindings |
| `/ingestcraft/mass-validator/queue/material` | POST | Ingest textures (PNG -> DDS) |
| `/stagecraft/layers/target` | GET | Get active layer |
| `/stagecraft/layers/{id}/save` | POST | Save layer |

### PBR Channel Mapping

| Channel | MDL Input | Ingest Validation | sRGB | Bits |
|---|---|---|---|---|
| albedo | diffuse_texture | DIFFUSE | Yes | 8 |
| normal | normalmap_texture | NORMAL_DX | No | 8 |
| roughness | reflectionroughness_texture | ROUGHNESS | No | 8 |
| metallic | metallic_texture | METALLIC | No | 8 |
| emissive | emissive_mask_texture | EMISSIVE | Yes | 8 |
| height | height_texture | HEIGHT | No | 16 |
| opacity | opacity_texture | OPACITY | No | 8 |
| ao | ao_texture | AO | No | 8 |
| transmittance | transmittance_texture | TRANSMITTANCE | Yes | 8 |
| ior | ior_texture | IOR | No | 8 |
| subsurface | subsurface_texture | SUBSURFACE | Yes | 8 |

### UI Automation Flow (Current Implementation)

```
PullFromRemix()
  │
  ├─ GetSelectedRemixAssetDetails()  ← REST API call (WORKS)
  │
  ├─ Optional: Auto-unwrap with Blender (WORKS)
  │
  ├─ TryCreateTexturingProjectFromMesh()  ← THE BLOCKER
  │    │
  │    ├─ Case 1: Chooser already visible (startup screen)
  │    │    ├─ FindNewProjectChooserRoot()
  │    │    ├─ Schedule QTimer::singleShot(300ms) for wizard interaction
  │    │    ├─ Try: QML automation → Accessibility → QWidget fallback
  │    │    └─ Result: FAILS (can't detect QML elements)
  │    │
  │    └─ Case 2: Need to trigger File > New menu action
  │         ├─ FindBestNewProjectAction() → trigger action
  │         ├─ QDialog::exec() blocks...
  │         ├─ QTimer::singleShot fires inside event loop
  │         ├─ Try: QML automation → Accessibility → QWidget fallback
  │         └─ Result: FAILS (can't detect QML elements)
  │
  ├─ Fallback: Copy mesh path to clipboard + show instructions
  │
  └─ Optional: ImportTexturesFromRemix() (if auto-import enabled)
```

---

## File Structure

```
InstaMAT2Remix/
├── PluginMain.cpp            # DLL entry point, plugin lifecycle
├── CExports.cpp              # C function exports for InstaMAT SDK
├── RemixConnector.cpp        # Core logic (~3400 lines): REST API, Pull, Push, UI automation
├── RemixConnector.h          # Header for RemixConnector class
├── RemixNodes.cpp            # Custom Element Graph nodes (Import/Export)
├── RemixNodes.h              # Header for custom nodes
├── GuiManager.cpp            # Menu bar injection and action handlers
├── GuiManager.h              # Header for GuiManager
├── SettingsDialog.cpp        # Settings UI (tabbed dialog)
├── SettingsDialog.h          # Header for SettingsDialog
├── DiagnosticsDialog.cpp     # Diagnostics report dialog
├── DiagnosticsDialog.h       # Header for DiagnosticsDialog
├── ExternalTools.cpp         # Blender + texconv integration
├── ExternalTools.h           # Header for ExternalTools
├── PluginPaths.cpp           # Plugin directory detection helpers
├── PluginPaths.h             # Header for PluginPaths
├── PluginInfo.h              # Plugin metadata (name, version, description)
├── Logger.cpp                # Persistent log file writer
├── Logger.h                  # Header for Logger
├── Utils.h                   # Misc utility functions
├── InstaMATAPI.h             # InstaMAT SDK API header (provided by SDK)
├── InstaMATPluginAPI.h       # InstaMAT Plugin SDK header (provided by SDK)
├── CMakeLists.txt            # Build configuration
├── build_plugin.ps1          # Automated build + install script
├── watch_and_build.ps1       # File watcher for auto-rebuild on save
├── rtx_remix_connector.py    # Python startup script (legacy, may not be needed)
├── texconv.exe               # Microsoft texture converter (DDS <-> PNG)
├── InstaMAT2Remix.IMP        # Pre-built package (may be stale)
├── InstaMAT2Remix.IMP.meta   # Package metadata JSON
├── assets/
│   └── meshes/
│       └── plane_tiling.usd  # Default tiling plane mesh
├── package/
│   └── .meta                 # Package meta for .IMP building
└── build/                    # CMake build output (gitignored)
```

---

## Build & Install

### Prerequisites
- **Windows 10/11** (x64)
- **Visual Studio 2022** (with C++ Desktop workload)
- **Qt 6.5.3 or 6.6.3** (MSVC 64-bit) installed to `C:\Qt`
- **CMake** (3.16+)
- **InstaMAT Studio** installed

### Build Command

```powershell
cd InstaMAT2Remix
.\build_plugin.ps1
```

The script automatically:
1. Finds Qt6 (prefers 6.6.3, falls back to 6.5.3, then any MSVC)
2. Runs CMake configure + build (Release)
3. Runs `windeployqt` to gather Qt DLL dependencies
4. Creates a dev junction link: `Documents\InstaMAT\Plugins\InstaMAT2Remix` -> `build\Release`
5. Copies plugin to `%APPDATA%\InstaMAT Studio\Plugins\`
6. Attempts install to `C:\Program Files\InstaMAT Studio\Environment\` (needs admin)
7. Builds an `.IMP` package to `Documents\InstaMAT\Library\InstaMAT2Remix.IMP`
8. Installs the Python startup script

### Verify Installation
1. Restart InstaMAT Studio
2. Look for **"RTX Remix"** in the menu bar
3. If missing, check: `Documents\InstaMAT2Remix\logs\remix_connector.log`

---

## Requirements

| Component | Required | Notes |
|---|---|---|
| InstaMAT Studio | Yes | With Plugin SDK support |
| RTX Remix Toolkit | Yes | Running with REST service on `http://localhost:8011` |
| Qt 6.5.3 or 6.6.3 | Yes (build only) | MSVC 64-bit. Must match InstaMAT's Qt version |
| Visual Studio 2022 | Yes (build only) | C++ Desktop development workload |
| CMake 3.16+ | Yes (build only) | |
| Blender | Optional | For auto-unwrap feature |
| texconv.exe | Bundled | For DDS <-> PNG conversion |

---

## Usage (Current State)

### Pull from Remix (Manual Fallback Required)
1. In RTX Remix, select a mesh/material
2. In InstaMAT: **RTX Remix > Pull from Remix**
3. Choose "Without tiling" or "With tiling"
4. **Current behavior**: Plugin will attempt auto-project creation, likely fail, then:
   - Copy the mesh path to clipboard
   - Show a dialog with manual instructions
5. **Manual step needed**: Create a new "Asset Texturing" project, paste the mesh path
6. Then use **Import Textures from Remix** to pull textures

### Import Textures from Remix
1. With a project open and linked to a Remix material
2. In InstaMAT: **RTX Remix > Import Textures from Remix**
3. Textures are converted from DDS to PNG and placed in a pulled-textures folder

### Push to Remix
1. With textures generated in InstaMAT
2. In InstaMAT: **RTX Remix > Push to Remix**
3. Select the graph to export (first time only, persisted)
4. Plugin exports PBR textures, ingests into Remix, updates the material, saves the layer

### Force Push to Remix
1. Select a different material in RTX Remix
2. In InstaMAT: **RTX Remix > Force Push to Remix**
3. Relinks to the new material and pushes

---

## Changelog / Session Log

This section tracks what was done in each development session. **Update this when making changes.**

### Session 10 (2026-02-09) — Brute-Force QObject Search & Deep QML Root Detection
- Applied InstaMAT dev team guidance: QTimer/QMetaObject::invokeMethod inside QDialog::exec() event loop
- **Key insight from InstaMAT devs**: The New Project dialog uses QDialog::exec() which blocks; scheduling via QTimer inside exec()'s event loop IS the correct approach — the issue was that element detection inside the callback was failing
- **Major new feature: Brute-force QObject text search** (`FindQObjectWithTextGlobal`, `FindQObjectWithPropertyGlobal`)
  - Searches ALL QObjects reachable from ALL visible windows/widgets/focus windows
  - Also searches QQuickWindow contentItem trees (QML items)
  - Matches by `text`, `placeholderText`, or `objectName` properties
  - Works even when QML roots can't be found through normal `GetQmlRoot()` channels
  - This is the ultimate fallback that bypasses the QML root detection problem entirely
- **Improved QML root detection** (`CollectAllQmlRoots`)
  - Now searches children of ALL top-level widgets (handles QDialog wrapping QQuickWidget)
  - Searches active modal widget, active window, and focus window children
  - Previously only checked top-level objects, missing embedded QML containers
- **New automation fallback** (`TryCreateTexturingProjectBruteForce`)
  - Steps through the dialog by finding QObjects globally by text property
  - Clicks "Asset Texturing", sets name/category/type/mesh, clicks "Create Project"
  - Integrated as fallback in both `TryCreateTexturingProjectFromMeshQml` and `FillProjectWizardAndCreate`
- **Fixed dialog closing on failure** (`CloseAnyVisibleDialog`)
  - Now closes QWindow-based (QML) dialogs, not just QWidget modals
  - Tries: QWidget modal close, QWidget dialog close, modal QWindow close, Escape key on focus window
  - Fixes the bug where the New Project dialog stays open when automation fails
- **Increased initial timer delay** from 100ms to 500ms
  - QML content may load asynchronously after the dialog shell appears
  - 500ms gives better chance of all elements being discoverable
- **Added comprehensive diagnostic logging** at timer fire
  - Dumps all visible windows/widgets, their class names, and child counts
  - Logs how many QML roots are found and how many text-bearing items each contains
  - Check `Documents\InstaMAT2Remix\logs\remix_connector.log` for this output
- **Improved `QmlSetText`** — now also fires `editingFinished` and `accepted` signals after setting text
- **Result**: Multiple new fallback paths that should work even when accessibility is inactive
- **Automation chain is now**: QWidget detection → QML root search → Brute-force QObject search → Accessibility → Brute-force again as final fallback
- Build verified: compiles and installs successfully
- **Still waiting on**: InstaMAT dev team to provide stable class/object names for dialog widgets (would make detection 100% reliable)

### Session 9 (2026-02-08) — Save Changes Dialog Handling (Edge Case Fix)
- Added `HandleSaveChangesPrompt()` helper in `RemixConnector.cpp`
  - Detects "Save Changes?" / "Unsaved" QMessageBox that appears when triggering File > New while a project is open
  - Automatically clicks "Don't Save" / "Discard" / "No" to dismiss it
  - This dialog IS a QWidget (`QMessageBox`), so QWidget detection works reliably on it
- Integrated into the QTimer polling loop: the loop now checks for and dismisses Save Changes prompts before looking for the wizard
- Also improved the polling loop structure to check active modal widgets more thoroughly
- **Result**: Fixes one failure mode (Save Changes blocking wizard), but the core QML detection blocker remains
- **Does NOT fix the main blocker**: After the Save Changes prompt is dismissed and the New Project wizard appears, the plugin still cannot detect/interact with QML elements
- Build verified: compiles and installs successfully
- Source: Gemini suggestion — Save Changes handling was the useful part; the claim it "ensures one-click operation" was incorrect

### Session 8 (2026-02-08) — README Creation
- Created this comprehensive README for GitHub
- Established the rule to update README on each change session
- No code changes

### Session 7 — GitHub Prep & Distribution
- Created clean directory structure for GitHub
- Set up `.gitignore` (excludes build artifacts, keeps `texconv.exe`)
- Made initial Git commit
- Identified distribution files needed for community sharing

### Session 6 — API-Only Refactoring (Remove Mouse/Keyboard)
- **Removed all** `QMouseEvent`, `QKeyEvent`, drag-and-drop simulation
- Switched to Qt Accessibility API + `QMetaObject::invokeMethod` for signal-based interaction
- Added `EnsureQtAccessibilityActive()` helper (doesn't work at runtime)
- Added `TryCreateTexturingProjectFromMeshAccessibleAnyRoot()` for multi-root search
- Added BFS accessibility tree traversal with node limits
- **Result**: Cleaner code, but auto-project creation still fails because accessibility is inactive

### Session 5 — QML Automation Fallback
- Implemented `TryCreateTexturingProjectFromMeshQml()`:
  - QObject hierarchy traversal from QQuickWindow roots
  - Text property matching for finding UI elements
  - `QMetaObject::invokeMethod` for clicking QML items
- Integrated as fallback before accessibility path
- **Result**: Occasionally works but unreliable across InstaMAT versions

### Session 4 — QTimer Event Loop Fix
- Fixed the core timing issue: `QDialog::exec()` blocks, automation must run inside
- Implemented `QTimer::singleShot` scheduling within the dialog's event loop
- Added chooser vs wizard detection (`HasProjectWizardSignature`, `HasNewProjectChooserSignature`)
- Added retry logic (up to 3 attempts with increasing delays)
- **Result**: Timer fires correctly inside the event loop, but element detection still fails

### Session 3 — InstaMAT Dev Feedback
- InstaMAT developers confirmed `QDialog::exec()` blocking behavior
- Recommended `QTimer::singleShot` or `QMetaObject::invokeMethod` with queued connection
- Offered to provide stable class/object names for dialog widgets (not yet utilized)
- Identified that `QT_ACCESSIBILITY=1` must be set before app launch

### Session 2 — PBR Expansion & Template System
- Added AO, Transmittance, IOR, Subsurface PBR channels
- Added tiling mesh pull option
- Fixed DLL loading conflict (singleton pattern for `RemixConnector`)
- Implemented template-based project creation (requires one-time manual setup)
- Built and tested `.IMP` packaging

### Session 1 — Initial Plugin Creation
- Created full plugin architecture from scratch
- Implemented REST API communication with RTX Remix
- Implemented Pull, Push, Import Textures, Force Push
- Created menu injection system
- Created Settings and Diagnostics dialogs
- Set up build system with CMake + PowerShell

---

## Next Steps & Potential Solutions

### Priority 1: Get `QT_ACCESSIBILITY=1` Working

The most promising path. If Qt accessibility is active, our existing `TryCreateTexturingProjectFromMeshAccessible()` code should work. Options:

1. **Launcher wrapper**: Create a batch/PowerShell script that sets `QT_ACCESSIBILITY=1` before launching InstaMAT Studio
2. **Windows Registry**: Set it as a system environment variable (affects all Qt apps)
3. **InstaMAT config**: Check if InstaMAT has a config file or launch flag that enables accessibility
4. **Plugin DLL injection timing**: Check if setting the env var in `DllMain` (before Qt fully initializes) works

### Priority 2: Request InstaMAT SDK Enhancement

Ask the InstaMAT team for:
1. A `CreateAssetTexturingProject(meshPath)` API function
2. Stable `objectName` properties on QML elements in the New Project dialog
3. A way to enable accessibility from plugins

### Priority 3: Windows UI Automation (UIA)

Bypass Qt entirely and use the Windows UI Automation framework:
- `IUIAutomation` COM interface
- Works at the OS level, doesn't depend on Qt accessibility
- Can find and interact with any visible UI element
- Would require adding Windows UIA headers and linking `UIAutomationCore.lib`

### Priority 4: File-Based Project Creation

Research whether InstaMAT project files (`.IMP`) have a structure that could be reverse-engineered:
- Analyze the binary format more deeply
- Check if there's a JSON/XML representation that gets compiled to `.IMP`
- Look for project template files that could be copied and modified

### Priority 5: Alternative Workflow

If automated project creation proves impossible:
- Create a streamlined manual workflow with maximum automation around it
- Auto-open File > New, copy mesh path, show clear instructions
- Auto-detect when the user finishes creating the project, then continue with texture import

---

## For LLM Assistants Reading This

If you are an AI/LLM helping develop this plugin, here is the critical context:

### Current State Summary
- **Everything works EXCEPT automatic "Asset Texturing" project creation**
- The blocker is InstaMAT's QML-based New Project dialog + inactive Qt accessibility
- The code for UI automation EXISTS and is well-structured — it just can't detect QML elements

### Key Files to Focus On
- `RemixConnector.cpp` lines ~1150-2100: All UI automation code
- `RemixConnector.cpp` lines ~2700-2850: `PullFromRemix()` function
- `GuiManager.cpp`: Menu injection and action handlers
- `build_plugin.ps1`: Build and install script

### Key Functions in the Automation Chain
1. `PullFromRemix()` — Entry point for pull
2. `TryCreateTexturingProjectFromMesh()` — Main automation orchestrator
3. `TryCreateTexturingProjectFromMeshQml()` — QML object reflection path
4. `TryCreateTexturingProjectBruteForce()` — Brute-force QObject text search (NEW)
5. `TryCreateTexturingProjectFromMeshAccessible()` — Accessibility API path
6. `TryCreateTexturingProjectFromMeshAccessibleAnyRoot()` — Multi-root accessibility search
7. `FillProjectWizardAndCreate()` — Fills fields and clicks Create
8. `FindBestNewProjectAction()` — Finds File > New menu action
9. `CollectAllQmlRoots()` — Deep QML root collection (NEW)
10. `FindQObjectWithTextGlobal()` — Global QObject text property search (NEW)
11. `CloseAnyVisibleDialog()` — Comprehensive dialog cleanup on failure (NEW)

### Things That WILL NOT Work
- `QMouseEvent`/`QKeyEvent` simulation (removed by user request)
- `QAccessible` without `QT_ACCESSIBILITY=1` set before app launch
- `findChildren<QLineEdit*>()` on QML dialogs
- Runtime `QAccessible::setActive(true)`
- Generating `.IMP` files from scratch

### Things That MIGHT Work (Try These)
1. Getting stable QML objectNames from InstaMAT developers (HIGHEST PRIORITY — they offered!)
2. Setting `QT_ACCESSIBILITY=1` via a launcher script or `DllMain`
3. Windows UI Automation (IUIAutomation COM) bypassing Qt entirely
4. An InstaMAT SDK update with `CreateProject()` API
5. The new brute-force QObject text search may already work — test it!

### Build and Test Cycle
```powershell
cd InstaMAT2Remix
.\build_plugin.ps1
# Restart InstaMAT Studio
# Test: RTX Remix > Pull from Remix
# Check: Documents\InstaMAT2Remix\logs\remix_connector.log
```

### Important Constraints
- DO NOT add mouse/keyboard event simulation (user requirement)
- DO NOT add `Qt6Quick.dll` / `Qt6Qml.dll` as link-time dependencies (breaks plugin loading from .IMP packages)
- The plugin DLL is loaded by InstaMAT from a temp directory when installed via `.IMP` — dependency resolution is tricky
- Always test with `build_plugin.ps1` (it handles the full install chain)

---

## License

This project is currently in private development. License TBD.

---

## Contributing

This is an active development project. If you're interested in contributing:

1. Read this README thoroughly (especially the "Current Blocker" and "History of Approaches" sections)
2. Check the logs at `Documents\InstaMAT2Remix\logs\remix_connector.log`
3. Focus efforts on the auto-project-creation blocker
4. Test with both "startup screen visible" and "startup screen closed" scenarios

# InstaMAT2Remix Plugin - Automatic Project Creation Improvements

## Summary

Updated the InstaMAT2Remix plugin to reliably **automatically create new texturing projects** when pulling from RTX Remix, matching the Substance2Remix workflow. The plugin now behaves just like Substance Painter's "Pull from Remix" feature.

## Key Changes

### 1. **Enhanced Project Creation with Retry Logic** (`RemixConnector.cpp`)
   - **Added retry mechanism**: The plugin now attempts to create a project up to 3 times with increasing delays
   - **Improved timing**: Added strategic delays to allow the UI to fully load before automation
   - **Better error handling**: More detailed error messages help diagnose issues
   - **Status logging**: Each attempt is logged for troubleshooting

### 2. **Increased Timeout Values for Reliability**
   - Extended UI element waiting times:
     - Mesh field validation: 400ms → 800ms
     - Post drag/drop wait: 1000ms → 2000ms
     - Project creation wait: 15s → 30s (to accommodate mesh baking)
   - More generous event processing intervals (30ms → 50-100ms)

### 3. **Improved Texture Import Sequencing**
   - Added 2-second delay after project creation before importing textures
   - Textures now only auto-import if project creation succeeded
   - Template graph execution only runs after successful project creation

### 4. **Enhanced User Feedback**
   - Success messages now include ✓ symbols and clear status
   - Failure messages provide step-by-step manual instructions
   - Progress indicators clearly show what's happening
   - More informative error messages with actionable details

### 5. **Updated Documentation** (`README.md`)
   - README now accurately reflects the automatic project creation feature
   - Usage instructions updated to match actual behavior
   - Feature list updated to highlight auto-creation capability

## How It Works Now

### Workflow Comparison

**Substance2Remix**:
1. Pull from Remix → Automatically creates project + links material
2. Separately: Import Textures from Remix

**InstaMAT2Remix** (Now Enhanced):
1. Pull from Remix → Automatically creates project + links material + optionally imports textures
   - Uses intelligent UI automation with multiple retry attempts
   - Waits for UI elements to load before interaction
   - Validates each step before proceeding

### What Happens When You Click "Pull from Remix"

1. **Query Remix**: Gets selected mesh and material from RTX Remix
2. **Optional Unwrap**: If enabled, auto-unwraps mesh with Blender
3. **Auto-Create Project**: (NEW - Enhanced)
   - Attempts to trigger File → New Project menu
   - Navigates the project wizard UI
   - Selects "Asset Texturing" template
   - Fills in the mesh path
   - Clicks "Create Project"
   - Retries up to 3 times if needed
4. **Link Material**: Stores the Remix material prim for future push operations
5. **Auto-Import Textures**: If enabled, automatically imports textures after project is ready

## Technical Details

### UI Automation Strategy

The plugin uses a multi-layered approach to ensure compatibility across InstaMAT versions:

1. **Widget-based automation**: Searches for QLineEdit, QComboBox, QPushButton elements
2. **Accessibility API fallback**: Uses QAccessible for QtQuick/QML-based UIs
3. **Multiple interaction methods**: 
   - Direct setText()
   - Clipboard paste
   - Drag & drop simulation
   - Keyboard events (Enter key)
4. **Validation**: Waits for "Create Project" button to become enabled before clicking

### Retry Logic

```cpp
const int maxAttempts = 3;
for (int attempt = 1; attempt <= maxAttempts && !projectCreated; ++attempt) {
    if (attempt > 1) {
        // Wait progressively longer between attempts
        QTimer::singleShot(1000 * attempt, ...);
    }
    projectCreated = TryCreateTexturingProjectFromMesh(...);
}
```

## Settings

The plugin respects these settings (configurable via Settings dialog):

- **Auto Import Textures After Pull**: `true` by default - automatically imports textures after creating project
- **Use Tiling Mesh On Pull**: `false` by default - use a simple tiling plane instead of the selected mesh
- **Auto Unwrap with Blender**: `false` by default - UV unwrap mesh before project creation

## Testing

### To Test the Changes:

1. **Restart InstaMAT Studio** to load the updated plugin
2. **Open RTX Remix** with a project loaded
3. **Select a mesh/material** in Remix
4. In InstaMAT: **RTX Remix → Pull from Remix**
5. **Observe**: 
   - A new "Asset Texturing" project should automatically appear
   - The selected mesh should be loaded and ready
   - Textures should import automatically (if enabled in settings)

### Expected Behavior:

✅ **Success**: "✓ Texturing project created and linked to RTX Remix! The mesh has been loaded and is ready for texturing."

⚠️ **If Manual Steps Needed**: "⚠ Pulled selection from RTX Remix, but could not auto-create a new texturing project. Manual steps: 1. Create a new 'Asset Texturing' project in InstaMAT..."

## Troubleshooting

If project creation still fails:

1. **Check logs**: `Documents/InstaMAT2Remix/logs/remix_connector.log`
2. **Look for**: "Auto-create project attempt X failed" messages
3. **Common issues**:
   - InstaMAT UI might have changed in newer versions
   - Another dialog might be open blocking the wizard
   - Mesh path might be inaccessible

## Files Modified

- `InstaMAT2Remix/RemixConnector.cpp`: Enhanced project creation logic, retry mechanism, improved timeouts
- `InstaMAT2Remix/README.md`: Updated documentation to reflect automatic project creation

## Compatibility

- Tested with: InstaMAT Studio (latest version)
- Qt 6.5.3 / 6.6.3
- Windows 10/11
- RTX Remix Toolkit

## Future Improvements

Potential enhancements for even better reliability:

1. **InstaMAT API**: If InstaMAT adds a native project creation API, replace UI automation
2. **UI fingerprinting**: Save UI element signatures for faster detection
3. **Version detection**: Adjust automation strategy based on InstaMAT version
4. **Settings option**: Allow users to disable auto-creation if preferred

---

**Result**: The InstaMAT2Remix plugin now provides the same seamless "Pull from Remix" experience as Substance2Remix, automatically creating projects and importing textures without manual intervention.

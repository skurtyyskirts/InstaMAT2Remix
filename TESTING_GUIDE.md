# Testing Guide: InstaMAT2Remix Auto-Create Project Feature

## Quick Start

### Prerequisites
✅ InstaMAT Studio is installed
✅ RTX Remix Toolkit is running (default: http://localhost:8011). **Security recommendation**: Use HTTPS for remote connections.
✅ A Remix project is open with capturedselectable meshes

### Step-by-Step Test

1. **Restart InstaMAT Studio**
   - Close InstaMAT completely
   - Reopen to load the updated plugin
   - Verify plugin loaded: Check for "RTX Remix" menu

2. **Prepare RTX Remix**
   - Open your Remix project
   - Select any mesh/material in the scene
   - Confirm it's highlighted/selected

3. **Test Pull from Remix**
   - In InstaMAT: Click **RTX Remix → Pull from Remix**
   - **Expected**: A dialog should appear with mesh baking options
   - **Expected**: After a few seconds, a new "Asset Texturing" project window opens
   - **Expected**: The selected mesh is loaded and ready
   - **Expected**: Dialog shows: "✓ Texturing project created and linked to RTX Remix!"

4. **Verify Auto-Import** (if enabled in Settings)
   - **Expected**: Textures automatically import after project creation
   - **Expected**: Textures appear in the "Pulled Textures" folder
   - Check: `C:\Users\[YourName]\AppData\Local\Temp\InstaMAT2Remix\Pulled Textures\[ProjectName]`

### Expected Results

#### ✅ Success Scenario
```
Dialog Message:
"✓ Texturing project created and linked to RTX Remix!

The mesh has been loaded and is ready for texturing.

Linked Material Prim:
/path/to/material/HASH

Mesh file (copied to clipboard):
C:/path/to/mesh.usd

Mesh mode: Selected mesh.

Template graph: ✓ Ready

⏳ Importing textures automatically..."
```

#### ⚠️ Manual Fallback (if UI automation fails)
```
Dialog Message:
"⚠ Pulled selection from RTX Remix, but could not auto-create a new texturing project.

Details:
[Error details here]

Manual steps:
1. Create a new 'Asset Texturing' project in InstaMAT
2. Paste the mesh path from clipboard
3. Use 'Import Textures from Remix' to pull textures"
```

## Advanced Testing

### Test Different Scenarios

1. **Test with Simple Mesh**
   - Select a low-poly mesh in Remix
   - Pull from Remix
   - Verify project creates quickly

2. **Test with Complex Mesh**
   - Select a high-poly mesh
   - Enable "Bake Mesh" in the wizard (if visible)
   - Verify project creates (may take longer due to baking)

3. **Test with Tiling Mesh Mode**
   - Open Settings: **RTX Remix → Settings**
   - Enable "Use Simple Tiling Mesh on Pull"
   - Pull from Remix
   - Verify it uses the default tiling plane instead

4. **Test with Auto-Unwrap**
   - Ensure Blender is configured in Settings
   - Pull from Remix with auto-unwrap enabled
   - Verify mesh is UV unwrapped before project creation

5. **Test Retry Logic**
   - Pull from Remix while InstaMAT is busy/loading
   - Verify it retries (check logs for "attempt 2/3" messages)

### Check Logs

View detailed logs at:
```
C:\Users\[YourName]\Documents\InstaMAT2Remix\logs\remix_connector.log
```

Look for entries like:
```
[INFO] Project created successfully on attempt 1
[INFO] Project created successfully on attempt 2
[INFO] Project created successfully on attempt 3
[WARNING] Auto-create project attempt 1 failed: [reason]
[ERROR] All 3 project creation attempts failed. Last error: [reason]
```

## Troubleshooting

### Problem: Project Creation Fails Every Time

**Possible Causes:**
1. InstaMAT UI has changed (newer version)
2. Another dialog is open blocking the wizard
3. Permissions issue accessing the mesh file

**Solutions:**
1. Check logs for specific error messages
2. Try closing all InstaMAT dialogs first
3. Use the manual fallback: Create project manually with clipboard mesh path
4. Report the issue with log file attached

### Problem: Textures Don't Import

**Possible Causes:**
1. Auto-import is disabled in Settings
2. Project creation failed
3. texconv.exe missing or can't convert DDS files

**Solutions:**
1. Enable "Auto Import Textures After Pull" in Settings
2. Verify project was created successfully
3. Manually run: **RTX Remix → Import Textures from Remix**
4. Check texconv.exe exists next to plugin DLL

### Problem: Mesh Path is Invalid

**Possible Causes:**
1. Mesh file is on a network drive
2. Remix returned a relative path without context
3. File permissions issue

**Solutions:**
1. Copy mesh to local drive
2. Use absolute paths in Remix
3. Check clipboard - paste to verify path is correct

## Performance Notes

- **First attempt**: Usually succeeds in 2-5 seconds
- **With retries**: May take up to 10-15 seconds
- **With mesh baking**: May take 20-30 seconds (depends on mesh complexity)
- **With auto-unwrap**: Adds 5-30 seconds (depends on Blender and mesh size)

## Comparison with Substance Painter

### Substance2Remix Workflow
1. Pull from Remix → Creates project
2. Manually: Import Textures from Remix

### InstaMAT2Remix Workflow (Enhanced)
1. Pull from Remix → Creates project + Auto-imports textures (optional)

**Advantage**: InstaMAT can be MORE automated than Substance Painter!

## Next Steps

After successful testing:

1. **Start texturing** in the new project
2. **When ready**: Use **RTX Remix → Push to Remix** to send textures back
3. **For different materials**: Use **RTX Remix → Force Push to Remix** to relink

## Report Issues

If you encounter problems:

1. Collect the log file: `Documents\InstaMAT2Remix\logs\remix_connector.log`
2. Note the InstaMAT version: Help → About
3. Describe what happened vs. what was expected
4. Include any error dialog messages

---

**Happy Texturing! 🎨**

# InstaMAT2Remix (RTX Remix Connector) — installer.
# Copies the plugin into your user folders; no admin rights required.
#
# Usage (from an unzipped release folder):
#   powershell -ExecutionPolicy Bypass -File .\install.ps1
#   powershell -ExecutionPolicy Bypass -File .\install.ps1 -EnablePythonBridge
param(
    [switch]$EnablePythonBridge,
    [switch]$NoPause
)

$ErrorActionPreference = "Stop"

$ProjectName = "InstaMAT2Remix"
$Here = $PSScriptRoot

function Wait-BeforeClosing {
    # Right-click > "Run with PowerShell" closes the window immediately;
    # keep it open so success/failure output is actually readable.
    if (-not $NoPause -and $Host.Name -eq 'ConsoleHost') {
        Read-Host "`nPress Enter to close"
    }
}

try {
    # 0. Warn if InstaMAT Studio is running (it locks the plugin DLL).
    $studio = Get-Process -Name "InstaMATStudio*", "InstaMAT*" -ErrorAction SilentlyContinue |
        Where-Object { $_.ProcessName -notlike "*Remix*" }
    if ($studio) {
        Write-Warning "InstaMAT Studio appears to be running. Close it before installing, then re-run this script."
        Wait-BeforeClosing
        exit 1
    }

    # Resolve Documents via the shell known folder: on Windows 11 it is
    # commonly OneDrive-redirected and is NOT %USERPROFILE%\Documents.
    $Documents = [Environment]::GetFolderPath('MyDocuments')
    if (-not $Documents) { $Documents = Join-Path $env:USERPROFILE "Documents" }

    # Friendly heads-up when Studio isn't installed (the plugin only loads once it is).
    if (-not (Test-Path (Join-Path $env:ProgramFiles "InstaMAT Studio"))) {
        Write-Warning "InstaMAT Studio was not found in $env:ProgramFiles\InstaMAT Studio. Files will be installed, but the plugin only loads once Studio is installed."
    }

    # 1. Plugin folder (DLL + Qt runtime + texconv + export worker + assets).
    $PluginDst = Join-Path $Documents "InstaMAT\Plugins\$ProjectName"
    Write-Host "Installing plugin files -> $PluginDst"
    New-Item -ItemType Directory -Path $PluginDst -Force | Out-Null
    Copy-Item (Join-Path $Here "plugin\*") -Destination $PluginDst -Recurse -Force

    # 2. Bare DLL copy in the Plugins root (some Studio setups scan here).
    $PluginsRoot = Join-Path $Documents "InstaMAT\Plugins"
    Copy-Item (Join-Path $Here "plugin\${ProjectName}Plugin.dll") -Destination $PluginsRoot -Force

    # 3. .IMP package into the Library (this is what registers the plugin).
    $LibraryDst = Join-Path $Documents "InstaMAT\Library"
    New-Item -ItemType Directory -Path $LibraryDst -Force | Out-Null
    Copy-Item (Join-Path $Here "$ProjectName.IMP") -Destination $LibraryDst -Force
    Write-Host "Installed package -> $(Join-Path $LibraryDst "$ProjectName.IMP")"

    # 4. Optional Python bridge (off by default; requires
    #    INSTAMAT2REMIX_ENABLE_PYTHON_BRIDGE=1 to activate).
    if ($EnablePythonBridge) {
        $startupDirs = @(
            (Join-Path $env:APPDATA "InstaMAT Studio\scripts\startup"),
            (Join-Path $env:APPDATA "InstaMAT Studio\Scripts\Startup"),
            (Join-Path $env:APPDATA "InstaMAT Studio\Startup")
        )
        foreach ($dir in $startupDirs) {
            New-Item -ItemType Directory -Path $dir -Force | Out-Null
            Copy-Item (Join-Path $Here "scripts\rtx_remix_connector.py") -Destination $dir -Force
        }
        Write-Host "Python bridge installed. Set INSTAMAT2REMIX_ENABLE_PYTHON_BRIDGE=1 to activate it."
    }

    Write-Host ""
    Write-Host "=== INSTALL COMPLETE ==="
    Write-Host "Next steps:"
    Write-Host "  1. Start RTX Remix Toolkit with its REST API enabled (default port 8011)."
    Write-Host "  2. Start InstaMAT Studio."
    Write-Host "  3. Look for the 'RTX Remix Connector' menu in the menu bar."
    Write-Host "  4. Select a mesh or material in Remix, then use 'Pull From Remix'."
    Write-Host "See README.md for the full walkthrough and troubleshooting."
    Wait-BeforeClosing
} catch {
    Write-Host ""
    Write-Error "INSTALL FAILED: $($_.Exception.Message)"
    Write-Host "Nothing may have been fully installed. Fix the problem above and re-run install.ps1."
    Wait-BeforeClosing
    exit 1
}

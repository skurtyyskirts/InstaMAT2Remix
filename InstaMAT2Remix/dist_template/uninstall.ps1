# InstaMAT2Remix (RTX Remix Connector) — uninstaller.
# Removes everything install.ps1 put on this machine. User data
# (Documents\InstaMAT2Remix: logs, pulled textures, mesh cache) is kept unless
# you pass -RemoveUserData (which also clears the plugin's saved settings).
param(
    [switch]$RemoveUserData,
    [switch]$NoPause
)

$ErrorActionPreference = "Continue"

$ProjectName = "InstaMAT2Remix"

function Wait-BeforeClosing {
    if (-not $NoPause -and $Host.Name -eq 'ConsoleHost') {
        Read-Host "`nPress Enter to close"
    }
}

# Studio locks the plugin DLL while running — removal would silently fail.
$studio = Get-Process -Name "InstaMATStudio*", "InstaMAT*" -ErrorAction SilentlyContinue |
    Where-Object { $_.ProcessName -notlike "*Remix*" }
if ($studio) {
    Write-Warning "InstaMAT Studio appears to be running. Close it before uninstalling, then re-run this script."
    Wait-BeforeClosing
    exit 1
}

# Documents may be OneDrive-redirected; resolve via the shell known folder.
$Documents = [Environment]::GetFolderPath('MyDocuments')
if (-not $Documents) { $Documents = Join-Path $env:USERPROFILE "Documents" }

$targets = @(
    (Join-Path $Documents "InstaMAT\Plugins\$ProjectName"),
    (Join-Path $Documents "InstaMAT\Plugins\${ProjectName}Plugin.dll"),
    (Join-Path $Documents "InstaMAT\Library\$ProjectName.IMP")
)
foreach ($dir in @("scripts\startup", "Scripts\Startup", "Startup")) {
    $targets += (Join-Path $env:APPDATA "InstaMAT Studio\$dir\rtx_remix_connector.py")
}
if ($RemoveUserData) {
    $targets += (Join-Path $Documents $ProjectName)
}

$failures = @()
foreach ($t in $targets) {
    if (Test-Path $t) {
        try {
            Remove-Item $t -Recurse -Force -Confirm:$false -ErrorAction Stop
            Write-Host "Removed: $t"
        } catch {
            $failures += $t
            Write-Warning "Could not remove: $t ($($_.Exception.Message))"
        }
    }
}

# Saved settings + Remix link state live in the registry (QSettings).
if ($RemoveUserData) {
    $regPath = "HKCU:\Software\$ProjectName"
    if (Test-Path $regPath) {
        try {
            Remove-Item $regPath -Recurse -Force -ErrorAction Stop
            Write-Host "Removed settings: $regPath"
        } catch {
            $failures += $regPath
            Write-Warning "Could not remove settings: $regPath"
        }
    }
}

Write-Host ""
if ($failures.Count -gt 0) {
    Write-Warning "UNINSTALL INCOMPLETE — $($failures.Count) item(s) could not be removed (see warnings above)."
    Wait-BeforeClosing
    exit 1
}

Write-Host "=== UNINSTALL COMPLETE ==="
if (-not $RemoveUserData) {
    Write-Host "User data kept at $(Join-Path $Documents $ProjectName) (logs, pulled textures)."
    Write-Host "Re-run with -RemoveUserData to delete it (and the plugin's saved settings) too."
}
Wait-BeforeClosing

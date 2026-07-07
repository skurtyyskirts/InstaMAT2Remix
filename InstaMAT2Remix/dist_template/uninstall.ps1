# InstaMAT2Remix (RTX Remix Connector) — uninstaller.
# Removes everything install.ps1 put on this machine. User data
# (Documents\InstaMAT2Remix: logs, pulled textures, mesh cache) is kept unless
# you pass -RemoveUserData.
param(
    [switch]$RemoveUserData
)

$ErrorActionPreference = "Continue"

$ProjectName = "InstaMAT2Remix"

$targets = @(
    (Join-Path $env:USERPROFILE "Documents\InstaMAT\Plugins\$ProjectName"),
    (Join-Path $env:USERPROFILE "Documents\InstaMAT\Plugins\${ProjectName}Plugin.dll"),
    (Join-Path $env:USERPROFILE "Documents\InstaMAT\Library\$ProjectName.IMP")
)
foreach ($dir in @("scripts\startup", "Scripts\Startup", "Startup")) {
    $targets += (Join-Path $env:APPDATA "InstaMAT Studio\$dir\rtx_remix_connector.py")
}
if ($RemoveUserData) {
    $targets += (Join-Path $env:USERPROFILE "Documents\$ProjectName")
}

foreach ($t in $targets) {
    if (Test-Path $t) {
        Remove-Item $t -Recurse -Force -Confirm:$false
        Write-Host "Removed: $t"
    }
}

Write-Host "`n=== UNINSTALL COMPLETE ==="
if (-not $RemoveUserData) {
    Write-Host "User data kept at Documents\$ProjectName (logs, pulled textures)."
    Write-Host "Re-run with -RemoveUserData to delete it too."
}

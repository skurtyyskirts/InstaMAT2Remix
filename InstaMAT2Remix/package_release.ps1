# package_release.ps1 — builds a shareable release zip of InstaMAT2Remix.
#
# Output: dist\InstaMAT2Remix-v<version>-win64.zip containing the plugin DLL,
# the full Qt runtime (windeployqt, WITH the MSVC compiler runtime for end
# users), texconv, assets, the generated .IMP package, the optional Python
# bridge script, install/uninstall scripts, and user docs.
#
# Usage:
#   .\package_release.ps1              # build Release, then package
#   .\package_release.ps1 -SkipBuild   # package the existing build\Release
param(
    [string]$Version = "",
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"

$ProjectName = "InstaMAT2Remix"
$PluginDllName = "${ProjectName}Plugin.dll"
$RepoRoot = $PSScriptRoot
$BuildOutputDir = Join-Path $RepoRoot "build\Release"
$DistDir = Join-Path $RepoRoot "dist"

. (Join-Path $RepoRoot "tools\imp_packaging.ps1")

# 1. Resolve version from PluginInfo.h unless provided.
if (-not $Version) {
    $pluginInfo = Get-Content (Join-Path $RepoRoot "PluginInfo.h") -Raw
    if ($pluginInfo -match 'kPluginVersion\s*=\s*"([^"]+)"') {
        $Version = $Matches[1]
    } else {
        throw "Could not parse kPluginVersion from PluginInfo.h; pass -Version explicitly."
    }
}
Write-Host "Packaging $ProjectName v$Version"

# 2. Build (no local install) unless skipped.
if (-not $SkipBuild) {
    & (Join-Path $RepoRoot "build_plugin.ps1") -InstallMode None
    if ($LASTEXITCODE -ne 0) { throw "Build failed." }
}

$SourceDll = Join-Path $BuildOutputDir $PluginDllName
if (-not (Test-Path $SourceDll)) {
    throw "Build artifact not found: $SourceDll (run without -SkipBuild first)."
}

# 3. Stage the release layout.
$StageRoot = Join-Path $DistDir "$ProjectName-v$Version"
if (Test-Path $StageRoot) { Remove-Item -Recurse -Force $StageRoot }
$PluginStage = Join-Path $StageRoot "plugin"
$ScriptsStage = Join-Path $StageRoot "scripts"
New-Item -ItemType Directory -Path $PluginStage -Force | Out-Null
New-Item -ItemType Directory -Path $ScriptsStage -Force | Out-Null

Copy-Item $SourceDll -Destination $PluginStage -Force
Copy-Item (Join-Path $RepoRoot "texconv.exe") -Destination $PluginStage -Force
# Out-of-process export worker — Push spawns this instead of executing the
# layer graph inside Studio (GL thread-affinity fail-fast on Studio 3.1+).
$WorkerExe = Join-Path $BuildOutputDir "InstaMAT2RemixExport.exe"
if (-not (Test-Path $WorkerExe)) {
    throw "Build artifact not found: $WorkerExe (target InstaMAT2RemixExport)."
}
Copy-Item $WorkerExe -Destination $PluginStage -Force
$assetsDst = Join-Path $PluginStage "assets\meshes"
New-Item -ItemType Directory -Path $assetsDst -Force | Out-Null
Copy-Item (Join-Path $RepoRoot "assets\meshes\plane_tiling.usd") -Destination $assetsDst -Force

# 3b. Bundle the MSVC runtime (VCRUNTIME140/VCRUNTIME140_1/MSVCP140) app-local.
# InstaMAT2RemixExport.exe is a SEPARATE process; its static CRT imports are
# resolved from its own directory at load time, so without these DLLs it fails
# to start on any machine that lacks the VC++ 2015-2022 x64 redistributable
# (Studio ships its own CRT app-locally, so running Studio does NOT guarantee a
# system-wide redist). windeployqt --compiler-runtime does not reliably copy
# these, so we do it explicitly from the VS redist matching the build toolset.
$crtDlls = @("vcruntime140.dll", "vcruntime140_1.dll", "msvcp140.dll")
$crtSrcDir = $null
foreach ($pf in @($env:ProgramFiles, ${env:ProgramFiles(x86)})) {
    if (-not $pf) { continue }
    Get-ChildItem "$pf\Microsoft Visual Studio" -Directory -ErrorAction SilentlyContinue |
        ForEach-Object { Get-ChildItem $_.FullName -Directory -ErrorAction SilentlyContinue } |
        ForEach-Object {
            $root = Join-Path $_.FullName "VC\Redist\MSVC"
            if (Test-Path $root) {
                $cand = Get-ChildItem $root -Directory -ErrorAction SilentlyContinue |
                    Sort-Object Name -Descending |
                    ForEach-Object { Join-Path $_.FullName "x64\Microsoft.VC143.CRT" } |
                    Where-Object { Test-Path (Join-Path $_ "vcruntime140.dll") } |
                    Select-Object -First 1
                if ($cand -and -not $crtSrcDir) { $script:crtSrcDir = $cand }
            }
        }
}
if ($crtSrcDir) {
    foreach ($f in $crtDlls) { Copy-Item (Join-Path $crtSrcDir $f) -Destination $PluginStage -Force }
    Write-Host "Bundled MSVC runtime from $crtSrcDir"
} else {
    Write-Warning "MSVC runtime (VC143.CRT) not found under any VS redist. The export worker will require the VC++ 2015-2022 x64 redistributable on the target machine."
}

# 4. Qt runtime via windeployqt — include the compiler runtime for end users
#    (the dev script deliberately omits it).
$QtRoot = "C:\Qt"
$Windeployqt = $null
foreach ($ver in @("6.6.3", "6.5.3")) {
    $candidate = Get-ChildItem -Path "$QtRoot\$ver" -Directory -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -match "msvc20(19|22)_64" } |
        ForEach-Object { Join-Path $_.FullName "bin\windeployqt.exe" } |
        Where-Object { Test-Path $_ } | Select-Object -First 1
    if ($candidate) { $Windeployqt = $candidate; break }
}
if (-not $Windeployqt) { throw "windeployqt.exe not found under C:\Qt (6.6.3/6.5.3 MSVC)." }

& $Windeployqt (Join-Path $PluginStage $PluginDllName) `
    --release --no-translations --no-opengl-sw --compiler-runtime
if ($LASTEXITCODE -ne 0) { throw "windeployqt failed." }

# 5. Generate the .IMP package (self-contained MAT container).
New-ImpPackage -OutPath (Join-Path $StageRoot "$ProjectName.IMP") `
    -DllPath $SourceDll `
    -MetaPath (Join-Path $RepoRoot "$ProjectName.IMP.meta") `
    -ExtraResources @{ "plane_tiling.usd" = (Join-Path $RepoRoot "assets\meshes\plane_tiling.usd") }

# 6. Scripts + docs.
Copy-Item (Join-Path $RepoRoot "rtx_remix_connector.py") -Destination $ScriptsStage -Force
foreach ($doc in @("install.ps1", "uninstall.ps1", "README.md", "THIRD_PARTY_LICENSES.md")) {
    Copy-Item (Join-Path $RepoRoot "dist_template\$doc") -Destination $StageRoot -Force
}

# 7. Zip it.
$ZipPath = Join-Path $DistDir "$ProjectName-v$Version-win64.zip"
if (Test-Path $ZipPath) { Remove-Item $ZipPath -Force }
Compress-Archive -Path $StageRoot -DestinationPath $ZipPath
Write-Host "`n=== RELEASE PACKAGED ==="
Write-Host "  $ZipPath"

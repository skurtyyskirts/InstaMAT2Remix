$ErrorActionPreference = "Stop"

$ProjectName = "InstaMAT2Remix"
$PluginDllName = "${ProjectName}Plugin.dll"
$LegacyDllName = "${ProjectName}.dll"
$BuildDir = "build"
$BuildConfig = "Release"

$RepoRoot = $PSScriptRoot
$BuildDirFull = Join-Path $RepoRoot $BuildDir
$BuildOutputDir = Join-Path $BuildDirFull $BuildConfig
$DevPluginLinkPath = Join-Path $env:USERPROFILE ("Documents\\InstaMAT\\Plugins\\$ProjectName")
$UserPluginsRoot = Join-Path $env:USERPROFILE ("Documents\\InstaMAT\\Plugins")
$UserLibraryRoot = Join-Path $env:USERPROFILE ("Documents\\InstaMAT\\Library")
$TilingMeshSrc = Join-Path $RepoRoot "assets\\meshes\\plane_tiling.usd"

# Define potential install paths
$InstallPaths = @(
    "$env:APPDATA\InstaMAT Studio\Plugins",
    "C:\Program Files\InstaMAT Studio\Environment"
)

Write-Host "Starting build for $ProjectName using CMake..."

# 1. Find Qt6 (MSVC)
# Priority: 6.6.3 > 6.5.3 > Any MSVC
$QtRoot = "C:\Qt"
$QtPath = $null

if (Test-Path $QtRoot) {
    $SpecificVersions = @("6.6.3", "6.5.3")
    
    foreach ($ver in $SpecificVersions) {
        if (Test-Path "$QtRoot\$ver") {
            $msvcPaths = Get-ChildItem -Path "$QtRoot\$ver" | Where-Object { $_.Name -match "msvc20(19|22)_64" }
            if ($msvcPaths) {
                $QtPath = $msvcPaths[0].FullName
                Write-Host "Found preferred Qt version $ver at: $QtPath"
                break
            }
        }
    }

    if (-not $QtPath) {
        # Fallback to any MSVC version
        $PossiblePaths = Get-ChildItem -Path $QtRoot -Recurse -Depth 2 | Where-Object { 
            $_.PSIsContainer -and 
            ($_.Name -match "msvc20(19|22)_64") -and
            (Test-Path "$($_.FullName)\bin\qmake.exe")
        } | Sort-Object FullName
        
        if ($PossiblePaths) {
            $QtPath = $PossiblePaths[0].FullName
            Write-Host "Found Qt6 at: $QtPath"
        }
    }
}

if (-not $QtPath) {
    Write-Error "Could not find a valid Qt6 (MSVC 64-bit) installation in C:\Qt. Please install Qt 6.5 or 6.6 using the Maintenance Tool."
}

# 2. Check for CMake (auto-detect; don't require PATH)
# Prefer well-known install locations first (avoids broken Python wrapper shims).
$CMakeExe = $null
$CommonCMake = @(
    "C:\Program Files\CMake\bin\cmake.exe",
    "C:\Program Files (x86)\CMake\bin\cmake.exe"
)
foreach ($p in $CommonCMake) {
    if (Test-Path $p) { $CMakeExe = $p; break }
}

if (-not $CMakeExe) {
    try {
        $cmd = Get-Command "cmake" -ErrorAction SilentlyContinue
        if ($cmd -and $cmd.Source) {
            # Validate that cmake actually works (Python shims may be non-functional)
            $ver = & $cmd.Source --version 2>&1 | Select-Object -First 1
            if ($ver -match "cmake version") { $CMakeExe = $cmd.Source }
        }
    } catch {}
}

if (-not $CMakeExe) {
    Write-Error "CMake not found. Install CMake or ensure it is discoverable."
} else {
    Write-Host "Using CMake: $CMakeExe"
}

# 3. Setup Build Directory
if (Test-Path $BuildDirFull) {
    Remove-Item -Recurse -Force $BuildDirFull
}
New-Item -ItemType Directory -Path $BuildDirFull | Out-Null

# 4. Configure
Push-Location $BuildDirFull
try {
    $cmakeArgs = @($RepoRoot, "-G", "Visual Studio 17 2022", "-A", "x64")
    
    if ($QtPath) {
        $cmakeArgs += "-DCMAKE_PREFIX_PATH=$QtPath"
    }
    
    & $CMakeExe @cmakeArgs
} catch {
    Write-Error "CMake configuration failed."
}

# 5. Build
try {
    & $CMakeExe --build . --config $BuildConfig
} catch {
    Write-Error "Build failed."
}
Pop-Location

# 6. Install
$SourceDll = $null
$CandidateDirs = @(
    $BuildOutputDir,
    (Join-Path $BuildDirFull "Release"),
    (Join-Path $BuildDirFull "Debug")
)
foreach ($dir in $CandidateDirs) {
    foreach ($name in @($PluginDllName, $LegacyDllName)) {
        $candidate = Join-Path $dir $name
        if (Test-Path $candidate) {
            $SourceDll = $candidate
            # Update BuildOutputDir to the actual location where the DLL was found
            $BuildOutputDir = $dir 
            break
        }
    }
    if ($SourceDll) { break }
}

if ($SourceDll -and (Test-Path $SourceDll)) {
    
    # 6.0 Run windeployqt to deploy dependencies
    if ($QtPath) {
        $Windeployqt = Join-Path $QtPath "bin\windeployqt.exe"
        if (Test-Path $Windeployqt) {
            Write-Host "Running windeployqt to gather dependencies..."
            # Deploy to the build output directory so they can be copied along with the DLL
            try {
                & $Windeployqt $SourceDll --dir $BuildOutputDir --no-translations --no-opengl-sw --no-compiler-runtime
                Write-Host "SUCCESS: windeployqt completed."
            } catch {
                Write-Warning "windeployqt failed: $($_.Exception.Message)"
            }
        } else {
            Write-Warning "windeployqt.exe not found at $Windeployqt"
        }
    }

    # 6.1 Dev install: link the user plugin folder to the build output
    Write-Host "Setting up DEV install link (so builds reflect instantly)..."
    Write-Host "  Link:   $DevPluginLinkPath"
    Write-Host "  Target: $BuildOutputDir"

    try {
        if (-not (Test-Path $BuildOutputDir)) {
            throw "Build output directory not found: $BuildOutputDir"
        }

        # Remove existing link/directory
        if (Test-Path $DevPluginLinkPath) {
            # Use cmd to remove junction if powershell fails
            if ((Get-Item $DevPluginLinkPath).Attributes -band [IO.FileAttributes]::ReparsePoint) {
                 cmd /c rmdir "$DevPluginLinkPath"
            } else {
                 Remove-Item $DevPluginLinkPath -Recurse -Force
            }
        }

        # Try to create Junction using cmd /c mklink /J (often more reliable)
        # Check if parent dir exists
        $parent = Split-Path $DevPluginLinkPath -Parent
        if (-not (Test-Path $parent)) { New-Item -ItemType Directory -Path $parent -Force | Out-Null }
        
        cmd /c mklink /J "$DevPluginLinkPath" "$BuildOutputDir"
        
        if (-not (Test-Path $DevPluginLinkPath)) {
            throw "Junction creation failed."
        }
        Write-Host "SUCCESS: Dev plugin folder linked."
    } catch {
        Write-Warning "FAILED to create dev link at $DevPluginLinkPath. Falling back to copy install."
        try {
            if (-not (Test-Path $DevPluginLinkPath)) {
                New-Item -ItemType Directory -Path $DevPluginLinkPath -Force | Out-Null
            }
            # Copy EVERYTHING from build output (including Qt dependencies)
            Copy-Item "$BuildOutputDir\*" -Destination $DevPluginLinkPath -Recurse -Force
            
            $TexconvSrc = Join-Path $RepoRoot "texconv.exe"
            if (Test-Path $TexconvSrc) {
                Copy-Item $TexconvSrc -Destination $DevPluginLinkPath -Force
            }
            if (Test-Path $TilingMeshSrc) {
                $dst = Join-Path $DevPluginLinkPath "assets\\meshes"
                New-Item -ItemType Directory -Path $dst -Force | Out-Null
                Copy-Item $TilingMeshSrc -Destination (Join-Path $dst "plane_tiling.usd") -Force
            }
            Write-Host "SUCCESS: Copied plugin and dependencies to $DevPluginLinkPath"
        } catch {
            Write-Warning "Fallback copy install FAILED for $DevPluginLinkPath. Error: $($_.Exception.Message)"
        }
    }

    # Keep a direct DLL copy in the root Plugins folder as well (some InstaMAT setups load DLLs from here).
    # NOTE: This might fail to load if dependencies are not found, but we copied the full folder above.
    try {
        if (-not (Test-Path $UserPluginsRoot)) {
            New-Item -ItemType Directory -Path $UserPluginsRoot -Force | Out-Null
        }
        $RootDllPath = Join-Path $UserPluginsRoot $PluginDllName
        Copy-Item $SourceDll -Destination $RootDllPath -Force
        Write-Host "SUCCESS: Updated root plugin DLL: $RootDllPath"

        # Also copy assets next to the root plugin folder (for tiling mesh default).
        if (Test-Path $TilingMeshSrc) {
            $dst = Join-Path $UserPluginsRoot "assets\\meshes"
            New-Item -ItemType Directory -Path $dst -Force | Out-Null
            Copy-Item $TilingMeshSrc -Destination (Join-Path $dst "plane_tiling.usd") -Force
        }
    } catch {
        Write-Warning "FAILED to update root plugin DLL in $UserPluginsRoot."
    }

    foreach ($DestPath in $InstallPaths) {
        Write-Host "Attempting to install to: $DestPath"
        try {
            if (-not (Test-Path $DestPath)) {
                New-Item -ItemType Directory -Path $DestPath -Force | Out-Null
            }
            
            # Install with the *Plugin.dll name (InstaMAT Studio tends to scan for this pattern).
            Copy-Item $SourceDll -Destination (Join-Path $DestPath $PluginDllName) -Force
            
             # Copy dependencies
            $TexconvSrc = Join-Path $RepoRoot "texconv.exe"
            if (Test-Path $TexconvSrc) {
                Copy-Item $TexconvSrc -Destination $DestPath -Force
            }

            # Copy assets (tiling mesh) next to the plugin DLL.
            if (Test-Path $TilingMeshSrc) {
                $dst = Join-Path $DestPath "assets\\meshes"
                New-Item -ItemType Directory -Path $dst -Force | Out-Null
                Copy-Item $TilingMeshSrc -Destination (Join-Path $dst "plane_tiling.usd") -Force
            }
            
            Write-Host "SUCCESS: Installed to $DestPath"
        } catch {
            Write-Warning "FAILED to install to $DestPath. Access denied or path invalid."
        }
    }

    # 6.2 Install the startup script (adds the RTX Remix menu in InstaMAT Studio)
    try {
        $StartupScriptSrc = Join-Path $RepoRoot "rtx_remix_connector.py"
        $StartupDirs = @(
            (Join-Path $env:APPDATA "InstaMAT Studio\\scripts\\startup"),
            (Join-Path $env:APPDATA "InstaMAT Studio\\Scripts\\Startup"),
            # Some InstaMAT Studio builds use this folder name for startup execution.
            (Join-Path $env:APPDATA "InstaMAT Studio\\Startup")
        )

        if (-not (Test-Path $StartupScriptSrc)) {
            Write-Warning "Startup script not found in repo: $StartupScriptSrc"
        } else {
            foreach ($StartupScriptsDir in $StartupDirs) {
                $StartupScriptDst = Join-Path $StartupScriptsDir "rtx_remix_connector.py"

                if (-not (Test-Path $StartupScriptsDir)) {
                    New-Item -ItemType Directory -Path $StartupScriptsDir -Force | Out-Null
                }

                Copy-Item $StartupScriptSrc -Destination $StartupScriptDst -Force
                Write-Host "SUCCESS: Installed startup script: $StartupScriptDst"
            }
        }
    } catch {
        Write-Warning "FAILED to install startup script. Error: $($_.Exception.Message)"
    }

    # 6.3 Build + install a proper .IMP (MAT container) package so InstaMAT Studio registers the plugin.
    # InstaMAT Studio registers C++ plugins from .IMP packages (see startup logs: "registered plugin='X.dll' from package='Y.IMP'").
    # A valid .IMP begins with "MAT\x01" and stores resources (DLLs, .meta) in a simple container.
    try {
        if (-not (Test-Path $UserLibraryRoot)) {
            New-Item -ItemType Directory -Path $UserLibraryRoot -Force | Out-Null
        }

        $SampleImp = Join-Path $UserLibraryRoot "sample.IMP"
        if (-not (Test-Path $SampleImp)) {
            Write-Warning "sample.IMP not found at $SampleImp. Skipping .IMP packaging step (plugin may not auto-register)."
        } else {
            $PackageDst = Join-Path $UserLibraryRoot "$ProjectName.IMP"
            $MetaSrc = Join-Path $RepoRoot "$ProjectName.IMP.meta"

            if (-not (Test-Path $MetaSrc)) {
                Write-Warning "Package meta not found in repo: $MetaSrc. Skipping .IMP packaging step."
            } else {
                Add-Type -AssemblyName System.IO.Compression.FileSystem | Out-Null

                # Read template header (first entry begins at offset 0x8C in the shipped sample.IMP)
                $sampleBytes = [IO.File]::ReadAllBytes($SampleImp)
                $headerSize = 0x8C
                if ($sampleBytes.Length -lt $headerSize) {
                    throw "sample.IMP too small to contain expected header."
                }
                # IMPORTANT: PowerShell slicing returns object[]; ensure we keep this as a true byte[].
                $headerBytes = New-Object byte[] $headerSize
                [Array]::Copy($sampleBytes, 0, $headerBytes, 0, $headerSize)

                # Resource 1: plugin DLL (embed it so packaging is self-contained)
                $dllName = $PluginDllName
                $dllNameBytes = [Text.Encoding]::ASCII.GetBytes($dllName)
                $dllBytes = [IO.File]::ReadAllBytes($SourceDll)

                # Resource 2: .meta JSON (embedded file name is always '.meta')
                $metaName = ".meta"
                $metaNameBytes = [Text.Encoding]::ASCII.GetBytes($metaName)
                $metaText = Get-Content -LiteralPath $MetaSrc -Raw -Encoding UTF8
                $metaBytes = [Text.Encoding]::UTF8.GetBytes($metaText)

                # Resource 3 (optional): default tiling mesh (small .usda plane)
                $tilingName = "plane_tiling.usd"
                $tilingNameBytes = [Text.Encoding]::ASCII.GetBytes($tilingName)
                $tilingBytes = $null
                if (Test-Path $TilingMeshSrc) {
                    $tilingBytes = [IO.File]::ReadAllBytes($TilingMeshSrc)
                }

                $ms = New-Object System.IO.MemoryStream
                $bw = New-Object System.IO.BinaryWriter($ms)

                # Header
                $bw.Write($headerBytes)

                # Entry: DLL
                $bw.Write([UInt32]$dllNameBytes.Length)
                $bw.Write([UInt32]0)
                $bw.Write($dllNameBytes)
                $bw.Write([UInt64]$dllBytes.Length)
                $bw.Write($dllBytes)

                # Entry: .meta
                $bw.Write([UInt32]$metaNameBytes.Length)
                $bw.Write([UInt32]0)
                $bw.Write($metaNameBytes)
                $bw.Write([UInt64]$metaBytes.Length)
                $bw.Write($metaBytes)

                # Entry: default tiling mesh (optional)
                if ($tilingBytes -and ($tilingBytes.Length -gt 0)) {
                    $bw.Write([UInt32]$tilingNameBytes.Length)
                    $bw.Write([UInt32]0)
                    $bw.Write($tilingNameBytes)
                    $bw.Write([UInt64]$tilingBytes.Length)
                    $bw.Write($tilingBytes)
                }

                $bw.Flush()
                [IO.File]::WriteAllBytes($PackageDst, $ms.ToArray())

                $bw.Dispose()
                $ms.Dispose()

                Write-Host "SUCCESS: Installed package: $PackageDst"
            }
        }
    } catch {
        Write-Warning "FAILED to build/install .IMP package. Error: $($_.Exception.Message)"
    }

    Write-Host "`n=== BUILD COMPLETE ==="
} else {
    Write-Error "Build artifact not found."
}
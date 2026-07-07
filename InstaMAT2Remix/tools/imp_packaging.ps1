# imp_packaging.ps1 — shared helpers for building InstaMAT .IMP (MAT container)
# packages. Dot-source this file:  . "$PSScriptRoot\tools\imp_packaging.ps1"
#
# InstaMAT Studio registers C++ plugins from .IMP packages (startup log:
# "registered plugin='X.dll' from package='Y.IMP'"). The container format,
# verified against a Studio-shipped package:
#   - 0x8C-byte header: ASCII "MAT", one 0x01 version byte, zero padding.
#   - Followed by length-prefixed resource entries:
#       [UInt32 nameLen][UInt32 0][ASCII name][UInt64 dataLen][data]

function New-ImpHeader {
    $header = New-Object byte[] 0x8C
    [Text.Encoding]::ASCII.GetBytes("MAT").CopyTo($header, 0)
    $header[3] = 1
    return $header
}

function New-ImpPackage {
    <#
    .SYNOPSIS
        Writes a .IMP package containing the plugin DLL, its .meta JSON, and
        optional extra resources.
    .PARAMETER OutPath
        Destination .IMP file path (overwritten).
    .PARAMETER DllPath
        Path to the plugin DLL. Embedded under its own file name.
    .PARAMETER MetaPath
        Path to the package .meta JSON. Embedded as ".meta".
    .PARAMETER ExtraResources
        Optional hashtable of embeddedName -> sourcePath for additional
        resources (e.g. @{ "plane_tiling.usd" = "assets\meshes\plane_tiling.usd" }).
    #>
    param(
        [Parameter(Mandatory = $true)][string]$OutPath,
        [Parameter(Mandatory = $true)][string]$DllPath,
        [Parameter(Mandatory = $true)][string]$MetaPath,
        [hashtable]$ExtraResources = @{}
    )

    if (-not (Test-Path $DllPath)) { throw "New-ImpPackage: DLL not found: $DllPath" }
    if (-not (Test-Path $MetaPath)) { throw "New-ImpPackage: .meta not found: $MetaPath" }

    $ms = New-Object System.IO.MemoryStream
    $bw = New-Object System.IO.BinaryWriter($ms)
    try {
        # IMPORTANT: keep the header a true byte[] — passing a PowerShell
        # object[] here silently binds BinaryWriter.Write(Boolean) and writes
        # a single 0x01 byte instead of the 140-byte header.
        [byte[]]$headerBytes = New-ImpHeader
        $bw.Write($headerBytes)

        $writeEntry = {
            param([string]$name, [byte[]]$data)
            [byte[]]$nameBytes = [Text.Encoding]::ASCII.GetBytes($name)
            $bw.Write([UInt32]$nameBytes.Length)
            $bw.Write([UInt32]0)
            $bw.Write($nameBytes)
            $bw.Write([UInt64]$data.Length)
            $bw.Write($data)
        }

        & $writeEntry ([IO.Path]::GetFileName($DllPath)) ([IO.File]::ReadAllBytes($DllPath))

        $metaText = Get-Content -LiteralPath $MetaPath -Raw -Encoding UTF8
        & $writeEntry ".meta" ([Text.Encoding]::UTF8.GetBytes($metaText))

        foreach ($name in $ExtraResources.Keys) {
            $src = $ExtraResources[$name]
            if (Test-Path $src) {
                & $writeEntry $name ([IO.File]::ReadAllBytes($src))
            } else {
                Write-Warning "New-ImpPackage: extra resource missing, skipped: $src"
            }
        }

        $bw.Flush()
        $parent = Split-Path $OutPath -Parent
        if ($parent -and -not (Test-Path $parent)) {
            New-Item -ItemType Directory -Path $parent -Force | Out-Null
        }
        [IO.File]::WriteAllBytes($OutPath, $ms.ToArray())
    } finally {
        $bw.Dispose()
        $ms.Dispose()
    }
}

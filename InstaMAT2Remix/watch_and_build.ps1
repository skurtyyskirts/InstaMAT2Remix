$ErrorActionPreference = "Continue"

$SourceDir = $PSScriptRoot
$Extensions = @("*.cpp", "*.h", "*.txt")  # .txt for CMakeLists.txt
$BuildScript = Join-Path $SourceDir "build_plugin.ps1"
$Debounce = 3  # seconds to wait before rebuilding (coalesces rapid saves)

Write-Host "=== InstaMAT2Remix Auto-Builder ===" -ForegroundColor Cyan
Write-Host "Watching: $SourceDir"
Write-Host "Extensions: $($Extensions -join ', ')"
Write-Host "Debounce: ${Debounce}s"
Write-Host "Press Ctrl+C to stop."
Write-Host ""

# Track the last build time to debounce rapid changes
$script:lastBuildTime = [DateTime]::MinValue
$script:pendingBuild = $false
$script:buildTimer = $null

function Start-Build {
    $now = Get-Date
    $elapsed = ($now - $script:lastBuildTime).TotalSeconds
    if ($elapsed -lt $Debounce) {
        # Too soon since last build, mark pending
        $script:pendingBuild = $true
        return
    }

    $script:lastBuildTime = $now
    $script:pendingBuild = $false

    Write-Host ""
    Write-Host "[$($now.ToString('HH:mm:ss'))] Change detected - rebuilding..." -ForegroundColor Yellow
    Write-Host ("=" * 60)

    try {
        & powershell -ExecutionPolicy Bypass -File $BuildScript 2>&1 | ForEach-Object {
            if ($_ -match "ERROR|FAILED|error ") {
                Write-Host $_ -ForegroundColor Red
            } elseif ($_ -match "SUCCESS|COMPLETE") {
                Write-Host $_ -ForegroundColor Green
            } else {
                Write-Host $_
            }
        }
        Write-Host ""
        Write-Host "[$((Get-Date).ToString('HH:mm:ss'))] Build finished." -ForegroundColor Green
    } catch {
        Write-Host "[$((Get-Date).ToString('HH:mm:ss'))] Build failed: $($_.Exception.Message)" -ForegroundColor Red
    }

    Write-Host ("=" * 60)
    Write-Host "Watching for changes..." -ForegroundColor Cyan
}

# Create FileSystemWatcher for each extension pattern
$watchers = @()
foreach ($ext in $Extensions) {
    $watcher = New-Object System.IO.FileSystemWatcher
    $watcher.Path = $SourceDir
    $watcher.Filter = $ext
    $watcher.IncludeSubdirectories = $false
    $watcher.NotifyFilter = [IO.NotifyFilters]::LastWrite -bor [IO.NotifyFilters]::FileName
    $watcher.EnableRaisingEvents = $true
    $watchers += $watcher

    # Register for Changed and Created events
    Register-ObjectEvent $watcher "Changed" -Action {
        $script:pendingBuild = $true
    } | Out-Null
    Register-ObjectEvent $watcher "Created" -Action {
        $script:pendingBuild = $true
    } | Out-Null
}

Write-Host "Watching for changes..." -ForegroundColor Cyan

# Initial build
Write-Host "Running initial build..." -ForegroundColor Yellow
Start-Build

# Poll loop (FileSystemWatcher events set $pendingBuild, we debounce here)
try {
    while ($true) {
        Start-Sleep -Seconds 1
        if ($script:pendingBuild) {
            $script:pendingBuild = $false
            Start-Sleep -Seconds $Debounce  # Debounce: wait for rapid saves to finish
            Start-Build
        }
    }
} finally {
    Write-Host "`nStopping watchers..." -ForegroundColor Yellow
    foreach ($w in $watchers) {
        $w.EnableRaisingEvents = $false
        $w.Dispose()
    }
    Get-EventSubscriber | Unregister-Event -ErrorAction SilentlyContinue
    Write-Host "Done." -ForegroundColor Green
}

<#
.SYNOPSIS
    Collects everything needed to diagnose a multiplayer problem into one zip.

.DESCRIPTION
    Run this after a session where something went wrong, then send the resulting
    zip. It gathers:

      - the mod log and configuration (SporeMultiplayer.log / .ini)
      - the dedicated server log (if this machine hosted)
      - the launcher log and configuration
      - Spore's own crash reports (if the game crashed)
      - system information (OS, CPU, memory, network adapters)
      - the versions of every Spore Multiplayer binary found

    No personal files are collected: only logs, ini files and crash dumps that
    Spore itself produced.

.PARAMETER Output
    Path of the zip file. Defaults to the Desktop.

.PARAMETER IncludeDumps
    Also include Spore's minidump files. They can be large, so they are optional.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File collect-logs.ps1

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File collect-logs.ps1 -IncludeDumps
#>

param(
    [string]$Output = '',
    [switch]$IncludeDumps
)

$ErrorActionPreference = 'Continue'

# --- where things live ------------------------------------------------------

function Find-LauncherKit {
    $pathInfo = Join-Path $env:APPDATA 'Spore ModAPI Launcher\path.info'
    if (Test-Path $pathInfo) {
        $path = (Get-Content $pathInfo -Raw).Trim()
        if ($path -and (Test-Path $path)) { return $path }
    }

    $fallback = 'C:\ProgramData\SPORE ModAPI Launcher Kit'
    if (Test-Path $fallback) { return $fallback }

    return ''
}

function Find-SporeBin {
    $candidates = @(
        'F:\SteamLibrary\steamapps\common\spore\SporebinEP1',
        'C:\Program Files (x86)\Steam\steamapps\common\Spore\SporebinEP1',
        'C:\Program Files (x86)\Origin Games\SPORE\SporebinEP1',
        'C:\Program Files\EA Games\SPORE\SporebinEP1'
    )

    foreach ($candidate in $candidates) {
        if (Test-Path $candidate) { return $candidate }
    }

    return ''
}

$kit = Find-LauncherKit
$sporeBin = Find-SporeBin

$stamp = Get-Date -Format 'yyyy-MM-dd_HH-mm-ss'
$workDir = Join-Path $env:TEMP "spore-mp-logs-$stamp"
New-Item -ItemType Directory -Force -Path $workDir | Out-Null

if (-not $Output) {
    $desktop = [Environment]::GetFolderPath('Desktop')
    $Output = Join-Path $desktop "Spore-Multiplayer-logs-$stamp.zip"
}

Write-Host "Collecting logs into $workDir"
Write-Host ''

$collected = 0

function Add-File {
    param([string]$Path, [string]$Name)

    if (-not (Test-Path $Path)) {
        Write-Host "  [skip] $Name (not found)"
        return
    }

    Copy-Item $Path (Join-Path $workDir $Name) -Force -ErrorAction SilentlyContinue
    $size = (Get-Item $Path).Length
    Write-Host "  [ok]   $Name ($size bytes)"
    $script:collected++
}

function Add-Text {
    param([string]$Name, [string]$Text)

    Set-Content -Path (Join-Path $workDir $Name) -Value $Text -Encoding UTF8
    Write-Host "  [ok]   $Name"
    $script:collected++
}

# --- client (the mod) -------------------------------------------------------

Write-Host 'Client mod:'

if ($kit) {
    Add-File (Join-Path $kit 'mLibs\SporeMultiplayer.log') 'client-SporeMultiplayer.log'
    Add-File (Join-Path $kit 'mLibs\SporeMultiplayer.ini') 'client-SporeMultiplayer.ini'
    Add-File (Join-Path $kit 'mLibs\Spore-Multiplayer-Client.dll') 'client-Spore-Multiplayer-Client.dll'

    # The mod falls back to %APPDATA% when the DLL folder is not writable.
    $appDataDir = Join-Path $env:APPDATA 'SporeMultiplayer'
    if (Test-Path $appDataDir) {
        Add-File (Join-Path $appDataDir 'SporeMultiplayer.log') 'client-appdata-SporeMultiplayer.log'
        Add-File (Join-Path $appDataDir 'SporeMultiplayer.ini') 'client-appdata-SporeMultiplayer.ini'
    }
}

# --- dedicated server / master / relay / launcher ---------------------------

Write-Host ''
Write-Host 'Servers and launcher:'

$searchRoots = @()

if ($kit) { $searchRoots += $kit }
$searchRoots += (Join-Path (Get-Location) 'bin\Release')
$searchRoots += (Join-Path (Get-Location) 'Server\Spore-Multiplayer-Server')
$searchRoots += (Join-Path (Get-Location) 'Server\Spore-Multiplayer-Server\bin\Release')
$searchRoots += (Join-Path (Get-Location) 'Server\Spore-Multiplayer-Master')
$searchRoots += (Join-Path (Get-Location) 'Server\Spore-Multiplayer-Master\bin\Release')
$searchRoots += (Join-Path (Get-Location) 'Server\Spore-Multiplayer-Relay')
$searchRoots += (Join-Path (Get-Location) 'Server\Spore-Multiplayer-Relay\bin\Release')
$searchRoots += (Join-Path (Get-Location) 'Server\Spore-Multiplayer-Launcher')
$searchRoots += (Join-Path (Get-Location) 'Server\Spore-Multiplayer-Launcher\bin\Release')

foreach ($root in $searchRoots) {
    if (-not (Test-Path $root)) { continue }

    foreach ($name in @('SporeMultiplayer-Server.log', 'server.ini', 'master.ini', 'relay.ini', 'launcher.ini')) {
        $file = Join-Path $root $name
        if (Test-Path $file) {
            Add-File $file ("server-" + $name)
        }
    }
}

# --- Spore's own crash reports ---------------------------------------------

Write-Host ''
Write-Host 'Spore crash reports:'

if ($sporeBin) {
    $reports = Get-ChildItem $sporeBin -Filter 'Exception Report*exception.txt' -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime -Descending |
        Select-Object -First 3

    foreach ($report in $reports) {
        Add-File $report.FullName ("spore-crash-" + $report.Name)
    }

    if ($IncludeDumps) {
        $dumps = Get-ChildItem $sporeBin -Filter 'Exception Report*minidump.mdmp' -ErrorAction SilentlyContinue |
            Sort-Object LastWriteTime -Descending |
            Select-Object -First 2

        foreach ($dump in $dumps) {
            Add-File $dump.FullName ("spore-dump-" + $dump.Name)
        }
    }

    # Only the version of the game is needed, not the 25 MB executable.
    $sporeExe = Join-Path $sporeBin 'SporeApp.exe'
    if (Test-Path $sporeExe) {
        $info = (Get-Item $sporeExe).VersionInfo
        Add-Text 'spore-version.txt' "SporeApp.exe file=$($info.FileVersion) product=$($info.ProductVersion)"
    }
}

# --- system information -----------------------------------------------------

Write-Host ''
Write-Host 'System information:'

$systemInfo = @()
$systemInfo += "Collected: $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')"
$systemInfo += "Computer: $env:COMPUTERNAME"
$systemInfo += "User: $env:USERNAME"
$systemInfo += "OS: $([System.Environment]::OSVersion.VersionString)"
$systemInfo += "64-bit OS: $([Environment]::Is64BitOperatingSystem)"
$systemInfo += "PowerShell: $($PSVersionTable.PSVersion)"
$systemInfo += ''

try {
    $cpu = Get-CimInstance Win32_Processor -ErrorAction Stop | Select-Object -First 1
    $systemInfo += "CPU: $($cpu.Name) ($($cpu.NumberOfCores) cores)"
}
catch { $systemInfo += 'CPU: (unavailable)' }

try {
    $memory = Get-CimInstance Win32_ComputerSystem -ErrorAction Stop
    $systemInfo += "RAM: $([math]::Round($memory.TotalPhysicalMemory / 1GB, 1)) GB"
}
catch { $systemInfo += 'RAM: (unavailable)' }

$systemInfo += ''
$systemInfo += 'Network adapters:'
try {
    Get-NetIPAddress -AddressFamily IPv4 -ErrorAction Stop |
        Where-Object { $_.IPAddress -ne '127.0.0.1' } |
        ForEach-Object { $systemInfo += "  $($_.InterfaceAlias): $($_.IPAddress)" }
}
catch { $systemInfo += '  (unavailable)' }

$systemInfo += ''
$systemInfo += 'Listening ports (Spore Multiplayer):'
try {
    Get-NetTCPConnection -State Listen -ErrorAction Stop |
        Where-Object { $_.LocalPort -in 7000, 7100, 7200, 7300, 7400, 7500, 7600, 17401, 17402 } |
        ForEach-Object { $systemInfo += "  TCP $($_.LocalAddress):$($_.LocalPort) pid $($_.OwningProcess)" }
}
catch { $systemInfo += '  (unavailable)' }

Add-Text 'system-info.txt' ($systemInfo -join "`r`n")

# --- versions of every binary ----------------------------------------------

$versions = @()

$binaries = @()
if ($kit) {
    $binaries += Get-ChildItem (Join-Path $kit 'mLibs') -Filter 'Spore-Multiplayer-*.dll' -ErrorAction SilentlyContinue
}
foreach ($root in $searchRoots) {
    if (Test-Path $root) {
        $binaries += Get-ChildItem $root -Filter 'Spore-Multiplayer-*.exe' -ErrorAction SilentlyContinue
    }
}

foreach ($binary in $binaries) {
    $info = $binary.VersionInfo
    $versions += "$($binary.Name)  file=$($info.FileVersion)  product=$($info.ProductVersion)  size=$($binary.Length)"
}

if ($versions.Count -eq 0) { $versions = @('No Spore Multiplayer binaries found') }

Add-Text 'versions.txt' ($versions -join "`r`n")

# --- pack -------------------------------------------------------------------

Write-Host ''
Write-Host "Creating $Output"

if (Test-Path $Output) { Remove-Item $Output -Force }

Compress-Archive -Path (Join-Path $workDir '*') -DestinationPath $Output -Force
Remove-Item $workDir -Recurse -Force -ErrorAction SilentlyContinue

$size = (Get-Item $Output).Length
Write-Host ''
Write-Host "Done: $Output ($([math]::Round($size / 1KB, 1)) KB, $collected items)" -ForegroundColor Green
Write-Host 'Send this zip to the developer.' -ForegroundColor Green

Write-Host ''
Write-Host 'Tip: set log_level=debug in SporeMultiplayer.ini before reproducing the'
Write-Host 'problem, so the log contains the detailed steps.'

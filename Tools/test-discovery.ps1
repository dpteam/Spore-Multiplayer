<#
.SYNOPSIS
    Integration test for the LAN session discovery.

.DESCRIPTION
    Starts the dedicated server on a free port, listens for the UDP beacons and
    checks their contents. This validates that a client can find a session
    without knowing any IP address.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File Tools\test-discovery.ps1
#>

param(
    [int]$Port = 0,
    [int]$Seconds = 8
)

$ErrorActionPreference = 'Stop'

function Find-ServerExe {
    $candidates = @(
        (Join-Path $PSScriptRoot '..\bin\Release\Spore-Multiplayer-Server.exe'),
        (Join-Path $PSScriptRoot '..\bin\Debug\Spore-Multiplayer-Server.exe'),
        (Join-Path $PSScriptRoot '..\Server\Spore-Multiplayer-Server\bin\Release\Spore-Multiplayer-Server.exe'),
        (Join-Path $PSScriptRoot '..\Server\Spore-Multiplayer-Server\bin\Debug\Spore-Multiplayer-Server.exe')
    )

    foreach ($candidate in $candidates) {
        if (Test-Path $candidate) { return (Resolve-Path $candidate).Path }
    }

    throw 'Spore-Multiplayer-Server.exe not found. Build the Server project first.'
}

if ($Port -eq 0) { $Port = 7100 + (Get-Random -Minimum 100 -Maximum 800) }

$exe = Find-ServerExe
$logPath = Join-Path ([System.IO.Path]::GetTempPath()) 'spore-multiplayer-discovery-test.log'
if (Test-Path $logPath) { Remove-Item $logPath -Force }

Write-Host "Server: $exe"
Write-Host "Port:   $Port"

$server = Start-Process -FilePath $exe `
    -ArgumentList @('--port', $Port, '--log-level', 'debug') `
    -RedirectStandardOutput $logPath `
    -RedirectStandardError ($logPath + '.err') `
    -WindowStyle Hidden `
    -PassThru

Start-Sleep -Milliseconds 800

if ($server.HasExited) {
    $log = if (Test-Path $logPath) { Get-Content $logPath -Raw } else { '' }
    throw "The server exited immediately: $log"
}

$failures = 0

function Check {
    param([bool]$Condition, [string]$Message)

    if ($Condition) {
        Write-Host "  [ok]   $Message" -ForegroundColor Green
    }
    else {
        Write-Host "  [FAIL] $Message" -ForegroundColor Red
        $script:failures++
    }
}

$udp = $null

try {
    Write-Host ''
    Write-Host "Listening for beacons on UDP 17400 for $Seconds s..."

    $udp = New-Object System.Net.Sockets.UdpClient
    $udp.Client.SetSocketOption([System.Net.Sockets.SocketOptionLevel]::Socket,
        [System.Net.Sockets.SocketOptionName]::ReuseAddress, $true)
    # The beacons go to the fixed discovery port, not to the game port.
    $udp.Client.Bind((New-Object System.Net.IPEndPoint([System.Net.IPAddress]::Any, 17400)))
    $udp.Client.ReceiveTimeout = 2500

    $deadline = (Get-Date).AddSeconds($Seconds)
    $beacons = @()

    while ((Get-Date) -lt $deadline) {
        try {
            $remote = New-Object System.Net.IPEndPoint([System.Net.IPAddress]::Any, 0)
            $data = $udp.Receive([ref]$remote)
        }
        catch {
            continue
        }

        if ($data.Length -lt 44) { continue }

        $magic = [System.BitConverter]::ToUInt16($data, 0)
        if ($magic -ne 0x504D) { continue }

        $beacons += [pscustomobject]@{
            Address  = $remote.Address.ToString()
            Protocol = [System.BitConverter]::ToUInt16($data, 2)
            Port     = [System.BitConverter]::ToUInt16($data, 4)
            Players  = [System.BitConverter]::ToUInt16($data, 6)
            Max      = [System.BitConverter]::ToUInt16($data, 8)
            Name     = [System.Text.Encoding]::ASCII.GetString($data, 12, 32).TrimEnd([char]0)
        }
    }

    Write-Host ''
    Write-Host 'Test: discovery beacons'

    Check ($beacons.Count -gt 0) "At least one beacon was received ($($beacons.Count))"

    if ($beacons.Count -gt 0) {
        $beacon = $beacons[0]
        Check ($beacon.Protocol -eq 1) "The protocol version is reported ($($beacon.Protocol))"
        Check ($beacon.Port -eq $Port) "The TCP port is reported ($($beacon.Port))"
        Check ($beacon.Max -eq 8) "The maximum number of players is reported ($($beacon.Max))"
        Check ($beacon.Name -eq 'Spore Co-op') "The session name is reported ('$($beacon.Name)')"
        Check ($beacon.Address -ne '0.0.0.0') "The beacon comes from a real address ($($beacon.Address))"
    }
}
finally {
    if ($udp -ne $null) { $udp.Close() }

    try {
        if (-not $server.HasExited) {
            Stop-Process -Id $server.Id -Force
            $server.WaitForExit(3000) | Out-Null
        }
    }
    catch { }
}

Write-Host ''
if ($failures -eq 0) {
    Write-Host 'All checks passed.' -ForegroundColor Green
    exit 0
}

Write-Host "$failures check(s) failed. Server log: $logPath" -ForegroundColor Red
exit 1

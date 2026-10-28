<#
.SYNOPSIS
    Integration test for the relay server.

.DESCRIPTION
    Starts the relay, opens a "host" and a "client" connection with a shared
    ticket and checks that arbitrary bytes are forwarded in both directions.
    It also checks that a wrong ticket is refused and that a room is closed when
    one side leaves.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File Tools\test-relay.ps1
#>

param(
    [int]$Port = 0
)

$ErrorActionPreference = 'Stop'

$script:failures = 0

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

function Find-RelayExe {
    $candidates = @(
        (Join-Path $PSScriptRoot '..\bin\Release\Spore-Multiplayer-Relay.exe'),
        (Join-Path $PSScriptRoot '..\bin\Debug\Spore-Multiplayer-Relay.exe'),
        (Join-Path $PSScriptRoot '..\Server\Spore-Multiplayer-Relay\bin\Release\Spore-Multiplayer-Relay.exe'),
        (Join-Path $PSScriptRoot '..\Server\Spore-Multiplayer-Relay\bin\Debug\Spore-Multiplayer-Relay.exe')
    )

    foreach ($candidate in $candidates) {
        if (Test-Path $candidate) { return (Resolve-Path $candidate).Path }
    }

    throw 'Spore-Multiplayer-Relay.exe not found. Build the Relay project first.'
}

function New-Handshake {
    param([byte]$Role, [string]$Ticket)

    $buffer = New-Object 'System.Collections.Generic.List[byte]'
    $buffer.AddRange([System.BitConverter]::GetBytes([uint16]0x4C52))  # "RL"
    $buffer.Add($Role)
    $buffer.Add(0)
    $buffer.AddRange([System.Text.Encoding]::ASCII.GetBytes($Ticket))
    return , $buffer.ToArray()
}

function Read-Exact {
    param([System.IO.Stream]$Stream, [int]$Count)

    $buffer = New-Object byte[] $Count
    $offset = 0
    while ($offset -lt $Count) {
        $read = $Stream.Read($buffer, $offset, $Count - $offset)
        if ($read -le 0) { throw 'The connection was closed' }
        $offset += $read
    }
    return , $buffer
}

if ($Port -eq 0) { $Port = 17500 + (Get-Random -Minimum 10 -Maximum 80) }

$exe = Find-RelayExe
$logPath = Join-Path ([System.IO.Path]::GetTempPath()) 'spore-multiplayer-relay-test.log'
if (Test-Path $logPath) { Remove-Item $logPath -Force }

Write-Host "Relay: $exe"
Write-Host "Port:  $Port"
Write-Host ''

$relay = Start-Process -FilePath $exe `
    -ArgumentList @('--port', $Port, '--log-level', 'debug') `
    -RedirectStandardOutput $logPath `
    -RedirectStandardError ($logPath + '.err') `
    -WindowStyle Hidden `
    -PassThru

Start-Sleep -Milliseconds 900

if ($relay.HasExited) {
    $log = if (Test-Path $logPath) { Get-Content $logPath -Raw } else { '' }
    throw "The relay exited immediately: $log"
}

$hostClient = $null
$peerClient = $null

try {
    Write-Host 'Test 1: wrong ticket is refused'

    $wrong = New-Object System.Net.Sockets.TcpClient
    $wrong.NoDelay = $true
    $wrong.Connect('127.0.0.1', $Port)
    $wrongStream = $wrong.GetStream()
    $wrongStream.Write((New-Handshake -Role 2 -Ticket 'ffffffffffffffff'), 0, 20)
    $wrongStream.Flush()

    $result = Read-Exact $wrongStream 1
    Check ($result[0] -eq 2) "The client is refused with NoRoom ($($result[0]))"
    $wrong.Close()

    Write-Host ''
    Write-Host 'Test 2: host creates a room'

    $ticket = 'a1b2c3d4e5f60718'

    $hostClient = New-Object System.Net.Sockets.TcpClient
    $hostClient.NoDelay = $true
    $hostClient.Connect('127.0.0.1', $Port)
    $hostStream = $hostClient.GetStream()

    $handshake = New-Handshake -Role 1 -Ticket $ticket
    $hostStream.Write($handshake, 0, $handshake.Length)
    $hostStream.Flush()

    $result = Read-Exact $hostStream 1
    Check ($result[0] -eq 0) "The host is accepted ($($result[0]))"

    Write-Host ''
    Write-Host 'Test 3: client joins the room'

    $peerClient = New-Object System.Net.Sockets.TcpClient
    $peerClient.NoDelay = $true
    $peerClient.Connect('127.0.0.1', $Port)
    $peerStream = $peerClient.GetStream()

    $handshake = New-Handshake -Role 2 -Ticket $ticket
    $peerStream.Write($handshake, 0, $handshake.Length)
    $peerStream.Flush()

    $result = Read-Exact $peerStream 1
    Check ($result[0] -eq 0) "The client is accepted ($($result[0]))"

    Write-Host ''
    Write-Host 'Test 4: bytes are forwarded in both directions'

    $payloadA = [System.Text.Encoding]::ASCII.GetBytes('hello from the client')
    $peerStream.Write($payloadA, 0, $payloadA.Length)
    $peerStream.Flush()

    $receivedA = Read-Exact $hostStream $payloadA.Length
    $textA = [System.Text.Encoding]::ASCII.GetString($receivedA)
    Check ($textA -eq 'hello from the client') "Client -> host ('$textA')"

    $payloadB = [System.Text.Encoding]::ASCII.GetBytes('and hello back from the host')
    $hostStream.Write($payloadB, 0, $payloadB.Length)
    $hostStream.Flush()

    $receivedB = Read-Exact $peerStream $payloadB.Length
    $textB = [System.Text.Encoding]::ASCII.GetString($receivedB)
    Check ($textB -eq 'and hello back from the host') "Host -> client ('$textB')"

    Write-Host ''
    Write-Host 'Test 5: binary data survives unchanged'

    $binary = New-Object byte[] 4096
    $random = New-Object System.Random 12345
    $random.NextBytes($binary)

    $peerStream.Write($binary, 0, $binary.Length)
    $peerStream.Flush()

    $received = Read-Exact $hostStream $binary.Length

    $identical = $true
    for ($i = 0; $i -lt $binary.Length; $i++) {
        if ($received[$i] -ne $binary[$i]) { $identical = $false; break }
    }
    Check $identical 'Binary payload is forwarded byte for byte'

    Write-Host ''
    Write-Host 'Test 6: leaving closes the room'

    $peerClient.Close()
    Start-Sleep -Milliseconds 700

    # The host side must be closed by the relay.
    $hostStream.ReadTimeout = 3000
    $closed = $false
    try {
        $buffer = New-Object byte[] 16
        $read = $hostStream.Read($buffer, 0, 16)
        $closed = ($read -le 0)
    }
    catch {
        $closed = $true
    }
    Check $closed 'The host connection is closed when the client leaves'
}
finally {
    foreach ($connection in @($hostClient, $peerClient)) {
        if ($connection -ne $null) {
            try { $connection.Close() } catch { }
        }
    }

    try {
        if (-not $relay.HasExited) {
            Stop-Process -Id $relay.Id -Force
            $relay.WaitForExit(3000) | Out-Null
        }
    }
    catch { }
}

Write-Host ''
if ($script:failures -eq 0) {
    Write-Host 'All checks passed.' -ForegroundColor Green
    exit 0
}

Write-Host "$($script:failures) check(s) failed. Relay log: $logPath" -ForegroundColor Red
exit 1

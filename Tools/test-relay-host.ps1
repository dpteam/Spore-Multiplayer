<#
.SYNOPSIS
    End-to-end test of the relay hosting path used in production.

.DESCRIPTION
    Starts the relay and the dedicated server with `--relay`, so the server
    itself bridges the relay room to its local port. A fake game client then
    joins through the relay and the full handshake, state and chat are checked.

    This is the exact code path used by `mp_host` when no port forwarding is
    available.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File Tools\test-relay-host.ps1
#>

param(
    [int]$RelayPort = 0,
    [int]$GamePort = 0
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

function Find-Exe {
    param([string]$Name)

    $candidates = @(
        (Join-Path $PSScriptRoot "..\bin\Release\$Name"),
        (Join-Path $PSScriptRoot "..\bin\Debug\$Name"),
        (Join-Path $PSScriptRoot "..\Server\Spore-Multiplayer-Server\bin\Release\$Name"),
        (Join-Path $PSScriptRoot "..\Server\Spore-Multiplayer-Server\bin\Debug\$Name"),
        (Join-Path $PSScriptRoot "..\Server\Spore-Multiplayer-Relay\bin\Release\$Name"),
        (Join-Path $PSScriptRoot "..\Server\Spore-Multiplayer-Relay\bin\Debug\$Name")
    )

    foreach ($candidate in $candidates) {
        if (Test-Path $candidate) { return (Resolve-Path $candidate).Path }
    }

    throw "$Name not found. Build the projects first."
}

function Add-UInt16 {
    param([System.Collections.Generic.List[byte]]$Buffer, [uint16]$Value)
    $Buffer.AddRange([System.BitConverter]::GetBytes($Value))
}

function Add-UInt32 {
    param([System.Collections.Generic.List[byte]]$Buffer, [uint32]$Value)
    $Buffer.AddRange([System.BitConverter]::GetBytes($Value))
}

function Add-Single {
    param([System.Collections.Generic.List[byte]]$Buffer, [single]$Value)
    $Buffer.AddRange([System.BitConverter]::GetBytes($Value))
}

function Add-FixedString {
    param([System.Collections.Generic.List[byte]]$Buffer, [string]$Value, [int]$Size)
    $bytes = New-Object byte[] $Size
    if ($Value) {
        $encoded = [System.Text.Encoding]::ASCII.GetBytes($Value)
        $count = [Math]::Min($encoded.Length, $Size - 1)
        [Array]::Copy($encoded, 0, $bytes, 0, $count)
    }
    $Buffer.AddRange($bytes)
}

function Send-Frame {
    param([System.IO.Stream]$Stream, [uint16]$Type, [byte[]]$Payload)

    $frame = New-Object byte[] (4 + $Payload.Length)
    [Array]::Copy([System.BitConverter]::GetBytes($Type), 0, $frame, 0, 2)
    [Array]::Copy([System.BitConverter]::GetBytes([uint16]$Payload.Length), 0, $frame, 2, 2)
    if ($Payload.Length -gt 0) { [Array]::Copy($Payload, 0, $frame, 4, $Payload.Length) }

    $Stream.Write($frame, 0, $frame.Length)
    $Stream.Flush()
}

function Read-Exact {
    param([System.IO.Stream]$Stream, [int]$Count)

    $buffer = New-Object byte[] $Count
    $offset = 0
    while ($offset -lt $Count) {
        $read = $Stream.Read($buffer, $offset, $Count - $offset)
        if ($read -le 0) { throw 'The connection was closed while reading' }
        $offset += $read
    }
    return , $buffer
}

function Read-Frame {
    param([System.IO.Stream]$Stream)

    $header = Read-Exact $Stream 4
    $type = [System.BitConverter]::ToUInt16($header, 0)
    $size = [System.BitConverter]::ToUInt16($header, 2)
    $payload = if ($size -gt 0) { Read-Exact $Stream $size } else { New-Object byte[] 0 }

    return [pscustomobject]@{ Type = $type; Payload = $payload }
}

function Read-Until {
    param([System.IO.Stream]$Stream, [uint16]$Type, [int]$MaxFrames = 300)

    for ($i = 0; $i -lt $MaxFrames; $i++) {
        $frame = Read-Frame $Stream
        if ($frame.Type -eq $Type) { return $frame }
    }

    throw "Message type $Type was not received"
}

if ($RelayPort -eq 0) { $RelayPort = 17800 + (Get-Random -Minimum 10 -Maximum 80) }
if ($GamePort -eq 0) { $GamePort = 7500 + (Get-Random -Minimum 10 -Maximum 80) }

$serverExe = Find-Exe 'Spore-Multiplayer-Server.exe'
$relayExe = Find-Exe 'Spore-Multiplayer-Relay.exe'

$serverLog = Join-Path ([System.IO.Path]::GetTempPath()) 'spore-relay-host-server.log'
$relayLog = Join-Path ([System.IO.Path]::GetTempPath()) 'spore-relay-host-relay.log'
foreach ($path in @($serverLog, $relayLog)) { if (Test-Path $path) { Remove-Item $path -Force } }

Write-Host "Game port:  $GamePort"
Write-Host "Relay port: $RelayPort"
Write-Host ''

$relay = Start-Process -FilePath $relayExe `
    -ArgumentList @('--port', $RelayPort, '--log-level', 'debug') `
    -RedirectStandardOutput $relayLog -RedirectStandardError ($relayLog + '.err') `
    -WindowStyle Hidden -PassThru

Start-Sleep -Milliseconds 700

$server = Start-Process -FilePath $serverExe `
    -ArgumentList @('--port', $GamePort, '--relay', "127.0.0.1:$RelayPort", '--log-level', 'debug') `
    -RedirectStandardOutput $serverLog -RedirectStandardError ($serverLog + '.err') `
    -WindowStyle Hidden -PassThru

Start-Sleep -Seconds 2

$client = $null

try {
    Write-Host 'Test 1: the server opened a relay room'

    $serverLogText = Get-Content $serverLog -Raw
    Check ($serverLogText -match 'Relay room ready') 'The server created a relay room'

    $match = [regex]::Match($serverLogText, 'ticket ([0-9a-f]{16})')
    Check ($match.Success) 'The ticket is logged'
    $ticket = $match.Groups[1].Value

    Write-Host ''
    Write-Host "Test 2: a game client joins through the relay (ticket $ticket)"

    $client = New-Object System.Net.Sockets.TcpClient
    $client.NoDelay = $true
    $client.Connect('127.0.0.1', $RelayPort)
    $stream = $client.GetStream()
    $stream.ReadTimeout = 8000

    # Relay handshake: magic, role = client, reserved, ticket.
    $handshake = New-Object 'System.Collections.Generic.List[byte]'
    Add-UInt16 $handshake 0x4C52
    $handshake.Add([byte]2)
    $handshake.Add([byte]0)
    $handshake.AddRange([System.Text.Encoding]::ASCII.GetBytes($ticket))
    $stream.Write($handshake.ToArray(), 0, 20)
    $stream.Flush()

    $result = Read-Exact $stream 1
    Check ($result[0] -eq 0) "The relay accepted the client ($($result[0]))"

    Write-Host ''
    Write-Host 'Test 3: the game handshake works through the relay'

    $hello = New-Object 'System.Collections.Generic.List[byte]'
    Add-UInt16 $hello 1
    Add-UInt16 $hello 1
    Add-UInt32 $hello ([uint32]305419896)
    Add-FixedString $hello 'RelayBob' 24
    $hello.Add([byte]5)
    $hello.Add([byte]0)
    Add-UInt32 $hello 0
    Add-UInt32 $hello 0
    Add-UInt32 $hello 0
    Send-Frame $stream 1 $hello.ToArray()

    $welcome = Read-Until $stream 2
    $playerId = [System.BitConverter]::ToUInt32($welcome.Payload, 0)
    Check ($playerId -gt 0) "The server assigned a player id through the relay ($playerId)"

    $name = [System.Text.Encoding]::ASCII.GetString($welcome.Payload, 40, 24).TrimEnd([char]0)
    Check ($name -eq 'RelayBob') "The name arrived intact ('$name')"

    $list = Read-Until $stream 4
    Check ($list.Payload[0] -eq 0) 'The player list is received'

    Write-Host ''
    Write-Host 'Test 4: chat flows through the relay'

    $chat = New-Object 'System.Collections.Generic.List[byte]'
    Add-UInt32 $chat 0
    Add-FixedString $chat 'relayed chat' 160
    Send-Frame $stream 10 $chat.ToArray()

    $chatFrame = Read-Until $stream 10
    $chatText = [System.Text.Encoding]::ASCII.GetString($chatFrame.Payload, 4, 160).TrimEnd([char]0)
    Check ($chatText -eq 'relayed chat') "Chat round-trips through the relay ('$chatText')"

    Write-Host ''
    Write-Host 'Test 5: the server logged the relayed player'

    Start-Sleep -Milliseconds 400
    $serverLogText = Get-Content $serverLog -Raw
    Check ($serverLogText -match 'joined through the relay') 'The server noticed the relayed connection'
    Check ($serverLogText -match 'RelayBob') 'The server logged the relayed player name'
}
finally {
    if ($client -ne $null) { try { $client.Close() } catch { } }

    foreach ($process in @($server, $relay)) {
        try {
            if ($process -ne $null -and -not $process.HasExited) {
                Stop-Process -Id $process.Id -Force
                $process.WaitForExit(3000) | Out-Null
            }
        }
        catch { }
    }
}

Write-Host ''
if ($script:failures -eq 0) {
    Write-Host 'All checks passed.' -ForegroundColor Green
    exit 0
}

Write-Host "$($script:failures) check(s) failed. Logs: $serverLog / $relayLog" -ForegroundColor Red
exit 1

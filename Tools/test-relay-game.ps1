<#
.SYNOPSIS
    End-to-end test: the real game protocol through the relay.

.DESCRIPTION
    Starts the dedicated server, a relay, and connects a fake Spore client to the
    server *through* the relay. It then checks the normal handshake, the player
    list and the state relay, proving the tunnel is transparent for the game.

    The relay is used the same way the mod will use it:
      host side:  relay -> server          (role 1, ticket)
      client side: game client -> relay    (role 2, ticket)

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File Tools\test-relay-game.ps1
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
    if ($Payload.Length -gt 0) {
        [Array]::Copy($Payload, 0, $frame, 4, $Payload.Length)
    }

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

# --- transparent bridge -----------------------------------------------------
#
# The relay forwards the client <-> server stream. In this test the "host"
# process is simulated inline: Pump transfers whatever is available in either
# direction, and every read below pumps first.

$script:tunnelStream = $null
$script:serverStream = $null

function Pump {
    $buffer = New-Object byte[] 8192

    if ($script:tunnelStream.DataAvailable) {
        $read = $script:tunnelStream.Read($buffer, 0, $buffer.Length)
        if ($read -le 0) { throw 'The tunnel was closed' }
        $script:serverStream.Write($buffer, 0, $read)
        $script:serverStream.Flush()
    }

    if ($script:serverStream.DataAvailable) {
        $read = $script:serverStream.Read($buffer, 0, $buffer.Length)
        if ($read -le 0) { throw 'The server closed the connection' }
        $script:tunnelStream.Write($buffer, 0, $read)
        $script:tunnelStream.Flush()
    }
}

function Read-ExactPumped {
    param([System.IO.Stream]$Stream, [int]$Count)

    $buffer = New-Object byte[] $Count
    $offset = 0

    while ($offset -lt $Count) {
        Pump
        if (-not $Stream.DataAvailable) {
            Start-Sleep -Milliseconds 10
            continue
        }

        $read = $Stream.Read($buffer, $offset, $Count - $offset)
        if ($read -le 0) { throw 'The connection was closed while reading' }
        $offset += $read
    }

    return , $buffer
}

function Read-FramePumped {
    param([System.IO.Stream]$Stream)

    $header = Read-ExactPumped $Stream 4
    $type = [System.BitConverter]::ToUInt16($header, 0)
    $size = [System.BitConverter]::ToUInt16($header, 2)
    $payload = if ($size -gt 0) { Read-ExactPumped $Stream $size } else { New-Object byte[] 0 }

    return [pscustomobject]@{ Type = $type; Payload = $payload }
}

function Read-UntilPumped {
    param([System.IO.Stream]$Stream, [uint16]$Type, [int]$MaxFrames = 300)

    for ($i = 0; $i -lt $MaxFrames; $i++) {
        $frame = Read-FramePumped $Stream
        if ($frame.Type -eq $Type) { return $frame }
    }

    throw "Message type $Type was not received"
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

if ($RelayPort -eq 0) { $RelayPort = 17600 + (Get-Random -Minimum 10 -Maximum 80) }
if ($GamePort -eq 0) { $GamePort = 7300 + (Get-Random -Minimum 10 -Maximum 80) }

$serverExe = Find-Exe 'Spore-Multiplayer-Server.exe'
$relayExe = Find-Exe 'Spore-Multiplayer-Relay.exe'

$serverLog = Join-Path ([System.IO.Path]::GetTempPath()) 'spore-relay-game-server.log'
$relayLog = Join-Path ([System.IO.Path]::GetTempPath()) 'spore-relay-game-relay.log'
foreach ($path in @($serverLog, $relayLog)) { if (Test-Path $path) { Remove-Item $path -Force } }

Write-Host "Server: $serverExe"
Write-Host "Relay:  $relayExe"
Write-Host "Game port:  $GamePort"
Write-Host "Relay port: $RelayPort"
Write-Host ''

$server = Start-Process -FilePath $serverExe `
    -ArgumentList @('--port', $GamePort, '--log-level', 'debug') `
    -RedirectStandardOutput $serverLog -RedirectStandardError ($serverLog + '.err') `
    -WindowStyle Hidden -PassThru

$relay = Start-Process -FilePath $relayExe `
    -ArgumentList @('--port', $RelayPort, '--log-level', 'debug') `
    -RedirectStandardOutput $relayLog -RedirectStandardError ($relayLog + '.err') `
    -WindowStyle Hidden -PassThru

Start-Sleep -Milliseconds 900

$hostTunnel = $null
$clientTunnel = $null

try {
    $ticket = 'deadbeefcafe1234'

    Write-Host 'Test 1: the server connects to the relay as host'

    # The "host side" of the tunnel: the relay connection is bridged to the server.
    $hostTunnel = New-Object System.Net.Sockets.TcpClient
    $hostTunnel.NoDelay = $true
    $hostTunnel.Connect('127.0.0.1', $RelayPort)
    $hostTunnelStream = $hostTunnel.GetStream()

    $handshake = New-Object 'System.Collections.Generic.List[byte]'
    Add-UInt16 $handshake 0x4C52
    $handshake.Add([byte]1)
    $handshake.Add([byte]0)
    $handshake.AddRange([System.Text.Encoding]::ASCII.GetBytes($ticket))
    $hostTunnelStream.Write($handshake.ToArray(), 0, 20)
    $hostTunnelStream.Flush()

    $result = Read-Exact $hostTunnelStream 1
    Check ($result[0] -eq 0) "The relay accepts the host ($($result[0]))"

    # Bridge: relay <-> game server, exactly what the host process does.
    $serverConnection = New-Object System.Net.Sockets.TcpClient
    $serverConnection.NoDelay = $true
    $serverConnection.Connect('127.0.0.1', $GamePort)
    $serverStream = $serverConnection.GetStream()

    Write-Host ''
    Write-Host 'Test 2: a game client connects through the relay'

    $clientTunnel = New-Object System.Net.Sockets.TcpClient
    $clientTunnel.NoDelay = $true
    $clientTunnel.Connect('127.0.0.1', $RelayPort)
    $clientStream = $clientTunnel.GetStream()

    $handshake = New-Object 'System.Collections.Generic.List[byte]'
    Add-UInt16 $handshake 0x4C52
    $handshake.Add([byte]2)
    $handshake.Add([byte]0)
    $handshake.AddRange([System.Text.Encoding]::ASCII.GetBytes($ticket))
    $clientStream.Write($handshake.ToArray(), 0, 20)
    $clientStream.Flush()

    $result = Read-Exact $clientStream 1
    Check ($result[0] -eq 0) "The relay accepts the client ($($result[0]))"

    # Bridge both directions inline, like a real host would.
    $script:tunnelStream = $hostTunnelStream
    $script:serverStream = $serverStream

    Start-Sleep -Milliseconds 200

    Write-Host ''
    Write-Host 'Test 3: the game handshake works through the tunnel'

    $hello = New-Object 'System.Collections.Generic.List[byte]'
    Add-UInt16 $hello 1
    Add-UInt16 $hello 1
    Add-UInt32 $hello ([uint32]305419896)
    Add-FixedString $hello 'TunnelBob' 24
    $hello.Add([byte]5)
    $hello.Add([byte]0)
    Add-UInt32 $hello 0
    Add-UInt32 $hello 0
    Add-UInt32 $hello 0

    Send-Frame $clientStream 1 $hello.ToArray()
    Pump

    $welcome = Read-UntilPumped $clientStream 2
    $playerId = [System.BitConverter]::ToUInt32($welcome.Payload, 0)
    Check ($playerId -gt 0) "The server assigned a player id through the relay ($playerId)"

    $assignedName = [System.Text.Encoding]::ASCII.GetString($welcome.Payload, 40, 24).TrimEnd([char]0)
    Check ($assignedName -eq 'TunnelBob') "The name arrived intact ('$assignedName')"

    $list = Read-UntilPumped $clientStream 4
    Check ($list.Payload[0] -eq 0) 'The player list is received'

    Write-Host ''
    Write-Host 'Test 4: state and chat flow through the tunnel'

    $state = New-Object 'System.Collections.Generic.List[byte]'
    Add-Single $state 11
    Add-Single $state 22
    Add-Single $state 33
    Add-Single $state 0
    Add-Single $state 0
    Add-Single $state 0
    Add-Single $state 1
    $state.Add([byte]5)
    $state.Add([byte]1)
    Add-UInt16 $state 0
    Add-UInt32 $state 0
    Send-Frame $clientStream 8 $state.ToArray()
    Pump

    $chat = New-Object 'System.Collections.Generic.List[byte]'
    Add-UInt32 $chat 0
    Add-FixedString $chat 'through the tunnel' 160
    Send-Frame $clientStream 10 $chat.ToArray()
    Pump

    $chatFrame = Read-UntilPumped $clientStream 10
    $chatText = [System.Text.Encoding]::ASCII.GetString($chatFrame.Payload, 4, 160).TrimEnd([char]0)
    Check ($chatText -eq 'through the tunnel') "Chat is echoed back through the relay ('$chatText')"

    Write-Host ''
    Write-Host 'Test 5: the server saw the connection'

    Start-Sleep -Milliseconds 300
    $serverLogText = Get-Content $serverLog -Raw
    Check ($serverLogText -match "TunnelBob") 'The server logged the player that joined through the relay'
}
finally {
    foreach ($connection in @($clientTunnel, $hostTunnel, $serverConnection)) {
        if ($connection -ne $null) { try { $connection.Close() } catch { } }
    }

    foreach ($process in @($relay, $server)) {
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

<#
.SYNOPSIS
    Integration test for the Spore Multiplayer dedicated server.

.DESCRIPTION
    Starts the server on a free port, connects a few fake Spore clients and
    checks the handshake, the player list, the state relay, the chat, the ping
    and the disconnect notifications.

    The test speaks the real wire protocol (see Shared/MpProtocol.h), so it
    validates the server without needing the game.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File Tools\test-server.ps1
#>

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

function New-Payload {
    param([byte[]]$Bytes = @())

    return , ([byte[]]$Bytes)
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

function New-HelloPayload {
    param([string]$Name, [uint16]$Protocol = 1, [byte]$Stage = 5)

    $buffer = New-Object 'System.Collections.Generic.List[byte]'
    Add-UInt16 $buffer $Protocol
    Add-UInt16 $buffer 1
    Add-UInt32 $buffer 0x12345678
    Add-FixedString $buffer $Name 24
    $buffer.Add($Stage)
    $buffer.Add(0)
    # ResourceKey (instance, type, group)
    Add-UInt32 $buffer 0
    Add-UInt32 $buffer 0
    Add-UInt32 $buffer 0
    return , $buffer.ToArray()
}

function New-LocalStatePayload {
    param([single]$X, [single]$Y, [single]$Z, [byte]$Stage = 5)

    $buffer = New-Object 'System.Collections.Generic.List[byte]'
    Add-Single $buffer $X
    Add-Single $buffer $Y
    Add-Single $buffer $Z
    Add-Single $buffer 0
    Add-Single $buffer 0
    Add-Single $buffer 0
    Add-Single $buffer 1
    $buffer.Add($Stage)
    $buffer.Add(1)   # kStateHasPosition
    Add-UInt16 $buffer 0
    Add-UInt32 $buffer 0
    return , $buffer.ToArray()
}

function New-ChatPayload {
    param([string]$Text)

    $buffer = New-Object 'System.Collections.Generic.List[byte]'
    Add-UInt32 $buffer 0
    Add-FixedString $buffer $Text 160
    return , $buffer.ToArray()
}

function New-PingPayload {
    param([uint32]$ClientTime)

    $buffer = New-Object 'System.Collections.Generic.List[byte]'
    Add-UInt32 $buffer $ClientTime
    return , $buffer.ToArray()
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

function Read-Frame {
    param([System.IO.Stream]$Stream)

    $header = Read-Exact $Stream 4
    $type = [System.BitConverter]::ToUInt16($header, 0)
    $size = [System.BitConverter]::ToUInt16($header, 2)
    $payload = if ($size -gt 0) { Read-Exact $Stream $size } else { New-Object byte[] 0 }

    return [pscustomobject]@{ Type = $type; Payload = $payload }
}

function Read-Until {
    param([System.IO.Stream]$Stream, [uint16]$Type, [int]$MaxFrames = 400)

    for ($i = 0; $i -lt $MaxFrames; $i++) {
        $frame = Read-Frame $Stream
        if ($frame.Type -eq $Type) { return $frame }
    }

    throw "Message type $Type was not received"
}

function Connect-Client {
    param([int]$Port, [string]$Name, [uint16]$Protocol = 1, [byte]$Stage = 5)

    $client = New-Object System.Net.Sockets.TcpClient
    $client.NoDelay = $true
    $client.Connect('127.0.0.1', $Port)
    $stream = $client.GetStream()
    $stream.ReadTimeout = 5000

    Send-Frame $stream 1 (New-HelloPayload -Name $Name -Protocol $Protocol -Stage $Stage)

    return [pscustomobject]@{ Client = $client; Stream = $stream }
}

# ---------------------------------------------------------------------------
# Start the server
# ---------------------------------------------------------------------------

$exe = Find-ServerExe
$port = 7000 + (Get-Random -Minimum 100 -Maximum 900)

Write-Host "Server: $exe"
Write-Host "Port:   $port"
Write-Host ''

$logPath = Join-Path ([System.IO.Path]::GetTempPath()) 'spore-multiplayer-server-test.log'
if (Test-Path $logPath) { Remove-Item $logPath -Force }

# The server output goes straight to a file: no pipes that could block the server
# and no need for asynchronous readers.
$server = Start-Process -FilePath $exe `
    -ArgumentList @('--port', $port, '--log-level', 'debug', '--timeout', '5') `
    -RedirectStandardOutput $logPath `
    -RedirectStandardError ($logPath + '.err') `
    -WindowStyle Hidden `
    -PassThru

Start-Sleep -Milliseconds 700

if ($server.HasExited) {
    $log = if (Test-Path $logPath) { Get-Content $logPath -Raw } else { '' }
    throw "The server exited immediately: $log"
}

$alice = $null
$bob = $null
$rejected = $null

try {
    Write-Host 'Test 1: handshake'

    $alice = Connect-Client -Port $port -Name 'Alice'
    $welcome = Read-Until $alice.Stream 2
    $aliceId = [System.BitConverter]::ToUInt32($welcome.Payload, 0)
    $tickRate = [System.BitConverter]::ToUInt16($welcome.Payload, 4)
    $sessionName = [System.Text.Encoding]::ASCII.GetString($welcome.Payload, 8, 32).TrimEnd([char]0)
    $assignedName = [System.Text.Encoding]::ASCII.GetString($welcome.Payload, 40, 24).TrimEnd([char]0)

    Check ($aliceId -gt 0) "Alice got a player id ($aliceId)"
    Check ($tickRate -eq 20) "Welcome reports the tick rate ($tickRate)"
    Check ($sessionName -eq 'Spore Co-op') "Welcome reports the session name ('$sessionName')"
    Check ($assignedName -eq 'Alice') "Welcome reports the assigned name ('$assignedName')"

    $list = Read-Until $alice.Stream 4
    Check ($list.Payload[0] -eq 0) 'The player list is empty for the first player'

    Write-Host ''
    Write-Host 'Test 2: player list and join notification'

    $bob = Connect-Client -Port $port -Name 'Bob'
    $bobWelcome = Read-Until $bob.Stream 2
    $bobId = [System.BitConverter]::ToUInt32($bobWelcome.Payload, 0)
    Check ($bobId -ne $aliceId) "Bob got a different id ($bobId)"

    $bobList = Read-Until $bob.Stream 4
    Check ($bobList.Payload[0] -eq 1) 'Bob sees one player in the list'
    $listedName = [System.Text.Encoding]::ASCII.GetString($bobList.Payload, 8, 24).TrimEnd([char]0)
    Check ($listedName -eq 'Alice') "Bob sees Alice in the list ('$listedName')"

    $joined = Read-Until $alice.Stream 5
    $joinedId = [System.BitConverter]::ToUInt32($joined.Payload, 0)
    Check ($joinedId -eq $bobId) 'Alice is notified that Bob joined'

    Write-Host ''
    Write-Host 'Test 3: position relay'

    Send-Frame $alice.Stream 8 (New-LocalStatePayload -X 1 -Y 2 -Z 3)
    Send-Frame $bob.Stream 8 (New-LocalStatePayload -X 4 -Y 5 -Z 6)

    $aliceStates = Read-Until $alice.Stream 9
    $stateCount = $aliceStates.Payload[0]
    Check ($stateCount -eq 1) "Alice receives one state (count $stateCount)"
    if ($stateCount -eq 1) {
        $stateId = [System.BitConverter]::ToUInt32($aliceStates.Payload, 4)
        $stateX = [System.BitConverter]::ToSingle($aliceStates.Payload, 8)
        $stateY = [System.BitConverter]::ToSingle($aliceStates.Payload, 12)
        $stateZ = [System.BitConverter]::ToSingle($aliceStates.Payload, 16)
        $stateFlags = $aliceStates.Payload[37]
        Check ($stateId -eq $bobId) 'The state belongs to Bob'
        Check ($stateX -eq 4 -and $stateY -eq 5 -and $stateZ -eq 6) "The position is relayed ($stateX, $stateY, $stateZ)"
        Check (($stateFlags -band 1) -eq 1) 'The position flag is set'
    }

    $bobStates = Read-Until $bob.Stream 9
    Check ($bobStates.Payload[0] -eq 1) 'Bob receives one state'
    if ($bobStates.Payload[0] -eq 1) {
        $stateId = [System.BitConverter]::ToUInt32($bobStates.Payload, 4)
        $stateX = [System.BitConverter]::ToSingle($bobStates.Payload, 8)
        Check ($stateId -eq $aliceId) 'The state belongs to Alice'
        Check ($stateX -eq 1) "Alice position is relayed ($stateX)"
    }

    Write-Host ''
    Write-Host 'Test 4: chat'

    Send-Frame $alice.Stream 10 (New-ChatPayload -Text 'hello from alice')

    $aliceChat = Read-Until $alice.Stream 10
    $chatSender = [System.BitConverter]::ToUInt32($aliceChat.Payload, 0)
    $chatText = [System.Text.Encoding]::ASCII.GetString($aliceChat.Payload, 4, 160).TrimEnd([char]0)
    Check ($chatSender -eq $aliceId) 'The chat keeps the sender id'
    Check ($chatText -eq 'hello from alice') "The chat text is relayed ('$chatText')"

    $bobChat = Read-Until $bob.Stream 10
    $bobChatText = [System.Text.Encoding]::ASCII.GetString($bobChat.Payload, 4, 160).TrimEnd([char]0)
    Check ($bobChatText -eq 'hello from alice') "Bob receives Alice chat ('$bobChatText')"

    Write-Host ''
    Write-Host 'Test 5: ping'

    Send-Frame $alice.Stream 11 (New-PingPayload -ClientTime 4242)
    $pong = Read-Until $alice.Stream 12
    $clientTime = [System.BitConverter]::ToUInt32($pong.Payload, 0)
    Check ($clientTime -eq 4242) "Pong echoes the client time ($clientTime)"

    Write-Host ''
    Write-Host 'Test 6: protocol mismatch is rejected'

    $rejected = Connect-Client -Port $port -Name 'Old' -Protocol 99
    $reject = Read-Until $rejected.Stream 3
    $reason = $reject.Payload[0]
    Check ($reason -eq 1) "The client is rejected with ProtocolMismatch ($reason)"

    Write-Host ''
    Write-Host 'Test 7: avatar asset relay'

    # Build a fake "creation PNG": the server must relay the bytes unchanged.
    $assetSize = 3000
    $asset = New-Object byte[] $assetSize
    for ($i = 0; $i -lt $assetSize; $i++) { $asset[$i] = [byte](($i * 7) % 256) }
    $assetId = [uint32]0x1234ABCD

    $info = New-Object 'System.Collections.Generic.List[byte]'
    Add-UInt32 $info $assetId
    Add-UInt32 $info ([uint32]$assetSize)
    Add-UInt32 $info $bobId
    Add-UInt32 $info 0
    Add-UInt32 $info 0
    Add-UInt32 $info 0
    Send-Frame $bob.Stream 15 $info.ToArray()

    $chunkSize = 1024
    for ($offset = 0; $offset -lt $assetSize; $offset += $chunkSize) {
        $count = [Math]::Min($chunkSize, $assetSize - $offset)

        $chunk = New-Object 'System.Collections.Generic.List[byte]'
        Add-UInt32 $chunk $assetId
        Add-UInt32 $chunk ([uint32]$offset)
        Add-UInt16 $chunk ([uint16]$count)
        Add-UInt16 $chunk 0
        for ($i = 0; $i -lt $count; $i++) { $chunk.Add($asset[$offset + $i]) }

        Send-Frame $bob.Stream 16 $chunk.ToArray()
    }

    $done = New-Object 'System.Collections.Generic.List[byte]'
    Add-UInt32 $done $assetId
    $done.Add([byte]1)
    $done.Add([byte]0)
    $done.Add([byte]0)
    $done.Add([byte]0)
    Send-Frame $bob.Stream 17 $done.ToArray()

    $assetInfo = Read-Until $alice.Stream 15
    $relayedId = [System.BitConverter]::ToUInt32($assetInfo.Payload, 0)
    $relayedSize = [System.BitConverter]::ToUInt32($assetInfo.Payload, 4)
    $relayedOwner = [System.BitConverter]::ToUInt32($assetInfo.Payload, 8)
    Check ($relayedId -eq $assetId) 'Alice receives the asset announcement'
    Check ($relayedSize -eq $assetSize) "The announced size is correct ($relayedSize)"
    Check ($relayedOwner -eq $bobId) 'The asset is attributed to Bob'

    $received = New-Object byte[] $assetSize
    $receivedBytes = 0
    for ($i = 0; $i -lt 20; $i++) {
        $chunkFrame = Read-Until $alice.Stream 16
        $offset = [System.BitConverter]::ToUInt32($chunkFrame.Payload, 4)
        $count = [System.BitConverter]::ToUInt16($chunkFrame.Payload, 8)
        [Array]::Copy($chunkFrame.Payload, 12, $received, $offset, $count)
        $receivedBytes += $count
        if ($receivedBytes -ge $assetSize) { break }
    }
    Check ($receivedBytes -eq $assetSize) "Alice receives the whole asset ($receivedBytes bytes)"

    $identical = $true
    for ($i = 0; $i -lt $assetSize; $i++) {
        if ($received[$i] -ne $asset[$i]) { $identical = $false; break }
    }
    Check $identical 'The asset bytes are relayed unchanged'

    $assetDone = Read-Until $alice.Stream 17
    $doneId = [System.BitConverter]::ToUInt32($assetDone.Payload, 0)
    Check ($doneId -eq $assetId) 'Alice receives the transfer completion'

    Write-Host ''
    Write-Host 'Test 8: leaving the session'

    $bob.Client.Close()
    $left = Read-Until $alice.Stream 6
    $leftId = [System.BitConverter]::ToUInt32($left.Payload, 0)
    Check ($leftId -eq $bobId) 'Alice is notified that Bob left'
}catch {
    Write-Host ''
    Write-Host "Unexpected error: $($_.Exception.Message)" -ForegroundColor Red
    Write-Host $_.ScriptStackTrace -ForegroundColor DarkGray
    $script:failures++
}
finally {
    foreach ($connection in @($alice, $bob, $rejected)) {
        if ($connection -ne $null) {
            try { $connection.Client.Close() } catch { }
        }
    }

    try {
        if (-not $server.HasExited) {
            Stop-Process -Id $server.Id -Force
            $server.WaitForExit(3000) | Out-Null
        }
    }
    catch { }
}

Write-Host ''
if ($script:failures -eq 0) {
    Write-Host 'All checks passed.' -ForegroundColor Green
    exit 0
}

Write-Host "$($script:failures) check(s) failed. Server log: $logPath" -ForegroundColor Red
exit 1


<#
.SYNOPSIS
    A fake Spore Multiplayer client, useful to test the mod without a second
    game installation.

.DESCRIPTION
    Connects to a Spore Multiplayer server, keeps the connection alive, sends a
    position that slowly moves in a circle and prints every message it receives.

    Press Ctrl+C to disconnect.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File Tools\fake-client.ps1 -Name Bob -Port 7000
#>
param(
    [string]$Name = 'FakePlayer',
    [string]$HostName = '127.0.0.1',
    [int]$Port = 7000,
    [double]$Radius = 200.0,
    [double]$Speed = 0.4,
    [string]$Say = ''
)

$ErrorActionPreference = 'Stop'

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

function Read-Frame {
    param([System.IO.Stream]$Stream)

    $header = New-Object byte[] 4
    $offset = 0
    while ($offset -lt 4) {
        $read = $Stream.Read($header, $offset, 4 - $offset)
        if ($read -le 0) { return $null }
        $offset += $read
    }

    $type = [System.BitConverter]::ToUInt16($header, 0)
    $size = [System.BitConverter]::ToUInt16($header, 2)
    $payload = New-Object byte[] $size
    $offset = 0
    while ($offset -lt $size) {
        $read = $Stream.Read($payload, $offset, $size - $offset)
        if ($read -le 0) { return $null }
        $offset += $read
    }

    return [pscustomobject]@{ Type = $type; Payload = $payload }
}

function New-LocalState {
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
    $buffer.Add(1)
    Add-UInt16 $buffer 0
    Add-UInt32 $buffer 0
    return , $buffer.ToArray()
}

# --- connect -------------------------------------------------------------

$client = New-Object System.Net.Sockets.TcpClient
$client.NoDelay = $true
$client.Connect($HostName, $Port)
$stream = $client.GetStream()

$hello = New-Object 'System.Collections.Generic.List[byte]'
Add-UInt16 $hello 1          # protocol version
Add-UInt16 $hello 1          # client build
Add-UInt32 $hello ([uint32]305419896) # nonce (0x12345678)
Add-FixedString $hello $Name 24
$hello.Add([byte]5)          # stage: Space
$hello.Add([byte]0)
Add-UInt32 $hello 0          # ship model
Add-UInt32 $hello 0
Add-UInt32 $hello 0

Send-Frame $stream 1 $hello.ToArray()

Write-Host "Connected to $HostName`:$Port as '$Name'. Press Ctrl+C to quit." -ForegroundColor Green

if ($Say -ne '') {
    $chat = New-Object 'System.Collections.Generic.List[byte]'
    Add-UInt32 $chat 0
    Add-FixedString $chat $Say 160
    Send-Frame $stream 10 $chat.ToArray()
    Write-Host "  Sent chat: $Say" -ForegroundColor Magenta
}

$stage = 5
$angle = 0.0

while ($true) {
    # Send a position that moves in a circle around the system centre.
    $angle += $Speed * 0.2
    $x = [single]([Math]::Cos($angle) * $Radius)
    $y = [single]0
    $z = [single]([Math]::Sin($angle) * $Radius)
    Send-Frame $stream 8 (New-LocalState -X $x -Y $y -Z $z -Stage $stage)

    # Print everything the server sent, without blocking for long.
    while ($stream.DataAvailable) {
        $frame = Read-Frame $stream
        if ($null -eq $frame) { break }

        switch ($frame.Type) {
            2 {
                $id = [System.BitConverter]::ToUInt32($frame.Payload, 0)
                $session = [System.Text.Encoding]::ASCII.GetString($frame.Payload, 8, 32).TrimEnd([char]0)
                Write-Host "  Welcome: player id $id in session '$session'" -ForegroundColor Cyan
            }
            4 {
                Write-Host "  Player list: $($frame.Payload[0]) player(s)" -ForegroundColor Cyan
            }
            5 {
                $id = [System.BitConverter]::ToUInt32($frame.Payload, 0)
                $name = [System.Text.Encoding]::ASCII.GetString($frame.Payload, 4, 24).TrimEnd([char]0)
                Write-Host "  Joined: #$id '$name'" -ForegroundColor Yellow
            }
            6 {
                $id = [System.BitConverter]::ToUInt32($frame.Payload, 0)
                Write-Host "  Left: #$id" -ForegroundColor DarkYellow
            }
            10 {
                $sender = [System.BitConverter]::ToUInt32($frame.Payload, 0)
                $text = [System.Text.Encoding]::ASCII.GetString($frame.Payload, 4, 160).TrimEnd([char]0)
                Write-Host "  Chat from #${sender}: $text" -ForegroundColor Magenta
            }
            12 { }
            default {
                Write-Host "  Message type $($frame.Type) ($($frame.Payload.Length) bytes)" -ForegroundColor DarkGray
            }
        }
    }

    Start-Sleep -Milliseconds 66
}

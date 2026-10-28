<#
.SYNOPSIS
    Integration test for the master server.

.DESCRIPTION
    Starts the master server, registers a fake session over the HTTP API, checks
    the public list, sends a heartbeat, verifies the timeout and the
    unregistration.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File Tools\test-master.ps1
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

function Find-MasterExe {
    $candidates = @(
        (Join-Path $PSScriptRoot '..\bin\Release\Spore-Multiplayer-Master.exe'),
        (Join-Path $PSScriptRoot '..\bin\Debug\Spore-Multiplayer-Master.exe'),
        (Join-Path $PSScriptRoot '..\Server\Spore-Multiplayer-Master\bin\Release\Spore-Multiplayer-Master.exe'),
        (Join-Path $PSScriptRoot '..\Server\Spore-Multiplayer-Master\bin\Debug\Spore-Multiplayer-Master.exe')
    )

    foreach ($candidate in $candidates) {
        if (Test-Path $candidate) { return (Resolve-Path $candidate).Path }
    }

    throw 'Spore-Multiplayer-Master.exe not found. Build the Master project first.'
}

if ($Port -eq 0) { $Port = 17400 + (Get-Random -Minimum 10 -Maximum 90) }

$exe = Find-MasterExe
$logPath = Join-Path ([System.IO.Path]::GetTempPath()) 'spore-multiplayer-master-test.log'
if (Test-Path $logPath) { Remove-Item $logPath -Force }

Write-Host "Master: $exe"
Write-Host "Port:   $Port"
Write-Host ''

$master = Start-Process -FilePath $exe `
    -ArgumentList @('--port', $Port, '--heartbeat', '2', '--timeout', '6', '--log-level', 'debug') `
    -RedirectStandardOutput $logPath `
    -RedirectStandardError ($logPath + '.err') `
    -WindowStyle Hidden `
    -PassThru

Start-Sleep -Milliseconds 900

if ($master.HasExited) {
    $log = if (Test-Path $logPath) { Get-Content $logPath -Raw } else { '' }
    throw "The master exited immediately: $log"
}

$base = "http://127.0.0.1:$Port"

function Invoke-Api {
    param([string]$Path, [string]$Method = 'GET', [string]$Body = '')

    $params = @{
        Uri         = "$base$Path"
        Method      = $Method
        TimeoutSec  = 10
    }

    if ($Body -ne '') {
        $params['Body'] = $Body
        $params['ContentType'] = 'application/json'
    }

    try {
        return Invoke-RestMethod @params
    }
    catch {
        return $null
    }
}

try {
    Write-Host 'Test 1: health endpoint'

    $health = Invoke-Api '/api/v1/health'
    Check ($health -ne $null) 'The health endpoint answers'
    Check ($health.ok -eq $true) 'The master reports ok'
    Check ($health.servers -eq 0) 'The master starts with no sessions'

    Write-Host ''
    Write-Host 'Test 2: empty list'

    $list = Invoke-Api '/api/v1/servers'
    Check ($list -ne $null) 'The list endpoint answers'
    Check ($list.count -eq 0) 'The list is empty'

    Write-Host ''
    Write-Host 'Test 3: registration'

    $registerBody = '{"name":"Test Session","port":7100,"players":0,"maxPlayers":8,"version":1}'
    $registered = Invoke-Api '/api/v1/register' -Method 'POST' -Body $registerBody

    Check ($registered -ne $null) 'The session registers'
    Check (-not [string]::IsNullOrEmpty($registered.token)) "A token is returned ($($registered.token))"
    Check ($registered.interval -gt 0) "The heartbeat interval is returned ($($registered.interval))"

    $token = $registered.token

    $list = Invoke-Api '/api/v1/servers'
    Check ($list.count -eq 1) "The list has one session ($($list.count))"

    if ($list.count -eq 1) {
        $session = $list.servers[0]
        Check ($session.name -eq 'Test Session') "The name is reported ('$($session.name)')"
        Check ($session.port -eq 7100) "The port is reported ($($session.port))"
        Check ($session.host -eq '127.0.0.1') "The public address is taken from the connection ($($session.host))"
    }

    Write-Host ''
    Write-Host 'Test 4: heartbeat updates the player count'

    $null = Invoke-Api '/api/v1/heartbeat' -Method 'POST' -Body "{`"token`":`"$token`",`"players`":3}"
    $list = Invoke-Api '/api/v1/servers'
    Check ($list.servers[0].players -eq 3) "The player count is updated ($($list.servers[0].players))"

    Write-Host ''
    Write-Host 'Test 5: unknown token is rejected'

    $bad = Invoke-Api '/api/v1/heartbeat' -Method 'POST' -Body '{"token":"nope","players":1}'
    Check ($bad -eq $null) 'An unknown token is rejected'

    Write-Host ''
    Write-Host 'Test 6: session times out without heartbeats'

    Write-Host '  waiting 8 s...'
    Start-Sleep -Seconds 8

    $list = Invoke-Api '/api/v1/servers'
    Check ($list.count -eq 0) "The session was dropped ($($list.count) left)"

    Write-Host ''
    Write-Host 'Test 7: unregistration'

    $registered = Invoke-Api '/api/v1/register' -Method 'POST' -Body $registerBody
    $token = $registered.token

    $list = Invoke-Api '/api/v1/servers'
    Check ($list.count -eq 1) 'The session is registered again'

    $null = Invoke-Api '/api/v1/unregister' -Method 'POST' -Body "{`"token`":`"$token`"}"
    $list = Invoke-Api '/api/v1/servers'
    Check ($list.count -eq 0) 'The session is removed on unregister'
}
finally {
    try {
        if (-not $master.HasExited) {
            Stop-Process -Id $master.Id -Force
            $master.WaitForExit(3000) | Out-Null
        }
    }
    catch { }
}

Write-Host ''
if ($script:failures -eq 0) {
    Write-Host 'All checks passed.' -ForegroundColor Green
    exit 0
}

Write-Host "$($script:failures) check(s) failed. Master log: $logPath" -ForegroundColor Red
exit 1

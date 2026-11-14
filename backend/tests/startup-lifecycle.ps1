param([string]$BackendImage = 'clipstream-backend:local')

# Isolated, unpublished containers only. Never connects to the application's database.
$ErrorActionPreference = 'Stop'
$prefix = 'clipstream-startup-' + [guid]::NewGuid().ToString('N').Substring(0, 10)
$network = "$prefix-net"
$volume = "$prefix-data"
$holder = "$prefix-volume-holder"
$db = "$prefix-db"
$redis = "$prefix-redis"
$api = "$prefix-api"
$worker = "$prefix-worker"
$cancel = "$prefix-cancel"
$timeout = "$prefix-timeout"
$badAuth = "$prefix-auth"
$created = @()
function Invoke-Docker {
    $output = & docker @args 2>&1
    if ($LASTEXITCODE -ne 0) { throw "docker $($args[0]) failed: $output" }
    $output
}
function Wait-For {
    param([scriptblock]$Probe, [string]$Description, [int]$Seconds = 180)
    $deadline = [DateTime]::UtcNow.AddSeconds($Seconds)
    do {
        if (& $Probe) { return }
        Start-Sleep -Milliseconds 500
    } while ([DateTime]::UtcNow -lt $deadline)
    throw "Timed out: $Description"
}
function Container-Logs([string]$Name) { (Invoke-Docker logs $Name) -join "`n" }
function Assert-Exit([string]$Name, [int]$Expected) {
    $state = (Invoke-Docker inspect $Name | ConvertFrom-Json)[0].State
    if ($state.Running -or $state.ExitCode -ne $Expected) {
        throw "$Name expected exit $Expected, got $($state.Status)/$($state.ExitCode)"
    }
}
try {
    Invoke-Docker network create $network | Out-Null
    # Keep the temporary memory-backed volume mounted while MySQL is stopped.
    # This preserves its schema across container restarts without slow disk initialization.
    Invoke-Docker volume create --driver local --opt type=tmpfs --opt device=tmpfs --opt o=size=1g $volume | Out-Null
    Invoke-Docker run -d --name $holder --network none --mount "type=volume,source=$volume,target=/data" `
        redis:7-alpine sleep infinity | Out-Null
    $created += $holder
    Invoke-Docker create --name $db --network $network --stop-timeout 120 `
        --mount "type=volume,source=$volume,target=/var/lib/mysql" -e MYSQL_ALLOW_EMPTY_PASSWORD=yes `
        -e MYSQL_DATABASE=clipstream_startup_test mysql:8.0 `
        --innodb-buffer-pool-size=128M --innodb-flush-log-at-trx-commit=2 | Out-Null
    $created += $db
    Invoke-Docker create --name $redis --network $network redis:7-alpine `
        redis-server --save '' --appendonly no | Out-Null
    $created += $redis
    Invoke-Docker start $db | Out-Null
    Wait-For { (Container-Logs $db) -match 'ready for connections.*port: 3306' } 'MySQL initialization'
    $environment = @('--network', $network, '-e', "MYSQL_HOST=$db", '-e', 'MYSQL_USER=root',
        '-e', 'MYSQL_DATABASE=clipstream_startup_test', '-e', "REDIS_HOST=$redis",
        '-e', 'MYSQL_POOL_SIZE=2', '-e', 'JWT_SECRET=startup-test-only-012345678901234567890123456789',
        '-e', 'STARTUP_TIMEOUT_SECONDS=90')
    Invoke-Docker run --rm @environment -e CLIPSTREAM_ROLE=migrate $BackendImage | Out-Null
    Invoke-Docker stop $db | Out-Null
    Assert-Exit $db 0
    if ((Container-Logs $db) -notmatch 'Shutdown complete') { throw 'MySQL did not shut down cleanly' }
    Write-Output 'PASS: MySQL shutdown completed with exit 0 and a 120s stop timeout'

    foreach ($entry in @(@($api, 'api'), @($worker, 'worker'))) {
        Invoke-Docker run -d --name $entry[0] @environment -e "CLIPSTREAM_ROLE=$($entry[1])" $BackendImage | Out-Null
        $created += $entry[0]
    }
    Wait-For { ((Container-Logs $api) -match 'Waiting for MySQL') -and
               ((Container-Logs $worker) -match 'Waiting for MySQL') } 'both processes waiting for MySQL'
    Invoke-Docker start $db | Out-Null
    Wait-For { ((Container-Logs $api) -match 'Waiting for Redis') -and
               ((Container-Logs $worker) -match 'Waiting for Redis') } 'both processes waiting for Redis'
    Invoke-Docker start $redis | Out-Null
    Wait-For { ((Container-Logs $api) -match '\[API\] Ready') -and
               ((Container-Logs $worker) -match '\[Worker\] Started') } 'API and worker ready'
    Invoke-Docker exec $api curl --fail --silent http://127.0.0.1:8080/health/ready
    foreach ($name in @($api, $worker)) {
        $state = (Invoke-Docker inspect $name | ConvertFrom-Json)[0]
        if (!$state.State.Running -or $state.RestartCount -ne 0) { throw "$name restarted during startup" }
    }
    Write-Output 'PASS: API and worker waited for late MySQL/Redis and became ready without restarting'

    Invoke-Docker run -d --name $cancel --network $network -e MYSQL_HOST=not-present `
        -e STARTUP_TIMEOUT_SECONDS=90 $BackendImage | Out-Null
    $created += $cancel
    Wait-For { (Container-Logs $cancel) -match 'Waiting for MySQL' } 'cancellable startup'
    Invoke-Docker stop --timeout 10 $cancel | Out-Null
    Assert-Exit $cancel 0
    Write-Output 'PASS: SIGTERM interrupted dependency waiting cleanly'

    Invoke-Docker run -d --name $timeout --network $network -e MYSQL_HOST=not-present `
        -e STARTUP_TIMEOUT_SECONDS=1 $BackendImage | Out-Null
    $created += $timeout
    Wait-For { ((Invoke-Docker inspect $timeout | ConvertFrom-Json)[0].State.Status) -eq 'exited' } 'startup timeout' 20
    Assert-Exit $timeout 1
    if ((Container-Logs $timeout) -notmatch 'Startup timed out waiting for MySQL') { throw 'Missing timeout diagnostic' }
    Write-Output 'PASS: startup timeout produced exit 1'

    Invoke-Docker run -d --name $badAuth @environment -e MYSQL_USER=invalid_startup_user $BackendImage | Out-Null
    $created += $badAuth
    Wait-For { ((Invoke-Docker inspect $badAuth | ConvertFrom-Json)[0].State.Status) -eq 'exited' } 'invalid authentication' 20
    Assert-Exit $badAuth 1
    if ((Container-Logs $badAuth) -match 'Waiting for MySQL') { throw 'Authentication failure was retried' }
    Write-Output 'PASS: invalid MySQL credentials failed without retry'
} finally {
    foreach ($name in $created) {
        & docker stop $name 2>&1 | Out-Null
        & docker rm $name 2>&1 | Out-Null
    }
    & docker network rm $network 2>&1 | Out-Null
    & docker volume rm $volume 2>&1 | Out-Null
}

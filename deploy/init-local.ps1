param([string]$Destination = "$PSScriptRoot/../temp/secrets")
$ErrorActionPreference = 'Stop'
$secretPath = [System.IO.Path]::GetFullPath($Destination)
New-Item -ItemType Directory -Force -Path $secretPath | Out-Null
foreach ($name in @('mysql_password','mysql_root_password','redis_password','jwt_secret','proxy_auth')) {
    $path = Join-Path $secretPath $name
    if (Test-Path -LiteralPath $path) { continue }
    $bytes = New-Object byte[] 48
    $rng = [System.Security.Cryptography.RandomNumberGenerator]::Create()
    try { $rng.GetBytes($bytes) } finally { $rng.Dispose() }
    [System.IO.File]::WriteAllText($path, [Convert]::ToBase64String($bytes))
}
# Twitch secret must be supplied by the operator; never overwrite it.
$twitch = Join-Path $secretPath 'twitch_client_secret'
if (-not (Test-Path -LiteralPath $twitch)) { [System.IO.File]::WriteAllText($twitch, '') }
if ($IsWindows -or $env:OS -eq 'Windows_NT') {
    $identity = [System.Security.Principal.WindowsIdentity]::GetCurrent().Name
    & icacls $secretPath /inheritance:r /grant:r "${identity}:(OI)(CI)F" | Out-Null
} else {
    & chmod 700 $secretPath
    Get-ChildItem -LiteralPath $secretPath -File | ForEach-Object { & chmod 600 $_.FullName }
}
Write-Output "Local secrets initialized: $secretPath. Fill twitch_client_secret to enable Twitch."

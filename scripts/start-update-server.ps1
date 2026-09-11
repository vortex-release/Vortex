param([string]$ReleaseDirectory = (Join-Path $env:LOCALAPPDATA 'Vortex\releases'),[int]$Port = 17843)
$ErrorActionPreference = 'Stop'
$root = Join-Path $env:LOCALAPPDATA 'Vortex'
$buildExe = Join-Path $root 'Build\app\Release\VortexUpdateServer.exe'
$exe = Join-Path $root 'server\VortexUpdateServer.exe'
if (-not (Test-Path -LiteralPath $buildExe)) { throw 'Build Vortex first.' }
New-Item -ItemType Directory -Path $ReleaseDirectory,(Join-Path $root 'logs') -Force | Out-Null
$ReleaseDirectory = [IO.Path]::GetFullPath($ReleaseDirectory)
$pidFile = Join-Path $root 'server-process.json'
if (Test-Path -LiteralPath $pidFile) {
    $saved = Get-Content -LiteralPath $pidFile -Raw | ConvertFrom-Json
    $running = Get-Process -Id $saved.id -ErrorAction SilentlyContinue
    if ($running -and $running.Path -eq $exe -and $running.StartTime.ToUniversalTime().Ticks -eq $saved.started) {
        Write-Output "Already running: http://127.0.0.1:$($saved.port)/"; return
    }
}
New-Item -ItemType Directory -Path (Split-Path $exe) -Force | Out-Null
Copy-Item -LiteralPath $buildExe -Destination $exe -Force
$server = Start-Process -FilePath $exe -ArgumentList @(('"' + $ReleaseDirectory + '"'),$Port) -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $root 'logs\update-server.log') -RedirectStandardError (Join-Path $root 'logs\update-server-error.log')
Start-Sleep -Milliseconds 500
if ($server.HasExited) { throw (Get-Content -LiteralPath (Join-Path $root 'logs\update-server-error.log') -Raw) }
@{id=$server.Id;started=$server.StartTime.ToUniversalTime().Ticks;port=$Port} | ConvertTo-Json | Set-Content -LiteralPath $pidFile
Write-Output "Your local download page: http://127.0.0.1:$Port/"
Write-Output 'Connect a stable HTTPS tunnel hostname to this address to make it available to other computers.'

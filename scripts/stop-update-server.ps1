$ErrorActionPreference = 'Stop'
$root = Join-Path $env:LOCALAPPDATA 'Vortex'
$file = Join-Path $root 'server-process.json'
if (-not (Test-Path -LiteralPath $file)) { Write-Output 'The server is not running.'; return }
$saved = Get-Content -LiteralPath $file -Raw | ConvertFrom-Json
$server = Get-Process -Id $saved.id -ErrorAction SilentlyContinue
$expected = Join-Path $root 'server\VortexUpdateServer.exe'
if ($server -and $server.Path -eq $expected -and $server.StartTime.ToUniversalTime().Ticks -eq $saved.started) { Stop-Process -Id $server.Id }
Remove-Item -LiteralPath $file
Write-Output 'Vortex update server stopped.'

$ErrorActionPreference = 'Stop'
$config = Get-Content -LiteralPath (Join-Path (Split-Path $PSScriptRoot) 'release\config.json') -Raw | ConvertFrom-Json
$version = $config.velopackVersion
$root = Join-Path $env:LOCALAPPDATA "Vortex\tools\vpk-$version"
if (Test-Path -LiteralPath (Join-Path $root 'tools\net8.0\any\vpk.dll')) { Write-Output 'Release tool is ready.'; return }
$headers = @{ 'User-Agent' = 'Vortex-Release-Tools'; Accept = 'application/vnd.github+json' }
$release = Invoke-RestMethod "https://api.github.com/repos/velopack/velopack/releases/tags/$version" -Headers $headers
$asset = $release.assets | Where-Object name -eq "vpk.$version.nupkg"
if (-not $asset -or -not $asset.digest -or $asset.digest -notmatch '^sha256:[a-fA-F0-9]{64}$') { throw 'Pinned release tool or its checksum was not found.' }
$zip = Join-Path $env:TEMP ("vortex-vpk-" + [Guid]::NewGuid().ToString('N') + '.zip')
try {
    Invoke-WebRequest -Uri $asset.browser_download_url -OutFile $zip -Headers $headers
    if ((Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash -ine $asset.digest.Substring(7)) { throw 'Release tool checksum verification failed.' }
    New-Item -ItemType Directory -Path $root -Force | Out-Null
    Expand-Archive -LiteralPath $zip -DestinationPath $root -Force
} finally { if (Test-Path -LiteralPath $zip) { Remove-Item -LiteralPath $zip } }
Write-Output 'Release tool is ready. Packaging uses the developer-only .NET 8 runtime.'

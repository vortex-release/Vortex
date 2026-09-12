param(
    [string]$Version,
    [string]$UpdateUrl,
    [string]$BuildDirectory = (Join-Path $env:LOCALAPPDATA 'Vortex\Build'),
    [string]$ReleaseDirectory = (Join-Path $env:LOCALAPPDATA 'Vortex\releases'),
    [string]$SignParams,
    [string]$AzureTrustedSignFile,
    [switch]$NoDesktopCopy,
    [switch]$LocalPreview
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$project = Split-Path $PSScriptRoot
. (Join-Path $PSScriptRoot 'build-support.ps1')
$config = Get-Content -LiteralPath (Join-Path $project 'release\config.json') -Raw | ConvertFrom-Json
if (-not $Version) { $Version = $config.version }
if (-not $PSBoundParameters.ContainsKey('UpdateUrl')) { $UpdateUrl = $config.updateUrl }
if ($Version -notmatch '^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$' -or @(([version]$Version).Major,([version]$Version).Minor,([version]$Version).Build | Where-Object { $_ -gt 65535 }).Count) { throw 'Use a stable semantic version with each component between 0 and 65535.' }
if ($Version -ne $config.version) { throw 'Update release/config.json first, or use release.ps1.' }
if ($UpdateUrl) {
    $uri = [Uri]$UpdateUrl
    $local = $uri.IsLoopback -and $uri.Scheme -eq 'http'
    if ((-not $uri.IsAbsoluteUri) -or ($uri.Scheme -ne 'https' -and -not ($LocalPreview -and $local)) -or $uri.UserInfo -or $uri.Query -or $uri.Fragment) {
        throw 'Public releases require a stable HTTPS URL without credentials, a query or fragment.'
    }
} elseif (-not $LocalPreview) { throw 'Configure your public HTTPS update address first, or use -LocalPreview for an offline preview installer.' }
$root = Join-Path $env:LOCALAPPDATA 'Vortex'
New-Item -ItemType Directory -Path $ReleaseDirectory -Force | Out-Null
$ReleaseDirectory = [IO.Path]::GetFullPath($ReleaseDirectory)
# Serialize packaging so two releases cannot write the same feed concurrently.
$lock = [IO.File]::Open((Join-Path $ReleaseDirectory '.publish.lock'), 'OpenOrCreate', 'ReadWrite', 'None')
try {
    if (Test-Path -LiteralPath (Join-Path $ReleaseDirectory 'releases.win.json')) {
        $feed = Get-Content -LiteralPath (Join-Path $ReleaseDirectory 'releases.win.json') -Raw | ConvertFrom-Json
        $versions = @($feed.Assets | Where-Object Type -eq 'Full' | ForEach-Object { [version]$_.Version })
        if ($versions.Count -and [version]$Version -le ($versions | Sort-Object -Descending | Select-Object -First 1)) {
            throw 'Each published release must have a higher version. Choose a new version or a separate test release folder.'
        }
    }
    & (Join-Path $project 'build.ps1') -BuildDirectory $BuildDirectory -NoDeploy -CMakeArguments @("-DVORTEX_UPDATE_URL=$UpdateUrl","-DVORTEX_VERSION_OVERRIDE=$Version")
    $stage = Join-Path $root ('staging\' + [Guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $stage -Force | Out-Null
    $app = Join-Path $BuildDirectory 'app\Release'
    # Package canonical outputs directly. A DLL-only rebuild need not relink the
    # launcher, so its POST_BUILD staging directory can still contain an older DLL.
    $payload = @{
        'Vortex.exe' = Join-Path $app 'Vortex.exe'
        'ObserverDemo.exe' = Join-Path $BuildDirectory 'Release\ObserverDemo.exe'
        'EntityAwarenessOverlay.dll' = Join-Path $BuildDirectory 'Release\EntityAwarenessOverlay.dll'
        'velopack_libc.dll' = Join-Path $project 'dependencies\velopack\lib\velopack_libc_win_x64_msvc.dll'
        'DefaultSettings.ini' = Join-Path $project 'DefaultSettings.ini'
    }
    foreach ($name in $payload.Keys) {
        Copy-Item -LiteralPath $payload[$name] -Destination (Join-Path $stage $name)
    }
    $profileStage = Join-Path $stage 'profile'
    New-Item -ItemType Directory -Path $profileStage -Force | Out-Null
    Get-ChildItem -LiteralPath (Join-Path $project 'profile') | Where-Object Name -ne 'background.png' |
        ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination $profileStage -Recurse }
    foreach ($name in @('Dear-ImGui-LICENSE.txt','MinHook-LICENSE.txt','Valve-Assets-NOTICE.txt','Velopack-LICENSE.txt','nlohmann-json-LICENSE.txt','Lucide-LICENSE.txt','Lefrizzel-Ai-LICENSE.txt')) {
        Copy-Item -LiteralPath (Join-Path $project "licenses\$name") -Destination $stage
    }
    Copy-Item -LiteralPath (Join-Path $project 'LICENSE') -Destination (Join-Path $stage 'LICENSE.txt')
    Copy-Item -LiteralPath (Join-Path $project 'release\Read Me.txt') -Destination $stage
    $snapshot = Get-Content -LiteralPath (Join-Path $project 'Updated Offsets\source.json') -Raw | ConvertFrom-Json
    [ordered]@{ name=$config.name; version=$Version; channel=$config.channel; offsetBuild=$snapshot.build_number; updateUrl=$UpdateUrl; dllSha256=(Get-FileHash -LiteralPath (Join-Path $stage 'EntityAwarenessOverlay.dll')).Hash } |
        ConvertTo-Json | Set-Content -LiteralPath (Join-Path $stage 'build-info.json') -Encoding UTF8
    $vpk = Join-Path $root "tools\vpk-$($config.velopackVersion)\tools\net8.0\any\vpk.dll"
    if (-not (Test-Path -LiteralPath $vpk)) { throw 'Run scripts\setup-release-tools.ps1 once before packaging.' }
    $pending = Join-Path $root ('pending\' + [Guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $pending -Force | Out-Null
    # Seed previous full packages and feed for delta generation, but never change the live feed during packing.
    Get-ChildItem -LiteralPath $ReleaseDirectory -File | Where-Object { $_.Extension -in '.json','.nupkg' -or $_.Name -eq 'RELEASES' } |
        ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination $pending }
    $arguments = @($vpk,'pack','--packId',$config.id,'--packTitle',$config.name,'--packAuthors',$config.publisher,'--packVersion',$Version,
        '--packDir',$stage,'--mainExe','Vortex.exe','--outputDir',$pending,'--channel',$config.channel,'--runtime','win-x64',
        '--icon',(Join-Path $project 'app\vortex.ico'),'--releaseNotes',(Join-Path $project 'release\notes.md'),'--noPortable',
        '--shortcuts','Desktop,StartMenuRoot','--yes')
    if ($SignParams) { $arguments += @('--signParams',$SignParams) }
    if ($AzureTrustedSignFile) { $arguments += @('--azureTrustedSignFile',[IO.Path]::GetFullPath($AzureTrustedSignFile)) }
    Invoke-BuildTool (Get-Command dotnet).Source $arguments
    $setup = Get-ChildItem -LiteralPath $pending -Filter '*Setup.exe' | Select-Object -First 1
    if (-not $setup) { throw 'The packager did not produce an installer.' }
    function Publish-File([string]$Source,[string]$Name) {
        $temporary = Join-Path $ReleaseDirectory ($Name + '.publishing')
        Copy-Item -LiteralPath $Source -Destination $temporary -Force
        $destination = Join-Path $ReleaseDirectory $Name
        if (Test-Path -LiteralPath $destination) { [IO.File]::Replace($temporary,$destination,[NullString]::Value) }
        else { [IO.File]::Move($temporary,$destination) }
    }
    # Versioned artifacts are published before the feed that advertises them.
    Get-ChildItem -LiteralPath $pending -Filter '*.nupkg' | ForEach-Object { Publish-File $_.FullName $_.Name }
    Publish-File $setup.FullName "Vortex-$Version-Setup.exe"
    Publish-File $setup.FullName 'Vortex-Setup.exe'
    Publish-File $setup.FullName 'VortexSetup.exe'
    $page = @"
<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width"><title>Vortex</title>
<style>body{background:#10121c;color:#f1f0ff;font:18px system-ui;max-width:680px;margin:12vh auto;padding:32px}h1{font-size:64px;margin:0}p{color:#bbb7cf;line-height:1.7}a{display:inline-block;background:#9663ff;color:white;padding:16px 24px;border-radius:10px;text-decoration:none}small{display:block;margin-top:40px;color:#9189ab}</style>
<h1>Vortex</h1><p>Your overlay, ready to go.<br>Version $Version for Windows x64.</p><a href="Vortex-Setup.exe">Download Vortex</a>
<small>Install once. New releases appear in the app.</small></html>
"@
    $page | Set-Content -LiteralPath (Join-Path $pending 'index.html') -Encoding UTF8
    Publish-File (Join-Path $pending 'index.html') 'index.html'
    Get-ChildItem -LiteralPath $pending -File | Where-Object { $_.Extension -eq '.json' -or $_.Name -eq 'RELEASES' } |
        ForEach-Object { Publish-File $_.FullName $_.Name }
    $checksummed = @('VortexSetup.exe','releases.win.json',"$($config.id)-$Version-full.nupkg")
    $lines = foreach ($name in $checksummed) {
        $artifact = Get-Item -LiteralPath (Join-Path $ReleaseDirectory $name)
        if ($artifact.Length -le 0) { throw "Empty release asset: $name" }
        (Get-FileHash -LiteralPath $artifact.FullName -Algorithm SHA256).Hash.ToLowerInvariant() + '  ' + $name
    }
    [IO.File]::WriteAllText((Join-Path $pending 'SHA256SUMS.txt'), ($lines -join "`n") + "`n", [Text.UTF8Encoding]::new($false))
    Publish-File (Join-Path $pending 'SHA256SUMS.txt') 'SHA256SUMS.txt'
    # Developer's ready-to-run copy contains only the same curated payload as the installer.
    if (-not $NoDesktopCopy) {
    $desktop = Join-Path (Get-DesktopDirectory) 'Vortex'
    New-Item -ItemType Directory -Path $desktop -Force | Out-Null
    Copy-Item -Path (Join-Path $stage '*') -Destination $desktop -Recurse -Force
    Copy-Item -LiteralPath (Join-Path $ReleaseDirectory "Vortex-$Version-Setup.exe") -Destination $desktop -Force
    }
    Write-Output "Installer: $ReleaseDirectory\Vortex-$Version-Setup.exe"
    if (-not $NoDesktopCopy) { Write-Output "App: $desktop\Vortex.exe" }
    Write-Output "Update feed: $ReleaseDirectory\releases.win.json"
    if (-not $SignParams -and -not $AzureTrustedSignFile) { Write-Output 'This local build is unsigned. Configure code signing before distributing a signed public release.' }
} finally { $lock.Dispose() }

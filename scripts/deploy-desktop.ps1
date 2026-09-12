param(
    [string]$BuildDirectory = (Join-Path $env:LOCALAPPDATA 'EntityAwarenessOverlay\Build'),
    [string]$Destination
)
$ErrorActionPreference='Stop'
$project=Split-Path $PSScriptRoot
. (Join-Path $PSScriptRoot 'build-support.ps1')
if (-not $Destination) { $Destination = Join-Path (Get-DesktopDirectory) 'CS2-Observer-Overlay' }
& (Join-Path $PSScriptRoot 'generate-offsets.ps1') -Check
$build=if ([IO.Path]::IsPathRooted($BuildDirectory)) { $BuildDirectory } else { Join-Path $project $BuildDirectory }
$release=Join-Path $build 'Release'
$dll=Get-Item -LiteralPath (Join-Path $release 'EntityAwarenessOverlay.dll')
$demo=Get-Item -LiteralPath (Join-Path $release 'ObserverDemo.exe')
$sourceFiles=@(Get-ChildItem -LiteralPath (Join-Path $project 'src'),(Join-Path $project 'include') -File -Recurse)
$sourceFiles+=Get-ChildItem -LiteralPath (Join-Path $project 'assets') -File | Where-Object { $_.Extension -in '.png','.bin' }
if ($sourceFiles | Where-Object { $_.LastWriteTimeUtc -gt $dll.LastWriteTimeUtc }) { throw 'Rebuild the DLL before deployment.' }
if (Get-ChildItem -LiteralPath (Join-Path $project 'demo'),(Join-Path $project 'include') -File -Recurse | Where-Object { $_.LastWriteTimeUtc -gt $demo.LastWriteTimeUtc }) { throw 'Rebuild the demo before deployment.' }
New-Item -ItemType Directory -Path $Destination -Force | Out-Null
foreach ($name in @('EntityAwarenessOverlay.dll','ObserverDemo.exe','awareness_cs2_live_probe.exe')) {
    Copy-Item -LiteralPath (Join-Path $release $name) -Destination $Destination -Force
}
Copy-Item -LiteralPath (Join-Path $project 'START-HERE.txt') -Destination (Join-Path $Destination 'Read Me.txt') -Force
Copy-Item -LiteralPath (Join-Path $project 'Camera Tracking.cmd') -Destination $Destination -Force
Copy-Item -LiteralPath (Join-Path $project 'LICENSE') -Destination (Join-Path $Destination 'LICENSE.txt') -Force
foreach ($name in @('Dear-ImGui-LICENSE.txt','MinHook-LICENSE.txt','Valve-Assets-NOTICE.txt','Lucide-LICENSE.txt','Lefrizzel-Ai-LICENSE.txt')) { Copy-Item -LiteralPath (Join-Path $project "licenses\$name") -Destination $Destination -Force }
Copy-Item -LiteralPath (Join-Path $project 'DefaultSettings.ini') -Destination $Destination -Force
$profileAssets=Join-Path $Destination 'profile'
New-Item -ItemType Directory -Path $profileAssets -Force | Out-Null
Get-ChildItem -LiteralPath (Join-Path $project 'profile') | Where-Object Name -ne 'background.png' | ForEach-Object {
    Copy-Item -LiteralPath $_.FullName -Destination $profileAssets -Recurse -Force
}
# Preserve the user's saved choices on updates; seed the shared defaults only on a fresh install.
$activeProfile=Join-Path $Destination 'OverlaySettings.ini'
if (-not (Test-Path -LiteralPath $activeProfile)) {
    Copy-Item -LiteralPath (Join-Path $project 'DefaultSettings.ini') -Destination $activeProfile
}
$snapshot=Get-Content -LiteralPath (Join-Path $project 'Updated Offsets\source.json') -Raw | ConvertFrom-Json
$manifest=[ordered]@{version=((Get-Content -LiteralPath (Join-Path $project 'release\config.json') -Raw | ConvertFrom-Json).version);offset_build=$snapshot.build_number;offset_snapshot=$snapshot.snapshot_id;offset_timestamp=$snapshot.timestamp;bone_layout_sha256=$snapshot.bone_layout_sha256;render_layout_sha256=$snapshot.render_layout_sha256;trajectory_layout_sha256=$snapshot.trajectory_layout_sha256;preview_model_sha256=(Get-FileHash -LiteralPath (Join-Path $project 'assets\preview-model.bin')).Hash;dll_sha256=(Get-FileHash -LiteralPath (Join-Path $Destination 'EntityAwarenessOverlay.dll')).Hash;source=$project}
$manifest | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $Destination 'build-info.json')
Write-Output "Ready on your Desktop: $Destination"

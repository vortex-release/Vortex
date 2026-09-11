param([string]$DumperPath)
$ErrorActionPreference='Stop'
$project=Split-Path $PSScriptRoot
. (Join-Path $PSScriptRoot 'build-support.ps1')
if (-not $DumperPath) { $DumperPath = Join-Path (Get-DesktopDirectory) 'cs2-dumper-main\target\release\cs2-dumper.exe' }
if (-not (Test-Path -LiteralPath $DumperPath)) { throw "cs2-dumper was not found: $DumperPath. Pass -DumperPath to select your local build." }
if (-not (Get-Process cs2 -ErrorAction SilentlyContinue)) { throw 'Open CS2 before refreshing offsets.' }
$scratch=Join-Path ([IO.Path]::GetTempPath()) ('AwarenessOffsets-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $scratch | Out-Null
try {
    & $DumperPath --output $scratch --file-types json,hpp --no-log-file -vv
    if ($LASTEXITCODE -ne 0) { throw 'The local dumper failed. Existing offsets were preserved.' }
    # Importer validates every consumed field and matching C++ header before copying.
    & (Join-Path $PSScriptRoot 'generate-offsets.ps1') -InputDirectory $scratch
    $destination=Join-Path $project 'Updated Offsets'
    Get-ChildItem -LiteralPath $scratch -File | Copy-Item -Destination $destination -Force
} finally {
    $resolved=[IO.Path]::GetFullPath($scratch)
    $tempRoot=[IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\')+'\'
    if ($resolved.StartsWith($tempRoot,[StringComparison]::OrdinalIgnoreCase) -and (Split-Path $resolved -Leaf) -match '^AwarenessOffsets-[0-9a-f]{32}$') {
        Remove-Item -LiteralPath $resolved -Recurse -Force
    }
}
Write-Output 'Updated Offsets refreshed. Run Build.cmd to rebuild and deploy to your Desktop.'

param([string]$TestDirectory)
$ErrorActionPreference = 'Stop'
$projectPath = Split-Path $PSScriptRoot
$casePath = Join-Path $TestDirectory ('offsets-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path (Join-Path $casePath 'scripts'),(Join-Path $casePath 'src') -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $projectPath 'Updated Offsets') -Destination $casePath -Recurse
$script = Join-Path $casePath 'scripts\generate-offsets.ps1'
Copy-Item -LiteralPath (Join-Path $projectPath 'scripts\generate-offsets.ps1') -Destination $script
& $script
& $script -Check
$globalsPath = Join-Path $casePath 'Updated Offsets\offsets.json'
$originalGlobals = [IO.File]::ReadAllText($globalsPath)
$globals = $originalGlobals | ConvertFrom-Json
$globals.'client.dll'.dwViewAngles += 8
$globals | ConvertTo-Json -Depth 50 | Set-Content -LiteralPath $globalsPath
$rejected = $false
try { & $script } catch { $rejected = $true }
if (-not $rejected) { throw 'Conflicting camera offset exports were accepted.' }
[IO.File]::WriteAllText($globalsPath,$originalGlobals)
& $script -Check
$header = Join-Path $casePath 'src\cs2_offsets.hpp'
$layoutPath = Join-Path $casePath 'Updated Offsets\bone-layout.json'
$originalLayout = [IO.File]::ReadAllText($layoutPath)
$layout = $originalLayout | ConvertFrom-Json
$headerHash = (Get-FileHash -LiteralPath $header).Hash
$layout.bones.Head = 8
$layout | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $layoutPath
$rejected = $false
try { & $script } catch { $rejected = $true }
if (-not $rejected -or (Get-FileHash -LiteralPath $header).Hash -cne $headerHash) {
    throw 'Out-of-range skeletal index was accepted or modified the generated header.'
}
$layout = $originalLayout | ConvertFrom-Json
$layout.client_sha256 = 'changed provenance'
$layout | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $layoutPath
$rejected = $false
try { & $script -Check } catch { $rejected = $true }
if (-not $rejected) { throw 'Changed skeletal provenance was accepted without regeneration.' }
[IO.File]::WriteAllText($layoutPath,$originalLayout)
& $script -Check
$renderLayoutPath=Join-Path $casePath 'Updated Offsets\render-layout.json'
$originalRenderLayout=[IO.File]::ReadAllText($renderLayoutPath)
$renderLayout=$originalRenderLayout | ConvertFrom-Json
$renderLayout.PacketStride=104
$renderLayout | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $renderLayoutPath
$rejected=$false
try { & $script -Check } catch { $rejected=$true }
if (-not $rejected) { throw 'Changed scene draw layout was accepted without regeneration.' }
[IO.File]::WriteAllText($renderLayoutPath,$originalRenderLayout)
& $script -Check
$trajectoryLayoutPath=Join-Path $casePath 'Updated Offsets\trajectory-layout.json'
$originalTrajectoryLayout=[IO.File]::ReadAllText($trajectoryLayoutPath)
$trajectoryLayout=$originalTrajectoryLayout | ConvertFrom-Json
$trajectoryLayout.provenance='changed provenance'
$trajectoryLayout | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $trajectoryLayoutPath
$rejected=$false
try { & $script -Check } catch { $rejected=$true }
if(-not $rejected){throw 'Changed trajectory provenance accepted without regeneration.'}
[IO.File]::WriteAllText($trajectoryLayoutPath,$originalTrajectoryLayout)
& $script -Check
$before = (Get-Item -LiteralPath $header).LastWriteTimeUtc
& $script
if ((Get-Item -LiteralPath $header).LastWriteTimeUtc -ne $before) { throw 'Unchanged header was rewritten.' }
[IO.File]::AppendAllText($header,'// stale')
$rejected = $false
try { & $script -Check } catch { $rejected = $true }
if (-not $rejected) { throw 'Stale generated header was accepted.' }
& $script
& $script -Check
$schemaPath = Join-Path $casePath 'Updated Offsets\client_dll.json'
$schema = Get-Content -LiteralPath $schemaPath -Raw | ConvertFrom-Json
$schema.'client.dll'.classes.C_BaseEntity.fields.m_iHealth = @{ offset = 1234 }
$schema | ConvertTo-Json -Depth 50 | Set-Content -LiteralPath $schemaPath
$rejected=$false
try { & $script } catch { $rejected=$true }
if (-not $rejected) { throw 'Conflicting JSON and C++ offsets were accepted.' }
# The next cases intentionally exercise the supported JSON-only input format.
Remove-Item -LiteralPath (Join-Path $casePath 'Updated Offsets\client_dll.hpp')
& $script
& $script -Check
if ([IO.File]::ReadAllText($header) -notmatch 'Health = 0x4D2;') { throw 'Wrapped schema update was not propagated.' }
$external=Join-Path $casePath 'external-json'
New-Item -ItemType Directory -Path $external | Out-Null
foreach ($name in @('offsets.json','client_dll.json','info.json')) {
    Copy-Item -LiteralPath (Join-Path $casePath "Updated Offsets\$name") -Destination $external
}
Copy-Item -LiteralPath (Join-Path $projectPath 'Updated Offsets\client_dll.hpp') -Destination (Join-Path $casePath 'Updated Offsets')
& $script -InputDirectory $external
& $script -Check
if (Test-Path -LiteralPath (Join-Path $casePath 'Updated Offsets\client_dll.hpp')) { throw 'JSON-only import retained a stale optional C++ export.' }
$headerHash = (Get-FileHash -LiteralPath $header).Hash
$schema.'client.dll'.classes.C_BaseEntity.fields.m_iHealth = -1
$schema | ConvertTo-Json -Depth 50 | Set-Content -LiteralPath $schemaPath
$rejected = $false
try { & $script } catch { $rejected = $true }
if (-not $rejected -or (Get-FileHash -LiteralPath $header).Hash -cne $headerHash) {
    throw 'Invalid snapshot was accepted or modified the generated header.'
}
Write-Output 'Offset generation, verification, no-op, schema update, and invalid-input checks passed.'

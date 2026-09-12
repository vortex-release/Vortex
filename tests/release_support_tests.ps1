param([Parameter(Mandatory)][string]$TestDirectory)
$ErrorActionPreference='Stop'
# CTest can launch Windows PowerShell with a sanitized module search path.
Import-Module (Join-Path $PSHOME 'Modules\Microsoft.PowerShell.Utility\Microsoft.PowerShell.Utility.psd1') -ErrorAction Stop
. (Join-Path (Split-Path $PSScriptRoot) 'scripts\release-support.ps1')
function Reject([scriptblock]$Action) { $rejected=$false; try { & $Action } catch { $rejected=$true }; if (-not $rejected) { throw 'Expected release validation to reject input.' } }
foreach ($version in @('0.1.0','1.0.10','3.23.1','3.24.0','65535.65535.65535')) { Assert-ReleaseVersion $version }
foreach ($version in @('v1.2.3','1.2','01.2.3','1.2.3-beta','1.2.3+meta','65536.0.0','-1.2.3','1.2.3.4')) { Reject { Assert-ReleaseVersion $version } }
$root=Join-Path $TestDirectory ([Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $root -Force | Out-Null
[IO.File]::WriteAllText((Join-Path $root 'VortexSetup.exe'),'fixture installer')
[IO.File]::WriteAllText((Join-Path $root 'Joshu.Vortex-1.0.0-full.nupkg'),'fixture package')
[IO.File]::WriteAllText((Join-Path $root 'releases.win.json'),'{"Assets":[{"Type":"Full","Version":"1.0.0","PackageId":"Joshu.Vortex"}]}')
$lines=foreach($name in @('VortexSetup.exe','Joshu.Vortex-1.0.0-full.nupkg','releases.win.json')) { (Get-FileHash -LiteralPath (Join-Path $root $name)).Hash.ToLowerInvariant()+'  '+$name }
[IO.File]::WriteAllText((Join-Path $root 'SHA256SUMS.txt'),($lines -join "`n")+"`n")
Assert-ReleaseArtifacts $root '1.0.0' 'Joshu.Vortex'
Reject { Assert-ReleaseArtifacts $root '1.0.1' 'Joshu.Vortex' }
[IO.File]::AppendAllText((Join-Path $root 'VortexSetup.exe'),'tampered')
Reject { Assert-ReleaseArtifacts $root '1.0.0' 'Joshu.Vortex' }
Write-Output 'Release version and artifact publication gates passed.'

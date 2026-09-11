param([string]$TestDirectory, [switch]$Child, [string]$Value, [string]$OutputFile)
$ErrorActionPreference = 'Stop'
if ($Child) { [IO.File]::WriteAllText($OutputFile,$Value); exit 0 }
. (Join-Path (Split-Path $PSScriptRoot) 'scripts\build-support.ps1')
$casePath = Join-Path $TestDirectory ('build-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $casePath -Force | Out-Null
$sourcePath = Join-Path $casePath 'source'
if (Test-MovedBuildCache $casePath $sourcePath) { throw 'Absent cache should not require reset.' }
$cache = Join-Path $casePath 'CMakeCache.txt'
[IO.File]::WriteAllLines($cache,@("CMAKE_HOME_DIRECTORY:INTERNAL=$sourcePath","CMAKE_CACHEFILE_DIR:INTERNAL=$casePath"))
if (Test-MovedBuildCache $casePath $sourcePath) { throw 'Matching cache should not require reset.' }
if (-not (Test-MovedBuildCache $casePath ($sourcePath + '-moved'))) { throw 'Moved source was not detected.' }
[IO.File]::WriteAllLines($cache,@("CMAKE_HOME_DIRECTORY:INTERNAL=$sourcePath","CMAKE_CACHEFILE_DIR:INTERNAL=$casePath-old"))
if (-not (Test-MovedBuildCache $casePath $sourcePath)) { throw 'Moved build directory was not detected.' }
$expected = 'path with spaces\end\ "quotes" $() `backtick\'
$output = Join-Path $casePath 'argument.txt'
$shell = (Get-Process -Id $PID).Path
Invoke-BuildTool $shell @('-NoProfile','-NonInteractive','-ExecutionPolicy','Bypass','-File',$PSCommandPath,'-Child','-Value',$expected,'-OutputFile',$output)
if ([IO.File]::ReadAllText($output) -cne $expected) { throw 'Native argument quoting changed the supplied text.' }
$failed = $false
try { Invoke-BuildTool $shell @('-NoProfile','-NonInteractive','-Command','exit 7') } catch { $failed = $true }
if (-not $failed) { throw 'Nonzero child exit code was ignored.' }
$project = Join-Path $casePath 'Example.vcxproj'
[IO.File]::WriteAllText($project,'fixture')
$oldLogs = Join-Path $casePath 'Example.dir\Release\old.tlog'
$newLogs = Join-Path $casePath 'Example.dir\Release\current.tlog'
New-Item -ItemType Directory -Path $oldLogs,$newLogs -Force | Out-Null
$oldMarker = Join-Path $oldLogs 'Example.lastbuildstate'
$newMarker = Join-Path $newLogs 'Example.lastbuildstate'
[IO.File]::WriteAllLines($oldMarker,@('toolset',"Release|x64|$casePath-old\|"))
[IO.File]::WriteAllLines($newMarker,@('toolset',"Release|x64|$casePath\|"))
Remove-StaleBuildMarkers $casePath
if ((Test-Path -LiteralPath $oldMarker) -or -not (Test-Path -LiteralPath $newMarker)) {
    throw 'Relocation cleanup must remove only obsolete build markers.'
}
Write-Output '7 build-support checks passed.'

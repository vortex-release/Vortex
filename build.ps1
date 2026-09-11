param(
    [ValidateSet('Debug','Release')][string]$Configuration = 'Release',
    [switch]$SkipTests,
    [string]$BuildDirectory = (Join-Path $env:LOCALAPPDATA 'EntityAwarenessOverlay\Build'),
    [string]$OffsetDirectory = (Join-Path $PSScriptRoot 'Updated Offsets'),
    [string]$OutputDirectory,
    [switch]$NoDeploy,
    [string]$DependencyDirectory,
    [switch]$Fresh,
    [string[]]$CMakeArguments = @()
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot 'scripts\build-support.ps1')
if (-not $OutputDirectory) { $OutputDirectory = Join-Path (Get-DesktopDirectory) 'CS2-Observer-Overlay' }
$cmakeCommand = Get-Command cmake -ErrorAction SilentlyContinue
if ($cmakeCommand) {
    $cmakePath = $cmakeCommand.Source
} else {
    $vswherePath = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path -LiteralPath $vswherePath)) { throw 'Install Visual Studio 2022 C++ Build Tools and CMake.' }
    $installPath = & $vswherePath -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $installPath) { throw 'Visual Studio C++ Build Tools were not found.' }
    $cmakePath = Join-Path $installPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
}
if (-not (Test-Path -LiteralPath $cmakePath)) { throw "CMake was not found at $cmakePath" }
$buildPath = if ([IO.Path]::IsPathRooted($BuildDirectory)) {
    [IO.Path]::GetFullPath($BuildDirectory)
} else { [IO.Path]::GetFullPath((Join-Path $PSScriptRoot $BuildDirectory)) }
if ($buildPath.TrimEnd('\','/') -eq $PSScriptRoot.TrimEnd('\','/')) {
    throw 'Choose a separate build directory; in-source builds are not supported.'
}
# Regenerate the complete snapshot before compiling, including schema fields.
& (Join-Path $PSScriptRoot 'scripts\generate-offsets.ps1') -InputDirectory $OffsetDirectory
$configure = @('-S',$PSScriptRoot,'-B',$buildPath,'-G','Visual Studio 17 2022','-A','x64')
if ($Fresh -or (Test-MovedBuildCache $buildPath $PSScriptRoot)) {
    Write-Output 'Regenerating the CMake cache for this project location (dependency sources are retained).'
    $configure += '--fresh'
}
# Reuse existing dependency sources when rebuilding a copied project offline.
if (-not $DependencyDirectory) {
    $DependencyDirectory = if (Test-Path -LiteralPath (Join-Path $PSScriptRoot 'dependencies')) {
        Join-Path $PSScriptRoot 'dependencies'
    } else { Join-Path $buildPath '_deps' }
}
foreach ($dependency in @('imgui','minhook')) {
    $dependencyPath = Join-Path $DependencyDirectory ($dependency + '-src')
    $marker = if ($dependency -eq 'imgui') { 'imgui.h' } else { 'include\MinHook.h' }
    if (Test-Path -LiteralPath (Join-Path $dependencyPath $marker)) {
        $configure += '-DFETCHCONTENT_SOURCE_DIR_' + $dependency.ToUpperInvariant() + '=' + [IO.Path]::GetFullPath($dependencyPath)
    }
}
$configure += $CMakeArguments
Invoke-BuildTool $cmakePath $configure
Remove-StaleBuildMarkers $buildPath
Invoke-BuildTool $cmakePath @('--build',$buildPath,'--config',$Configuration,'--parallel')
if (-not $SkipTests) {
    $ctestPath = Join-Path (Split-Path $cmakePath) 'ctest.exe'
    Invoke-BuildTool $ctestPath @('--test-dir',$buildPath,'-C',$Configuration,'--output-on-failure','--no-tests=error')
}
Write-Output "Built DLL and demo: $buildPath\$Configuration"
if (-not $NoDeploy -and -not $SkipTests -and $Configuration -eq 'Release') {
    & (Join-Path $PSScriptRoot 'scripts\deploy-desktop.ps1') -BuildDirectory $buildPath -Destination $OutputDirectory
}

function Get-DesktopDirectory {
    $known = [Environment]::GetFolderPath('DesktopDirectory')
    if ($known) { return $known }
    $registered = Get-ItemPropertyValue -LiteralPath 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Explorer\User Shell Folders' -Name Desktop -ErrorAction SilentlyContinue
    if ($registered) {
        $registered = [Environment]::ExpandEnvironmentVariables($registered)
        if (Test-Path -LiteralPath $registered -PathType Container) { return $registered }
    }
    if ($env:OneDrive) {
        $oneDriveDesktop = Join-Path $env:OneDrive 'Desktop'
        if (Test-Path -LiteralPath $oneDriveDesktop -PathType Container) { return $oneDriveDesktop }
    }
    return Join-Path ([Environment]::GetFolderPath('UserProfile')) 'Desktop'
}

function Invoke-BuildTool([string]$Program, [string[]]$ToolArguments) {
    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = $Program
    $start.UseShellExecute = $false
    # Quote argv for Windows PowerShell 5.1 as well as PowerShell 7. No shell is used.
    $quoted = foreach ($argument in $ToolArguments) {
        '"' + [regex]::Replace([regex]::Replace($argument, '(\\*)"', '$1$1\"'), '(\\+)$', '$1$1') + '"'
    }
    $start.Arguments = $quoted -join ' '
    # Some launchers provide both Path and PATH. MSBuild's case-insensitive
    # environment dictionary throws MSB6001 before it can launch the compiler.
    $cleanEnvironment = @{}
    foreach ($entry in [Environment]::GetEnvironmentVariables().GetEnumerator()) {
        $cleanEnvironment[$entry.Key] = $entry.Value
    }
    $start.EnvironmentVariables.Clear()
    foreach ($key in $cleanEnvironment.Keys) { $start.EnvironmentVariables[$key] = $cleanEnvironment[$key] }
    # PowerShell 5.1 and 7 use different built-in module locations. Let test
    # subprocesses initialize their own defaults instead of inheriting the host's.
    $start.EnvironmentVariables.Remove('PSModulePath')
    $process = [Diagnostics.Process]::Start($start)
    try {
        $process.WaitForExit()
        if ($process.ExitCode -ne 0) {
            throw "$Program failed with exit code $($process.ExitCode). See the diagnostic output above."
        }
    } finally { $process.Dispose() }
}

function Test-MovedBuildCache([string]$BuildPath, [string]$SourcePath) {
    $cachePath = Join-Path $BuildPath 'CMakeCache.txt'
    if (-not (Test-Path -LiteralPath $cachePath)) { return $false }
    $expected = @{
        CMAKE_HOME_DIRECTORY = [IO.Path]::GetFullPath($SourcePath).Replace('\','/').TrimEnd('/')
        CMAKE_CACHEFILE_DIR = [IO.Path]::GetFullPath($BuildPath).Replace('\','/').TrimEnd('/')
    }
    foreach ($line in [IO.File]::ReadAllLines($cachePath)) {
        if ($line -match '^(CMAKE_HOME_DIRECTORY|CMAKE_CACHEFILE_DIR):INTERNAL=(.*)$') {
            if ($Matches[2].Replace('\','/').TrimEnd('/') -ne $expected[$Matches[1]]) { return $true }
        }
    }
    return $false
}

function Remove-StaleBuildMarkers([string]$BuildPath) {
    $resolvedBuild = [IO.Path]::GetFullPath($BuildPath).TrimEnd('\','/')
    $removed = 0
    foreach ($project in Get-ChildItem -LiteralPath $resolvedBuild -Filter '*.vcxproj' -File) {
        $intermediate = Join-Path $resolvedBuild ($project.BaseName + '.dir')
        if (-not (Test-Path -LiteralPath $intermediate)) { continue }
        # Remove only generated lastbuildstate files that identify another build
        # location. Leave the current build markers, outputs, and sources intact.
        foreach ($marker in Get-ChildItem -LiteralPath $intermediate -Filter '*.lastbuildstate' -Recurse -File) {
            if ($marker.BaseName -ne $project.BaseName -or $marker.Directory.Name -notlike '*.tlog') { continue }
            $lines = [IO.File]::ReadAllLines($marker.FullName)
            if ($lines.Count -lt 2 -or $lines[1] -notmatch '^[^|]+\|[^|]+\|([^|]+)\|$') { continue }
            if ($Matches[1].Replace('/','\').TrimEnd('\') -ne $resolvedBuild.Replace('/','\')) {
                Remove-Item -LiteralPath $marker.FullName
                ++$removed
            }
        }
    }
    if ($removed) { Write-Output "Removed $removed obsolete MSBuild location markers." }
}


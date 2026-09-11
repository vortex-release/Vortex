param(
    [Parameter(Mandatory,Position=0)][string]$Version,
    [switch]$ValidateOnly,
    [string]$BuildDirectory=(Join-Path $env:LOCALAPPDATA 'Vortex\Build')
)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'scripts\release-support.ps1')
Assert-ReleaseVersion $Version
foreach ($name in @('git','dotnet')) { if (-not (Get-Command $name -ErrorAction SilentlyContinue)) { throw "Required command missing: $name" } }
$gh=Find-GitHubCli
Push-Location $PSScriptRoot
$configPath=Join-Path $PSScriptRoot 'release\config.json'
$original=[IO.File]::ReadAllText($configPath)
$published=$false
try {
    Assert-ReleaseRepository $PSScriptRoot
    & $gh auth status
    if ($LASTEXITCODE -ne 0) { throw 'GitHub authorization required: gh auth login --hostname github.com --git-protocol https --web' }
    $tag="v$Version"
    $localTags=@(Invoke-ReleaseGit @('tag','--list',$tag))
    $remoteTags=@(Invoke-ReleaseGit @('ls-remote','--tags','origin',"refs/tags/$tag","refs/tags/$tag^{}"))
    if ($localTags.Count -or $remoteTags.Count) { throw "$tag already exists. Tags and releases are never overwritten." }
    # Query the release collection: an API failure must not be interpreted as an absent release.
    $releases=& $gh api --paginate 'repos/vortex-release/Vortex/releases' --jq '.[].tag_name'
    if ($LASTEXITCODE -ne 0) { throw 'Could not verify existing GitHub releases.' }
    if (@($releases) -contains $tag) { throw "$tag already has a release." }
    $config=$original | ConvertFrom-Json
    if ([version]$Version -lt [version]$config.version) { throw 'Release version cannot move backwards.' }
    foreach ($existing in @($releases)) {
        if ($existing -match '^v(\d+\.\d+\.\d+)$' -and [version]$Version -le [version]$Matches[1]) { throw 'Choose a version newer than every published stable release.' }
    }
    $config.version=$Version
    [IO.File]::WriteAllText($configPath,($config | ConvertTo-Json)+"`n",[Text.UTF8Encoding]::new($false))
    Assert-ReleaseFiles
    & (Join-Path $PSScriptRoot 'scripts\setup-release-tools.ps1')
    $artifacts=Join-Path $env:LOCALAPPDATA ('Vortex\release-validation\'+$tag+'-'+[Guid]::NewGuid().ToString('N'))
    & (Join-Path $PSScriptRoot 'scripts\package-vortex.ps1') -BuildDirectory $BuildDirectory -ReleaseDirectory $artifacts -NoDesktopCopy
    Assert-ReleaseArtifacts $artifacts $Version $config.id
    Write-Output "Local release verified: $artifacts"
    if ($ValidateOnly) { Write-Output 'Validation only: no commit, tag or push.'; return }
    Assert-ReleaseFiles
    Invoke-ReleaseGit @('add','--all') | Out-Host
    Invoke-ReleaseGit @('commit','--allow-empty','-m',"Release $tag") | Out-Host
    Invoke-ReleaseGit @('tag','-a',$tag,'-m',"Vortex $Version") | Out-Host
    # Atomic push publishes the branch and tag together or changes neither.
    Invoke-ReleaseGit @('push','--atomic','-u','origin','main',"refs/tags/$tag") | Out-Host
    $published=$true
    $remote=@(Invoke-ReleaseGit @('ls-remote','--tags','origin',"refs/tags/$tag"))
    if (-not $remote.Count) { throw 'Push finished but the release tag could not be confirmed.' }
    Write-Output "Release tag accepted: https://github.com/vortex-release/Vortex/actions"
    & $gh run list --repo vortex-release/Vortex --workflow release.yml --limit 3 --json status,url,headBranch
    if ($LASTEXITCODE -ne 0) { Write-Warning 'Tag was pushed. Check the Actions page for workflow startup.' }
} finally {
    if ($ValidateOnly) { [IO.File]::WriteAllText($configPath,$original,[Text.UTF8Encoding]::new($false)) }
    Pop-Location
}

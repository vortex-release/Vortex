Set-StrictMode -Version Latest
function Assert-ReleaseVersion([string]$Version) {
    if ($Version -notmatch '^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$') { throw 'Use MAJOR.MINOR.PATCH without a v prefix or leading zeroes.' }
    foreach ($part in $Version.Split('.')) { if ($part.Length -gt 5 -or [int]$part -gt 65535) { throw 'Version components must be 0 through 65535.' } }
}
function Find-GitHubCli {
    $command=Get-Command gh -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }
    $path=Join-Path $env:ProgramFiles 'GitHub CLI\gh.exe'
    if (Test-Path -LiteralPath $path) { return $path }
    throw 'GitHub CLI is required. Install GitHub.cli and authorize gh auth login --web.'
}
function Invoke-ReleaseGit([string[]]$Arguments) {
    $output = & git @Arguments
    if ($LASTEXITCODE -ne 0) { throw "git $($Arguments[0]) failed ($LASTEXITCODE)." }
    return $output
}
function Assert-ReleaseRepository([string]$Project) {
    $root = Invoke-ReleaseGit @('rev-parse','--show-toplevel')
    if ([IO.Path]::GetFullPath($root).TrimEnd('\','/') -ine [IO.Path]::GetFullPath($Project).TrimEnd('\','/')) { throw 'Run this command in the Vortex source repository.' }
    $remote = Invoke-ReleaseGit @('remote','get-url','origin')
    if ($remote -cnotmatch '^https://github\.com/vortex-release/Vortex(?:\.git)?$') { throw 'origin must be https://github.com/vortex-release/Vortex.git.' }
    if ((Invoke-ReleaseGit @('branch','--show-current')) -ne 'main') { throw 'Publish from main.' }
}
function Assert-ReleaseFiles {
    $files = @(Invoke-ReleaseGit @('ls-files','--cached','--others','--exclude-standard')) | Sort-Object -Unique
    $secretPatterns = @('gh[pousr]_[A-Za-z0-9]{30,}', 'github_pat_[A-Za-z0-9_]{30,}', '-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----', 'AKIA[0-9A-Z]{16}')
    foreach ($file in $files) {
        if (-not (Test-Path -LiteralPath $file -PathType Leaf)) { continue }
        if ($file -match '(^|/)(\.env($|\.)|credentials($|\.)|id_rsa$)|\.(pfx|p12|pem|key|bak|tmp|pdb|obj|log)$') { throw "Unpublishable local file: $file" }
        $item = Get-Item -LiteralPath $file
        if ($item.Length -gt 50MB) { throw "Unexpected oversized repository file: $file" }
        if ($item.Extension -in '.cpp','.hpp','.h','.c','.ps1','.md','.txt','.json','.yml','.yaml','.ini','.cmd','') {
            $text = [IO.File]::ReadAllText($item.FullName)
            foreach ($pattern in $secretPatterns) { if ($text -match $pattern) { throw "Potential secret in $file. Refusing to publish." } }
        }
    }
}
function Assert-ReleaseArtifacts([string]$Directory, [string]$Version, [string]$Id) {
    $required=@('VortexSetup.exe','releases.win.json',"$Id-$Version-full.nupkg",'SHA256SUMS.txt')
    foreach ($name in $required) { if ((Get-Item -LiteralPath (Join-Path $Directory $name)).Length -le 0) { throw "Empty release artifact: $name" } }
    $seen=@{}
    foreach ($line in [IO.File]::ReadAllLines((Join-Path $Directory 'SHA256SUMS.txt'))) {
        if ($line -notmatch '^([a-fA-F0-9]{64})  ([A-Za-z0-9._-]+)$') { throw 'Invalid checksum manifest.' }
        $hash=$Matches[1]; $name=$Matches[2]
        if ($seen.ContainsKey($name)) { throw 'Duplicate checksum entry.' }; $seen[$name]=$true
        if ((Get-FileHash -LiteralPath (Join-Path $Directory $name) -Algorithm SHA256).Hash -ine $hash) { throw "Checksum mismatch: $name" }
    }
    foreach ($name in $required | Where-Object { $_ -ne 'SHA256SUMS.txt' }) { if (-not $seen.ContainsKey($name)) { throw "Missing checksum: $name" } }
    $feed=Get-Content -LiteralPath (Join-Path $Directory 'releases.win.json') -Raw | ConvertFrom-Json
    if (@($feed.Assets | Where-Object { $_.Type -eq 'Full' -and $_.Version -eq $Version -and $_.PackageId -eq $Id }).Count -ne 1) { throw 'Release feed does not identify exactly one matching full package.' }
}

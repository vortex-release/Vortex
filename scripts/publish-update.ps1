param([Parameter(Mandatory,Position=0)][string]$Version)
& (Join-Path (Split-Path $PSScriptRoot) 'release.ps1') -Version $Version

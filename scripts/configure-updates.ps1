param([Parameter(Mandatory)][string]$Url)
$ErrorActionPreference = 'Stop'
$uri = [Uri]$Url
if (-not $uri.IsAbsoluteUri -or $uri.Scheme -ne 'https' -or $uri.UserInfo -or $uri.Query -or $uri.Fragment -or $uri.IsLoopback) {
    throw 'Enter your stable public HTTPS address, such as https://updates.yourdomain.com.'
}
$file = Join-Path (Split-Path $PSScriptRoot) 'release\config.json'
$config = Get-Content -LiteralPath $file -Raw | ConvertFrom-Json
$config.updateUrl = $Url.TrimEnd('/')
$config | ConvertTo-Json | Set-Content -LiteralPath $file -Encoding UTF8
Write-Output 'Update address saved. Build a new installer before distributing Vortex.'

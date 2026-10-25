# Checks that a Windows release is consistent before it is published: the tag, version.h, the installer script
# and the download links in the README and on the site must all name the same version. Run locally before
# tagging (windows\release-check.ps1 -Tag win-v0.2.11) and by the release job for the pushed tag.
param([Parameter(Mandatory)][string]$Tag)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent

$header = Get-Content (Join-Path $root 'windows/src/app/version.h') -Raw
$part = @{}
foreach ($p in 'MAJOR', 'MINOR', 'PATCH') {
    if ($header -match "IXPAT_VERSION_$p (\d+)") { $part[$p] = $Matches[1] } else { throw "version.h: no IXPAT_VERSION_$p" }
}
$version = "$($part.MAJOR).$($part.MINOR).$($part.PATCH)"
$problems = @()

if ($Tag -ne "win-v$version") { $problems += "tag $Tag does not match version.h ($version)" }

$iss = Get-Content (Join-Path $root 'windows/installer/ixpat.iss') -Raw
if ($iss -notmatch "#define MyAppVersion `"$([regex]::Escape($version))`"") { $problems += "ixpat.iss: MyAppVersion is not $version" }

foreach ($file in 'README.md', 'docs/index.html') {
    $text = Get-Content (Join-Path $root $file) -Raw
    $links = [regex]::Matches($text, 'releases/(?:download|tag)/(win-v[0-9.]+)(?:/([^"\s)]+))?')
    if ($links.Count -eq 0) { $problems += "${file}: no Windows release links" }
    foreach ($m in $links) {
        $linkTag = $m.Groups[1].Value
        if ($linkTag -ne $Tag) { $problems += "${file}: links to $linkTag instead of $Tag" }
        if ($m.Groups[2].Success -and $m.Groups[2].Value -notlike "*$version*") {
            $problems += "${file}: asset $($m.Groups[2].Value) is not a $version file"
        }
    }
}

$problems = @($problems | Select-Object -Unique)
if ($problems.Count -gt 0) {
    foreach ($p in $problems) { Write-Host "::error::$p" }
    exit 1
}
Write-Host "release ${Tag}: version.h, the installer script and the download links agree on $version"

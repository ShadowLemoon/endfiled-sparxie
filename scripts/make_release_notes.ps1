# Fills .github/release-notes.template.md from the packaged plugin (hashes, version) and the git log since the previous tag.
# ASCII-only on purpose (Windows PowerShell 5 reads BOM-less files as ANSI); the template itself is UTF-8.
# Usage: scripts/make_release_notes.ps1 -Tag v0.1.0 [-Dist dist] [-Output dist/RELEASE_NOTES.md]
param(
    [Parameter(Mandatory = $true)] [string]$Tag,
    [string]$Dist = '',
    [string]$Output = ''
)
$ErrorActionPreference = 'Stop'
# git prints UTF-8; without this Windows PowerShell decodes it with the console code page (mojibake for Chinese commit subjects)
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
$root = Split-Path -Parent $PSScriptRoot
if (-not $Dist) { $Dist = Join-Path $root 'dist' }
if (-not $Output) { $Output = Join-Path $Dist 'RELEASE_NOTES.md' }

$manifest = [System.IO.File]::ReadAllText((Join-Path $root 'plugin.json'), [System.Text.Encoding]::UTF8) | ConvertFrom-Json
$folder = Join-Path $Dist $manifest.id
$zip = Join-Path $Dist ($manifest.id + '-' + $manifest.version + '.zip')
foreach ($required in @((Join-Path $folder 'module.dll'), (Join-Path $folder 'plugin.json'), $zip)) {
    if (-not (Test-Path $required)) { throw "missing build output: $required (run scripts/package.ps1 first)" }
}

function Get-Sha([string]$path) { (Get-FileHash $path -Algorithm SHA256).Hash.ToLowerInvariant() }

# commits since the previous tag; for the first release, the whole history
$previous = ''
try { $previous = (& git -C $root describe --tags --abbrev=0 "$Tag^" 2>$null) } catch { $previous = '' }
if ($LASTEXITCODE -ne 0) { $previous = '' }
$range = if ($previous) { "$previous..$Tag" } else { $Tag }
$subjects = @(& git -C $root -c i18n.logOutputEncoding=UTF-8 log $range --no-merges --pretty=format:'%s' 2>$null | Where-Object { $_ })
$changes = if ($subjects.Count -gt 0) { ($subjects | ForEach-Object { '- ' + $_ }) -join "`n" } else { '- (no changes recorded)' }

$text = [System.IO.File]::ReadAllText((Join-Path $root '.github\release-notes.template.md'), [System.Text.Encoding]::UTF8)
$values = @{
    '{{DISPLAY_NAME}}'    = $manifest.displayName
    '{{VERSION}}'         = $manifest.version
    '{{PLUGIN_ID}}'       = $manifest.id
    '{{ZIP_NAME}}'        = (Split-Path -Leaf $zip)
    '{{DLL_SHA256}}'      = (Get-Sha (Join-Path $folder 'module.dll'))
    '{{MANIFEST_SHA256}}' = (Get-Sha (Join-Path $folder 'plugin.json'))
    '{{ZIP_SHA256}}'      = (Get-Sha $zip)
    '{{CHANGES}}'         = $changes
}
foreach ($key in $values.Keys) { $text = $text.Replace($key, [string]$values[$key]) }
if ($text -match '\{\{[A-Z_0-9]+\}\}') { throw "unfilled placeholder in release notes: $($Matches[0])" }
[System.IO.File]::WriteAllText($Output, $text, (New-Object System.Text.UTF8Encoding $false))
Write-Host "Release notes: $Output"

# Static checks of plugin.json (ASCII-only script on purpose: Windows PowerShell 5 reads BOM-less files as ANSI).
# The host runs the full validation (ids, exe names, caps, settingsSchema); this catches the common mistakes before packaging.
param([string]$Root = (Split-Path -Parent $PSScriptRoot))
$ErrorActionPreference = 'Stop'
$manifestPath = Join-Path $Root 'plugin.json'
$manifest = Get-Content -Raw -Encoding UTF8 $manifestPath | ConvertFrom-Json
$errors = New-Object System.Collections.Generic.List[string]

if ($manifest.id -notmatch '^ext\.[a-z0-9-]{2,32}\.[a-z0-9-]{2,32}$') { $errors.Add("id is not a valid third-party id: $($manifest.id)") }
if ($manifest.abiMajor -ne 1) { $errors.Add('abiMajor must be 1') }
if (-not $manifest.exeNames -or $manifest.exeNames.Count -lt 1 -or $manifest.exeNames.Count -gt 8) { $errors.Add('exeNames must have 1-8 entries') }
foreach ($exe in $manifest.exeNames) { if ($exe -notmatch '^[^\\/:]+\.exe$') { $errors.Add("exeNames entry is not a plain exe name: $exe") } }
$known = 'createsProcess', 'injects', 'needsArgs', 'hotParams'
foreach ($cap in $manifest.caps) { if ($known -notcontains $cap) { $errors.Add("unknown cap: $cap") } }

# the version in plugin.json must equal the one the module reports (module/spx_module.h)
$header = Get-Content -Raw -Encoding UTF8 (Join-Path $Root 'module\spx_module.h')
if ($header -match 'kPluginVersion = L"([^"]+)"') {
  if ($Matches[1] -ne $manifest.version) { $errors.Add("version mismatch: plugin.json=$($manifest.version) module=$($Matches[1])") }
} else { $errors.Add('kPluginVersion not found in module/spx_module.h') }
if ($header -match 'kPluginId = L"([^"]+)"') {
  if ($Matches[1] -ne $manifest.id) { $errors.Add("id mismatch: plugin.json=$($manifest.id) module=$($Matches[1])") }
}

# settingsSchema: unique keys, showWhen refers to an earlier field, defaults in range
$seen = @{}
foreach ($f in $manifest.settingsSchema.fields) {
  if ($f.key -notmatch '^[A-Za-z][A-Za-z0-9_]{0,39}$') { $errors.Add("bad field key: $($f.key)") }
  if ($seen.ContainsKey($f.key)) { $errors.Add("duplicate field key: $($f.key)") }
  if ($f.showWhen -and -not $seen.ContainsKey($f.showWhen.key)) { $errors.Add("showWhen of $($f.key) must refer to an earlier field") }
  if ($f.type -eq 'int' -and ($f.default -lt $f.min -or $f.default -gt $f.max)) { $errors.Add("default of $($f.key) is out of range") }
  if ($f.type -eq 'enum' -and -not ($f.options | Where-Object { $_.value -eq $f.default })) { $errors.Add("default of $($f.key) is not an option") }
  $seen[$f.key] = $true
}
if ($seen.Count -gt 64) { $errors.Add('more than 64 settings fields') }

if ($errors.Count -gt 0) { $errors | ForEach-Object { Write-Error $_ -ErrorAction Continue }; exit 1 }
Write-Host "plugin.json OK: $($manifest.id) $($manifest.version), $($seen.Count) settings fields"
exit 0

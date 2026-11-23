# Builds Release and packs the plugin folder Sparxie expects: <DataRoot>\plugins\ext.<vendor>.<game>\{module.dll, plugin.json, ...}
# ASCII-only script on purpose (Windows PowerShell 5 reads BOM-less files as ANSI).
param([string]$Version = '', [string]$Output = '', [string]$Generator = '')
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
if (-not $Output) { $Output = Join-Path $root 'dist' }

& (Join-Path $PSScriptRoot 'verify_manifest.ps1') -Root $root
if ($LASTEXITCODE -ne 0) { throw 'plugin.json verification failed' }
$manifest = Get-Content -Raw -Encoding UTF8 (Join-Path $root 'plugin.json') | ConvertFrom-Json
if (-not $Version) { $Version = $manifest.version }
if ($Version -ne $manifest.version) { throw "Version $Version does not match plugin.json ($($manifest.version))" }

# Use the CMake bundled with the newest Visual Studio (falls back to cmake/ctest on PATH, e.g. on CI runners)
# and the matching generator; -Generator overrides the detection.
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$cmake = 'cmake'
$ctest = 'ctest'
$generatorName = $Generator
if (Test-Path $vswhere) {
  $vs = (& $vswhere -latest -products '*' -property installationPath | Select-Object -First 1)
  $vsVersion = (& $vswhere -latest -products '*' -property installationVersion | Select-Object -First 1)
  $bundled = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin'
  if (Test-Path (Join-Path $bundled 'cmake.exe')) {
    $cmake = Join-Path $bundled 'cmake.exe'
    $ctest = Join-Path $bundled 'ctest.exe'
  }
  if (-not $generatorName -and $vsVersion) {
    $known = @{ '17' = 'Visual Studio 17 2022'; '18' = 'Visual Studio 18 2026' }
    $major = ($vsVersion -split '\.')[0]
    if ($known.ContainsKey($major)) { $generatorName = $known[$major] }
  }
}
$generatorArgs = @()
if ($generatorName) { $generatorArgs = @('-G', $generatorName) }
$build = Join-Path $root 'build'
& $cmake -S $root -B $build @generatorArgs -A x64 | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'cmake configure failed' }
& $cmake --build $build --config Release | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'cmake build failed' }
& $ctest --test-dir $build -C Release --output-on-failure | Select-Object -Last 6
if ($LASTEXITCODE -ne 0) { throw 'native tests failed; refusing to package' }

$folder = Join-Path $Output $manifest.id
if (Test-Path $folder) { Remove-Item $folder -Recurse -Force }
New-Item -ItemType Directory -Force $folder | Out-Null
Copy-Item (Join-Path $build 'bin\Release\module.dll') $folder
# Text files are normalized to LF so the packaged bytes (and plugin.json's SHA-256 that the host asks the user to confirm)
# do not depend on git's core.autocrlf checkout setting.
foreach ($f in 'plugin.json', 'NOTICE.md', 'README.md') {
  $text = [System.IO.File]::ReadAllText((Join-Path $root $f)).Replace("`r`n", "`n")
  [System.IO.File]::WriteAllText((Join-Path $folder $f), $text, (New-Object System.Text.UTF8Encoding $false))
}
Copy-Item (Join-Path $root 'icon.ico') $folder

$zip = Join-Path $Output ($manifest.id + '-' + $Version + '.zip')
if (Test-Path $zip) { Remove-Item $zip -Force }
Compress-Archive -Path $folder -DestinationPath $zip -CompressionLevel Optimal

$lines = New-Object System.Collections.Generic.List[string]
foreach ($file in Get-ChildItem $folder -File) {
  $lines.Add(((Get-FileHash $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant() + '  ' + $manifest.id + '\' + $file.Name))
}
$lines.Add(((Get-FileHash $zip -Algorithm SHA256).Hash.ToLowerInvariant() + '  ' + (Split-Path -Leaf $zip)))
$sums = Join-Path $Output ($manifest.id + '-' + $Version + '.sha256.txt')
Set-Content -Path $sums -Value $lines -Encoding ASCII
Write-Host "Package: $zip"
Get-Content $sums

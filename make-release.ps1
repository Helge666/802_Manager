# Packs the Release build of 802 Manager into a zip for a GitHub release.
#   powershell -ExecutionPolicy Bypass -File ".\make-release.ps1" -Version v1.2.0
# Result: dist\802_Manager-<Version>-Windows-x64.zip

param([Parameter(Mandatory)][string]$Version)

$ErrorActionPreference = 'Stop'

$name = "802_Manager-$Version-Windows-x64"
$dist = Join-Path $PSScriptRoot 'dist'
$stage = Join-Path $dist $name

if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
New-Item -ItemType Directory -Force $stage | Out-Null

Copy-Item (Join-Path $PSScriptRoot 'build\802_Manager_App_artefacts\Release\802_Manager.exe') $stage
Copy-Item (Join-Path $PSScriptRoot 'data\dx_preset_library.sqlite3') $stage
Copy-Item (Join-Path $PSScriptRoot 'docs\release\INSTALL.txt') $stage
Copy-Item (Join-Path $PSScriptRoot 'LICENSE') (Join-Path $stage 'LICENSE.txt')

$zip = Join-Path $dist "$name.zip"
if (Test-Path $zip) { Remove-Item -Force $zip }
Compress-Archive -Path $stage -DestinationPath $zip
Write-Host "Packed $zip" -ForegroundColor Green

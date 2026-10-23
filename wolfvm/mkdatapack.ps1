# mkdatapack.ps1 — pack a local decrypted game Data tree into the Android
# shell's asset slot (wolfvm_data.zip), producing a PERSONAL TEST build.
#
# The pack is third-party game content (copyrighted): it is gitignored, stays
# on this machine, and must NOT be committed, pushed, or uploaded to a GitHub
# release. Code-only builds (no pack asset) remain the public deliverable —
# those read the game data from device storage instead.
#
# Usage:
#   powershell -File tools\mkdatapack.ps1 [-DataRoot <path-to-decrypted-Data-dir>]
# Default DataRoot is the restore location used on this machine.

param(
    [string]$DataRoot = "$env:USERPROFILE\wolfvm-data\game\Data"
)
$ErrorActionPreference = "Stop"

$pack = Join-Path $PSScriptRoot "..\wolfvm\android\app\src\main\assets\wolfvm_data.zip"
New-Item -ItemType Directory -Force (Split-Path -Parent $pack) | Out-Null

if (-not (Test-Path (Join-Path $DataRoot "BasicData\Game.dat"))) {
    Write-Error "not a decrypted Data tree: $DataRoot (BasicData\Game.dat missing)"
}
Write-Host "== mkdatapack =="
Write-Host "[1/3] source   : $DataRoot"
Write-Host "[2/3] pack     : $pack"

$parent = Split-Path -Parent $DataRoot
$seven = Get-Command 7z -ErrorAction SilentlyContinue
if ($seven) {
    Write-Host "      packing via 7z ..."
    Push-Location $parent
    & 7z a -tzip -mx=5 "$pack" Data | Out-Null
    $ok = ($LASTEXITCODE -eq 0)
    Pop-Location
    if (-not $ok) { Write-Error "7z pack failed (exit $LASTEXITCODE)" }
} else {
    Write-Host "      packing via Compress-Archive (slower; install 7z for speed) ..."
    Compress-Archive -Path $DataRoot -DestinationPath $pack -Force
}

Write-Host "[3/3] verify   :"
Add-Type -AssemblyName System.IO.Compression.FileSystem
$zip = [IO.Compression.ZipFile]::OpenRead($pack)
try {
    $entries = $zip.Entries.Count
    $bytes = ($zip.Entries | Measure-Object Length -Sum).Sum
    $names = @("Data/BasicData/Game.dat", "Data\BasicData\Game.dat")
    $has = $names | Where-Object { $zip.Entries.FullName.Contains($_) }
    Write-Host "      entries : $entries"
    Write-Host ("      raw size: {0:N0} bytes" -f $bytes)
    if ($has) {
        Write-Host "      Game.dat: present"
        Write-Host ""
        Write-Host "rebuild the test APK:"
        Write-Host "  cd wolfvm\android; .\gradlew assembleDebug --no-daemon"
        Write-Host "first app launch auto-imports the pack (in-app zip import pipeline)."
    } else {
        Write-Error "pack lacks Data/BasicData/Game.dat (wrong structure for the app's extractor)"
    }
} finally { $zip.Dispose() }

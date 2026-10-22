# restore-data.ps1 — re-materialize the Eris Dysnomia game data on a fresh session.
# Encodes the onboarding steps from README.md / ENGINE_ANALYSIS.md as one command.
#
# Usage:
#   powershell -File wolfvm\restore-data.ps1
#     -Archive   path to Eris_Dysnomia_v1.02.7z   (default: Downloads)
#     -UberWolf  path to UberWolfCli.exe          (optional: skips decrypt if absent)
#
# Steps: extract -> locate data root -> (decrypt .wolf archives) -> verify dat
# layer via wolfvm -> sweep sanity probe id 0. Safe to re-run (idempotent steps).

param(
    [string]$Archive  = "",
    [string]$UberWolf = "",
    [string]$SevenZip = "7z"
)
$ErrorActionPreference = "Stop"
$project = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)   # .../Default Project
$extractInto = $project
if (-not $Archive)  { $Archive  = "$HOME\Downloads\Eris_Dysnomia_v1.02.7z" }
if (-not $UberWolf) { $UberWolf = Join-Path $project "tools\UberWolfCli.exe" }

Write-Host "== restore-data =="
if (-not (Test-Path $Archive)) {
    Write-Error "archive not found: $Archive  (re-download Eris_Dysnomia_v1.02.7z)"
}
Write-Host "[1/5] extract $($Archive)"
& $SevenZip x -y "-o$extractInto" $Archive | Out-Null
if ($LASTEXITCODE -ne 0) { Write-Error "extraction failed (7z exit $LASTEXITCODE)" }

# Locate the game root (archive layout may or may not carry a top folder).
Write-Host "[2/5] locate Game.exe / Data tree"
$game = Get-ChildItem $extractInto -Recurse -Filter "Game.exe" -Depth 3 |
        Select-Object -First 1
if (-not $game) { Write-Error "Game.exe not found after extraction" }
$gameroot = Split-Path -Parent $game.FullName
Write-Host "      gameroot = $gameroot"
$data = Join-Path $gameroot "Data"
if (-not (Test-Path (Join-Path $data "BasicData\Game.dat"))) {
    Write-Error "Data\BasicData\Game.dat missing under $gameroot"
}

if ($UberWolf -and (Test-Path $UberWolf)) {
    Write-Host "[3/5] decrypt .wolf archives (UberWolfCli; keys auto-detect from Game.exe)"
    # Verified usage (v0.6.4 release asset, CLI11): positional FILEs =
    # <Game[Pro].exe> <data_folder> — unpacks every .wolf in the data folder;
    # -o makes re-runs idempotent against already-unpacked folders.
    & $UberWolf -o (Join-Path $gameroot "Game.exe") $data
    if ($LASTEXITCODE -ne 0) { Write-Error "decrypt failed (exit $LASTEXITCODE)" }
} else {
    Write-Host "[3/5] SKIP decrypt (no shiny tool: run with -UberWolf path\UberWolfCli.exe;"
    Write-Host "      official asset: github.com/Sinflower/UberWolf/releases, tag v0.6.4)"
}

Write-Host "[4/5] dat-layer verification via wolfvm"
$vm = Join-Path $PSScriptRoot "build\wolfvm.exe"
if (-not (Test-Path $vm)) { Write-Error "build wolfvm first: cmake --build wolfvm\build" }
& $vm selftest;  if ($LASTEXITCODE -ne 0) { Write-Error "selftest regressed" }
& $vm datinfo (Join-Path $data "BasicData\Game.dat")
& $vm commoninfo (Join-Path $data "BasicData\CommonEvent.dat")
& $vm maptree (Join-Path $data "BasicData\MapTree.dat")

Write-Host "[5/5] sweep sanity probe (id 0)"
& $vm sweep (Join-Path $data "BasicData\CommonEvent.dat") --from 0 --to 0
Write-Host ""
Write-Host "== full sweep when ready (= the v6 re-baseline) =="
Write-Host "& $vm sweep `"$data\BasicData\CommonEvent.dat`" ``"
Write-Host "  --db `"$data\BasicData\DataBase.project`" `"$data\BasicData\DataBase.dat`" ``"
Write-Host "  --cdb `"$data\BasicData\CDataBase.project`" `"$data\BasicData\CDataBase.dat`""

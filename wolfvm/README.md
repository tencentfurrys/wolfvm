# wolfvm — native WOLF RPG Editor 2.2x VM (Android target)

A clean-room reimplementation of the WOLF RPG Editor runtime ("RPG Drive") needed to run
Eris Dysnomia on Android. Built from the engine analysis in `../ENGINE_ANALYSIS.md`.

**Targets of record:** Windows x64 (dev loop) → Android arm64 (NDK, final APK shell).

## Status: Stage 1 (format layer) — in progress

- [x] Stage 0 — data unlocked: all 13 `.wolf` archives decrypted to plain files/folders
      (tool: UberWolfCli v0.6.4, keys auto-detected from shipped `Game.exe`; data now plaintext per-file).
- [ ] Stage 1 — dat layer parsers (`WOLF` magic, string/blob fields, common-event table, DB structures, MapTree, Game.dat config).
  - [x] `filecoder` primitives — byte/i32/string (u32-len-incl-NUL)/arrays + interleave-OBF undo; `trailing_size` self-check passed on Game.dat
  - [x] `Game.dat` ('M' config) — full-consumption verified on shipped file
  - [x] `CommonEvent.dat` ('C') — 584 events, command streams (cid/args/strings/indent, Move+route tails), tagged tails; full consumption (1,826,518 B)
  - [x] `MapTree.dat` — 56-node (parent,id) adjacency tree, no strings; zero-head + 0x90 marker + 0xBC terminator; full consumption
  - [x] `MapTreeOpenStatus.dat` — 56 × u8 expanded/opening flags, full consumption; open set == folder-bearing tree nodes
  - [x] `DataBase.dat`/`.project`, `CDataBase*`, `SysDatabase*` — .project type definitions + .dat typed value grids; trio verified: 100/57/28 types, 2,135/1,387/47 fields, 4,756/2,016/1,051 rows, 128,687/46,939/2,249 cells; per-row cells are ints-first grouped (reference semantics, confirmed byte-identical); full consumption on both files of each pair
  - [x] `SysDataBaseBasic.dat` — factory-reset system DB: `[00×10][0xC0 marker][i32 typeCount=12][12 × [i32 F][F × i32 indexinfo][i32 rowCapacity][R × row]][0xC0]`; rows ints-first; indexinfo ≥ 0x07D0 = string field; full consumption (438 B); 34 int + 19 string cells, capacity-only placeholder types included; no crypt (plaintext despite zero-byte lead)
  - [x] `TileSetData.dat` — 6 map-chip sets (after 0xD1 tag, terminated 0xCF); per set: 16 chip-PNG path strings + paired flag blocks (u8 array + i32 array sharing one count); full consumption (5,049 B)
  - [x] `.mps` map format (`Data/MapData/*`, plain records, magic "WOLFM") — header + 3 stacked chip planes (3·W·H·u32, independent of the `layers` field) + event tail: `6F 39 30 00 00`-marked records → pages (`0x79` … `03 00×6`/`0x7A`) with the shared CE command grammar, file ends 0x66; **105/105 files byte-exact** (676 records / 1,028 pages / 8,931 commands), head page-count == parsed pages on every record; native `mpsinfo` CLI totals match the independent PowerShell sweep exactly. No separate per-map `.dat` event files exist — all per-map data lives in `.mps` (old "MapXXX.dat" idea is dead)
- [ ] Stage 1 supplementary — `Reader::crypt_body` (LCG-XOR 3-stream, seeds read from raw header bytes; reference-exact) implemented for future encrypted records; game-dat entry decrypt now body-only per reference semantics
- [ ] Stage 2 — event VM: variable banks, editor command dispatch (messages, picture ops, jump/call/condition), MapX object loop.
- [x] Stage 3 — headless map-runtime object layer: ENGINE page-pred decoded
      (`0x482240` evaluator + `0x44B530` top-down pick), `mapobj`/`mpspagewalk`/`mpssweep` census tools.
  - [ ] Stage 3 remaining — renderer surface (SDL-family), audio (Oboe), input mapping.
- [ ] Stage 4 — Android NDK packaging → signed debug APK.
  - [x] `android/` shell project: Gradle 8.7 + AGP 8.6.1 + NDK r27, `libwolfvm.so`
        from the same clean-room `src/` (arm64-v8a + x86_64), JNI bridge
        (`WolfVm.cmd == run_cli` argv dispatch, pipe-captured), console Activity
        (selftest / locate-data / game+CE info / sweep probe / mps+mapobj).
  - [x] `assembleDebug` debug-signed APK (build instructions below).
  - [ ] on-device validation pass.
- [ ] Stage 5 — parity pass vs. Windows runtime (basic fixes; cg/voice/scene assets included as dumb loaders).

Backup: private GitHub repo `tencentfurrys/wolfvm` — pushed at each verified parser milestone.

## Build (dev loop, Windows)

```powershell
cmake -S . -B build -G "MinGW Makefiles"
cmake --build build
build\wolfvm.exe selftest
build\wolfvm.exe datinfo "..\Eris Dysnomia\Data\BasicData\Game.dat"
```

## Build (Android shell, Stage 4)

Prereqs: JDK 17, Android SDK (platforms;android-34, build-tools 35.0.0, NDK
27.3.13750724, cmake 3.31.5). `local.properties` with `sdk.dir=` stays local
(gitignored).

```powershell
cd android
.\gradlew assembleDebug        # → app\build\outputs\apk\debug\app-debug.apk
.\gradlew assembleRelease      # same code, signed with the debug key
```

The shell runs the same clean-room core on-device: a console activity with
buttons for `selftest`, data-locate, `gameinfo`/`commoninfo`, a `sweep` probe
(ids 0..20), `mpsinfo -c` and `mapobj`; the game data tree lives under
`/sdcard/WolfVM/Data/...` (path editable in the UI; All-files access requested on
first launch). Missing files are pre-validated in the UI because the native
reader aborts on open failure — copy `Data/` from the restored game root first.

`selftest` runs the in-memory VM regression net (24 checks; no game data needed):
121 gates, select/else/498/499 structure, loops + 171 break, 210 calls with
locals/strings/ret-bind, forward label scan, and both harness fault classes
(stepcap / reccap). It also PINS the current 600xxx early-terminate behavior
(the v5-sweep-measured binary) until the engine-true continue-past question is
settled by a data-driven sweep re-run.

### sweep — the harness, inside wolfvm

The per-id sweep + classifier (ENGINE_ANALYSIS 12.5 method) is now native:

```powershell
build\wolfvm.exe sweep "..\Eris Dysnomia\Data\BasicData\CommonEvent.dat" `
  --db "..\Eris Dysnomia\Data\BasicData\DataBase" `
  --cdb "..\Eris Dysnomia\Data\BasicData\CDataBase" --cap 2000000
```

Fresh GameState per id (= one process per id in the old external harness), same
2e6 step-cap default, classes OK / STEPCAP / RECCAP / OTHER, `.mps`-side gap
reporting, and `--from/--to` windows for re-running single ids (e.g. the ev581
600xxx window after any behavior change). DB pairs are parsed once and restored
to pristine on writes, preserving the harness's per-id isolation.

Sweep v6 baseline (2026-10-22, full 584 ids): **OK=485, stepcap=86, reccap=13,
other=0, gap=0** — per-id diff vs the archived v4 baseline is exactly one id
(168 STEPCAP→OK via the 171 fix). Artifact:
`analysis/temp/full_sweep_v6.txt`; per-id diffing with `tools/compare_sweep.ps1
<baseline.txt> <candidate.txt>` (kept outside the repo, tools are not committed).

### 2026-10-22 session changes

- tools: `restore-data.ps1` — one-command re-materialization of the game data
  on a fresh session (extract → locate → Bonus decrypt hook → dat-layer verify
  → sweep probe id 0). The per-session download/decrypt cost is now scripted;
  the only knowledge that ever decays is regenerable artifacts.

- vm: 171 BreakLoop double-pc-advance fixed (jump lands on e498+1, then an old
  `++f.pc` skipped the first statement after the loop — same family as the
  earlier 210 callee-return double-advance fix). Sweep v6 re-baseline needed
  when data returns (step counts near 171 uses may shift).
- vm: 600xxx comment corrected to match verified behavior (note + early
  terminate = the v5-measured binary; engine continue-past documented as TODO
  pending re-verification). Behavior unchanged.
- vm: hot-loop speedups (flat per-cid census array flushed RAII-style + note-cap
  guards at per-step note sites) — step-cap windows went ~10x faster. Then a
  deeper sweep pathology surfaced via single-id timing: `db_set` rescanned every
  row of a type on each write-driven grow, so row-append loops were quadratic —
  ev66 alone spent 430 s under the 2e6 step cap. Grow is now O(1) per write
  (per-row allocation-on-write; holes still read 0, so observable semantics are
  unchanged) — sweep v7 re-run: classes IDENTICAL to v6 (0 per-id diffs),
  runtime 2429 s → 198.6 s.
- tools: `mpssweep <X.mps> [--ce CommonEvent.dat] [--db/--cdb/--from/--to/--cap]`
  — Stage-2 headless map-page runner: synthesizes one common-event record per
  page (artificial ids 1000000+, real CommonEvent.dat optionally merged so the
  pages' 210 callees resolve as the engine's loaded table), runs every page
  through the same VM kernel with sweep-style classes. (approx) semantics:
  page activation conditions are NOT modeled (engine MapX object side, Stage 3);
  fresh GameState per page, DB restore-on-write isolation like `sweep`.
  1-1.mps: 15 OK + 1 RECCAP (rec 9 page 1, 210 recursion cap) in 0.5 s.
  All-maps run (105 files / 1028 pages, ~132 s): **OK=868, stepcap=35,
  reccap=125, other=0, gap=0** — every page terminates in a defined
  state; log at `analysis/temp/mpssweep_all.txt`.
- tools: `selftest` (in-memory VM net, data-free) + `sweep` (native harness +
  classifier) so the verification loop no longer depends on one machine's
  PowerShell harness.
- map census: `mpsinfo -c` aggregates the cid histogram of a `.mps` (and across
  all 105 files via a caller loop). Map-side total ≈ 7.2k commands; cids beyond
  the CE-decoded set that map pages use: 173 EraseEvent ×106, 126 BanInput ×2,
  125 AutoInput ×1 — the Stage-2 MapEvent-runner decode backlog.
- tools: `mapobj <X.mps> [--ticks N]` + `src/map_runtime.h` — Stage-3 headless
  map-object layer (MODEL, approx): per-object `{rec, erased, active_page}`,
  pluggable page predicate (`CondDefaultPred`: cond==-1 satisfied, other
  scalars inert), classic-Wolf top-down pick, sticky erase (`erase()` =
  cid 173 hook), tick() re-evaluation. selftest grew 24→28 checks: 4 MODEL
  pins (top-down pick, erase clears page, erase sticky, cond!=(-1) inert) —
  explicitly model semantics, NOT engine claims; page selection is
  TODO verify vs Game.exe and the model reproduces the known contradiction
  (multi-page all-(-1) records activate their last page — e.g. the 1-1 door
  picks page 1) that §14.2 documents until the loader is decoded.
- tools: `mpspagewalk <Data/MapData-dir>` — Stage-3 page-field census over all
  `.mps` in a directory (native; one machine-clean line per page + aggregate
  histograms; a shell-side census of this kept corrupting rows). Result on the
  target: 676 records / 1028 pages (byte-baseline match). Facts: `unknown16[0..3]`
  LE i32 equals the page position on 863/1028 pages; the last page of a
  multi-page record always carries the `{i0=30, b4=0x23, flags[0]=0x31}` tuple;
  block10[4] classes {0x20 x282, 0x23 x687, 0x25 x39, ...}. Engine reading of
  these fields stays open (TODO verify vs Game.exe loader) — recorded in
  ENGINE_ANALYSIS §14; artifact `analysis/temp/mps_pagewalk_1.txt`.

- engine: **map page selection decoded from Game.exe** (ENGINE_ANALYSIS §14.3).
  The "unknown" page bytes are the 4-term start condition block
  (op bytes = `graphic_flags`, LHS keys = `chips`, RHS = `unknown16`,
  trigger = `graphic_head[4]`); evaluator 0x482240 (prefix list, comparator
  table > >= == <= < != &=, self-var rebasing), pick 0x44B530 (top-down,
  erased → -1). `EnginePagePred` replaces the MODEL predicate in `mapobj`;
  the 1-1 door now shows page 0 until SelfVar2 = 1. selftest 28 → 36.
- vm: **cid 111 comparator table fixed to engine truth** (0 > 1 >= 2 == 3 <= 4 < 5 != 6 &=,
  JT 0x4586C8; old empirical table had 1/4 swapped). Sweep v8: **OK=512, stepcap=72,
  reccap=0, other=0** (v7 485/86/13); maps OK=900/stepcap=128/reccap=0 (§14.5).
  Linux build: `g++ -std=c++17 -O2 -o build/wolfvm src/*.cpp` (no cmake needed).

## Data layout expected by the VM

```
<gameroot>/Data/BasicData/Game.dat          # engine config 'M' record
<gameroot>/Data/BasicData/CommonEvent.dat   # common events 'C' record (script payload)
<gameroot>/Data/BasicData/DataBase.dat/.project, CDataBase*, SysDatabase*, MapTree*, TileSetData.dat
<gameroot>/Data/<ArchiveFolder>/...         # assets (pictures/maps/sounds), decrypted or packaged
```

## Conventions

- C++17, CMake, no external deps in the VM core (host shell may use SDL2 later).
- Parse layer returns errors with byte offsets — every boundary case is a test fixture.
- Nothing here ships engine assets or editor software; format facts come from our own
  binary analysis of shipped games + documented public parsers.

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
- [ ] Stage 3 — host shell: SDL-family renderer surface, audio (XAudio2 semantics → Oboe), input mapping.
- [ ] Stage 4 — Android NDK packaging → signed debug APK.
- [ ] Stage 5 — parity pass vs. Windows runtime (basic fixes; cg/voice/scene assets included as dumb loaders).

Backup: private GitHub repo `tencentfurrys/wolfvm` — pushed at each verified parser milestone.

## Build (dev loop, Windows)

```powershell
cmake -S . -B build -G "MinGW Makefiles"
cmake --build build
build\wolfvm.exe datinfo "..\Eris Dysnomia\Data\BasicData\Game.dat"
```

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

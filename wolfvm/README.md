# wolfvm — native WOLF RPG Editor 2.2x VM (Android target)

A clean-room reimplementation of the WOLF RPG Editor runtime ("RPG Drive") needed to run
Eris Dysnomia on Android. Built from the engine analysis in `../ENGINE_ANALYSIS.md`.

**Targets of record:** Windows x64 (dev loop) → Android arm64 (NDK, final APK shell).

## Status: Stage 1 (format layer)

- [x] Stage 0 — data unlocked: all 13 `.wolf` archives decrypted to plain files/folders
      (tool: UberWolfCli v0.6.4, keys auto-detected from shipped `Game.exe`; data now plaintext per-file).
- [ ] Stage 1 — dat layer parsers (`WOLF` magic, string/blob fields, common-event table, DB structures, MapTree, Game.dat config).
- [ ] Stage 2 — event VM: variable banks, editor command dispatch (messages, picture ops, jump/call/condition), MapX object loop.
- [ ] Stage 3 — host shell: SDL-family renderer surface, audio (XAudio2 semantics → Oboe), input mapping.
- [ ] Stage 4 — Android NDK packaging → signed debug APK.
- [ ] Stage 5 — parity pass vs. Windows runtime (basic fixes; cg/voice/scene assets included as dumb loaders).

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

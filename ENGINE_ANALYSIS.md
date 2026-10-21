# Engine Analysis — Eris Dysnomia (v1.02, Kagura Games build)

Target: `C:\Users\RDP\Documents\Default Project\Eris Dysnomia\` (extracted from `C:\Users\RDP\Downloads\Eris_Dysnomia_v1.02.7z`)
Method: REA CLI 6.5.0 (`rea execute-rizin-command`, `rea inspect-with-objdump`) with Rizin 0.9.1 and GNU objdump, plus local static string/RTTI scanning. All findings below are observed from the shipped binaries, not memory/speculation.

---

## 1. TL;DR — what engine this is

`Eris Dysnomia` runs on the **WOLF RPG Editor runtime ("RPG Drive") Version 2.25**, compiled **on top of the DX Library (DxLib)**, and ships as a plain **Win32 x86 (PE32) GUI executable**. Its data archives (`.wolf`) use the standard Wolf/DxArchive container ("DX" + format 8) but are **WolfPro-encrypted** (TOC and contents scrambled) — decryptable with open-source interop tools (WolfDec / UberWolf), whose key auto-detection works *from Game.exe itself*.

---

## 2. Binary identity (primary evidence)

From `rea execute-rizin-command Game.exe iI` (Evidence `ev_a7c66bd1…`):

| Property | Value |
|---|---|
| Format | PE32 (32-bit), Windows GUI subsystem, little-endian, i386 |
| Base / size | 0x00400000 / 0x6a3000 (~6.9 MB) |
| Sections | `.text` 0x3f85a8 @0x401000 · `.rdata` 0x9cd02 @0x7fa000 · `.data` 0x20b000 @0x897000 · `.rsrc` 0xbc8 @0x1026000 |
| Link date | **2022-05-09 06:45:54 UTC** |
| Toolchain | Old MSVC link ("Linker 07.10"), statically-linked CRT, stack cookies on |
| Mitigations | **No NX, no PIE** (classic 2016-era Wolf runtime build), `stripped=false` |
| Debug path (embedded PDB name) | `……\RPGドライブプログラム＋◆Editor - 20180525_ver2.24版_SteamKit版込\Rpgドライブプログラム\Release\Game2.25.pdb` |

The embedded PDB path is the strongest identifier: the developer's own folder names declare
- the runtime: **"RPGドライブプログラム" (RPG Drive program)**,
- its version: **Game2.25** (runtime 2.25, from the **2.24-line editor** source tree dated 2018-05-25),
- a **SteamKit-bundled** build variant, later localized by Kagura Games.

---

## 3. Engine stack (what's linked inside)

### 3.1 Base library = DX Library (DxLib)
DxLib source paths were compiled in (embedded asset-build breadcrumbs in `.rdata`):
- `Source\Library\Main\DxArchive_.cpp` — DXLib's archive engine → the `.wolf` container is the DxLib "DXArchive" scheme (hence the **"DX" magic** byte-for-byte at file start).
- `Windows\DxGraphicsD3D9.cpp`, `Windows\DxModelD3D9.cpp`, `Direct3DCreate9`, `Direct3DCreate9Ex` — Direct3D9 renderer + 3D-model path.
- `DxUseCLibOgg.cpp` (Ogg Vorbis decode in-exe), `DxUseCLibLive2DCubism4.cpp` (Cubism 4 support compiled in), `Windows\DxUseCStrmBaseFilter.cpp` (DirectShow media stream).
- MSVC RTTI class names in the EXE: `DX_UIElementSink@DxLib@@`, `D_IXAudio2VoiceCallback@@`, `D_IMMNotificationClient@@`, `D_ITfUIElementSink@@` — DxLib's C++ objects, XAudio2 voice callbacks, and WASAPI/TSF interop (IME).

### 3.2 Graphics
- Host window/input: plain Win32 (USER32 `GetAsyncKeyState`, `ClipCursor`, dialogs) + GDI (`GetDeviceCaps`).
- Renderer: **dynamic Direct3D9** — import table has no `d3d9.dll`/`d3dx9.dll` at all; instead the exe carries a **probe list `d3dx9_24.dll` … `d3dx9_50.dll`** and uses `LoadLibraryW` + `GetProcAddress` to pick the newest one. FXC-compiled HLSL blobs are embedded in `.rdata` (strings include HLSL compiler signature `9.29.952.3111`, shader uniforms such as `SV_POSITION`/`TEXCOORD`, semantic tokens like local/world/view/projection matrices, texture/blend samplers, toon-outline parameters).

### 3.3 Audio
- **XAudio2** output voices (RTTI callback) with **WASAPI device-change handling** (`IMMNotificationClient`), DirectShow for streamed media, in-exe Ogg decoder.
- **MIDI via external `GuruguruSMF4.dll`** (147 KB, sits next to Game.exe; runtime looks it up and prints a localized error when missing).

### 3.4 Other wired subsystems (imports)
`WININET` (net checks — no plaintext URLs found in strings, resolved at runtime), `PSAPI` (`GetProcessMemoryInfo` — memory HUD), custom TTF registration via `AddFontResourceExA` (game publishes `azuki.ttf`, `kanaA.ttf`, `love.ttf`), clipboard read/paste, `SHLWAPI` path helpers, embedded **zlib/unzip 1.01** (per-entry compression in archives).

---

## 4. Data layer — `.wolf` archives

### 4.1 Container header (verified bytes of `Data\BasicData.wolf`)
```
44 58 08 00                 "DX" + format/version 8
58 14 00 00                 0x1458
40 00 00 00 00 00 00 00     0x40 (first region starts right after this 64-B block)
04 2A 0C 00 … 0C 00 00 00   remaining region size records (0x2C04, 0x628, 0x1438, 0x3A4, 0xC)
```
The fixed 64-byte header parses cleanly; **everything after offset 0x40 is high-entropy** — the TOC (directory names, file names, entry table) is not plaintext.

### 4.2 Protection = WolfPro
- ASCII scan of the first 8 KB yields only noise → index and entry contents are scrambled at build time.
- This matches the "two-layer" Wolf published-game encryption handled by the open-source tools:
  - **WolfDec** (v0.3.3) — decrypts Wolf archives using code derived from DxLib.
  - **UberWolf** (GUI/CLI) — "automatic decryption key detection" + "Pro Editor Protection Key" support, and it *takes Game.exe as the key source*: the per-game keys are embedded in the runtime, which is why the engine itself can open these archives at startup.
- Runtime-side references (strings in Game.exe): `GameDatFileName`, `WOLF_FileReadText`, `*.wolf`, `BASICDATA\`, `BasicData\CommonEvent.dat`, `BasicData/CDataBase`, `BasicData/SysDataBase`, `BasicData/DataBase`, `BasicData/TileSetData.dat`, `BasicData\MapTree.dat`, and migration messages stating `Game.dat` lives in `Data/BasicData` since WolfRPG Ver 2.00.

### 4.3 The 13 shipped archives
`BasicData`, `MapData`, `MapChip`, `Picture`, `PicEX`, `BGM`, `SE`, `Hanime`, `Enemy`, `BattleEffect`, `BG`, `SystemGraphic`, `Kagura` (`.wolf`, ~53 MB total) + fonts + `GuruguruSMF4.dll` + `Config.exe` (299 KB Wolf config dialog) + `Script.vdf` (Steam install script containing the Kagura Games registry key).

---

## 5. What this buys you (practical paths)

1. **Extract/decrypt game data:** `UberWolfCli.exe <game>\Game.exe` (full auto-detect) or drag the `Data` folder / individual `.wolf` files. This is the standard, well-established open-source route for WolfPro data; keys needed are read from the game's own exe.
2. **Deeper code work:** the decompiler is the one REA capability still missing here (no Ghidra/Hopper/IDA on this box). Path of least effort is Ghidra 12.1.x + JDK 21 → set `GHIDRA_INSTALL_DIR` → REA gives pseudocode for the archive-open/decrypt routines (`DxArchive_.cpp`-derived functions), or you can keep using Rizin disassembly as done here.
3. **Engine reuse/interop:** because the data is standard Wolf 2.2x, the editor ecosystem's usual tooling (Wolf editor-compatible decompilers, translation/patch tooling) applies once decrypted.

---

## 6. Analysis run-book (reproducible)

```powershell
npm install --global rea-agents              # REA 6.5.0
winget install --id Rizin.Rizin -e           # Rizin 0.9.1
$env:REA_RIZIN_COMMAND='C:\Program Files\rizin\bin\rizin.exe'
rea execute-rizin-command .\Game.exe iI      # binary identity (ev_a7c66bd14e…)
rea execute-rizin-command .\Game.exe ii      # import table
rea inspect-with-objdump .\Game.exe section_headers
# strings/RTTI scans over Game.exe (ASCII + Shift-JIS) — 230k ASCII strings pulled
```

Artifact digests observed (Rizin Evidence): `Game.exe` sha256 `20fe3e82…4e9e323`, unchanged across operations.

## 7. Open items / minor unknowns
- WININET is imported but no URL string is visible → endpoint string likely assembled/obfuscated at runtime (candidate for decompilation task #2).
- A handful of very long base64-looking blobs in `.rdata` besides the shaders (candidates: pre-baked Cubism model / font assets) — not decoded in this pass.
- Config.exe was identified by role and size but not exhaustively parsed (standard Wolf config tool; same font/media plumbing).

---

## 8. TileSetData.dat loader chain (verified, wolfvm parser basis)

Call sites (each string is referenced exactly once in `.text` — byte-scan for the
`db 80 50 81 00` pointer):
- `Game.exe` VA `0x45001E`: `push "BasicData/TileSetData.dat"` (`.rdata` 0x815080) →
  `mov ecx, [local obj]` → `call 0x48a5d0` → `mov ecx, 0xaab0f8` → `call 0x486740`
- `Game.exe` VA `0x472272`: `push 0x816c14` (same filename) → `mov ecx, 0xaab08c` → `call 0x48a5d0`

Key instruction mapping — Rizin `iz` on this build prints **(paddr, vaddr)**,
so string addresses below are VAs (= paddr + 0x400000).

`0x48a5d0` = archive-entry reader + record-stream validator:
- reads filename (arg), opens/decrypts entry into a heap buffer saved in `[0xaa1790]`
- **parse cursor `[0xaa36f0]` starts at 0**; first loop copies 10 header bytes into
  stack scratch (this seeds the reserved-byte validators later)
- header check compares raw buffer bytes against `W O L F M` at indices 1/4/5/7/8
  (indices 0/2/3/6/9 stay zero), else "not a game data file" dialog path
- reads 1 u8 at cursor: must be `0xCF` / `0xD0` / `0xD1` → mode 1/2/3 stored in
  global `[0x89728c]` (0xD1 = TileSetData, mode 3)
- reads i32 (memcpy n=4) = record count → vector resize, then loops
  `0x48ac20` once per record
- then one u8 that must equal **0xCF** (terminator; the file's last byte)
- reserved bytes 0/3/9 of the header must be zero at exit

`0x48ac20` = per-record parser (this-call, record object ptr in ecx):
| offset | read | engine action |
|---|---|---|
| +0x00 | `[i32 len][len bytes]` | string m0 → `0x4d5d70(record+0, str)` |
| loop i=0..7 | `[i32 len][len bytes]` | string m1..m8 → `record+28*i+0x1c` (std::string stride 0x1c) |
| `cmp [0x89728c],3; jl` skip | — | only when mode ≥ 3 |
| loop i=8..15 | `[i32 len][len bytes]` | strings m9..m16; **mode<3 skips file reads and stores constant** from `.rdata` 0x816c79 |
| — | u8 | **must be 0xFF** (else MessageBoxA error, literal 0x817528) |
| — | i32 | flag-block count → `resize(member+0x1dc)` |
| loop n | 1 byte each | into member +0x1dc (vector of 1-byte elems) |
| — | u8 (unchecked by engine) | second FF tag — our file always carries 0xFF |
| — | i32 | int-block count → `resize(member+0x1ec)` |
| loop n | 4 bytes each (mode ≥ 2) or 1 bytes (mode == 1) | into member +0x1ec |

Observed file facts under this schema (TileSetData.dat of Eris Dysnomia, 5,049 B,
tag `0xD1`, count 6, terminator `0xCF` at 0x13B8):
- record 0: m0 = SJIS `8B F3` = 「空」("empty" group), m1 = `MapChip/Image2.png`,
  m2..m16 = one-byte empty strings; flag blocks = 272 u8 + 272 i32
- record 3: m0 = `83 66 83 74 83 48` = 「デフォ」("default" group), m1..m16 = the 16
  standard map-chip PNG paths; flag blocks = 456 u8 + 456 i32
- records 1/2/4/5: all-empty strings, 24/24 flag sizes
- the `FF` blocks therefore carry per-chip tile data (e.g. 272 = `Image2.png`
  chunk rows, 456 = auto-tile chunk set), not free-form padding.

Clean-room decoder: wolfvm `src/tile_set_data.cpp` (`tileset` CLI command); verified
full consumption of the real 5,049-byte file with all tag/count/terminator checks.

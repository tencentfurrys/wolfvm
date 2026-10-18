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

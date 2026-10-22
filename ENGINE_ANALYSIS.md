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

---

## 9. VM Stage-2 engine findings (decoded CommonEvent semantics, run-backed)

All findings below are displaced from live `commonrun` behavior against
`Eris Dysnomia` data + Rizin disasm of `Game.exe` (VA = paddr + 0x400000).

### 9.1 Packed DB-ref bases — RESOLVED, previous map was rotated

`fcn.0043a240` (generic operand resolver) decodes the 1e9-band refs directly:

| band | engine object at 0x480130 call | pair |
|---|---|---|
| `[1.0e9, 1.1e9)` | `mov ecx, 0xaa75f0` | **DataBase** |
| `[1.1e9, 1.2e9)` | `mov ecx, 0xaa9498` | **CData** |
| `[1.3e9, 1.4e9)` | `mov ecx, 0xaa7670` | **Sys** |

Decode: `rem = packed % base; type = rem/1e6; row = (rem%1e6)/100; field = rem%100`.
The earlier "1.0e9=Sys, 1.1e9=DataBase, 1.3e9=CData" mapping was wrong and
caused an entire class of state divergence (status flags written to one pair,
read from another → infinite gacha re-roll loop in ev9).

Companion proof — cid 250 select nibble (aligned disasm 0x43bdc4-0x43bdf9):
`(flags>>8)&0xF`: **0 → 0xaa9498 (CData), 1 → 0xaa7670 (Sys), 2 → 0xaa75f0
(DataBase)**; default/unknown → skip command (0x43be0a check).

### 9.2 cid 250 (Database) prep block — row/field/value all resolve

Shared prep before the read/write split (0x43c077/0x43c0f7/0x43c177):
- row operand (record elem 2): `cmp 0xf4240; jl raw` → when ≥ 1,000,000 resolve
  via `0x43a240(0xffffffff, frame-ctx, operand)` — i.e. eval-if-≥1e6.
- value operand (record elem 5): same ≥1e6 rule, same resolver.
- field operand (record elem 3): pushed through the type's name/marker path
  (0x4dc500). vm models this as "eval-if-≥1e6 else raw", which fixed
  `ev9#36 type 43 field 1600033` (field was a cself ref).

`(flags>>4)&0xF` = value-marshall flavor (expr vs literal) — non-semantic.

### 9.3 cid 121 — 7-arg matrix form, db row growth on write

- 7-arg form (12 uses in data, all `target = 1100000000` bare CData[0] ref):
  `[DB-pack, value, ?, flags, mode{1=plain,4=rand}, 0, ROW]` — **row = args[6]**
  (pack's row-part overridden), type/field from the pack. mode 4 correlates
  with flags 0x6000 rand (`[pack, 0, 1, 0x6000, 4, 0, 44]` = rand(0..1)).
- Engine write path bounds-checks **type count (0x480100) and field count
  (0x480250) only — no row check**; rows grow on write (ev7/ev29 clear matrix
  rows 63/78 of CData[0]-f0 whose file has 1 row). vm db_set now grows rows +
  int/string value arrays sized by field max-index.
- 5-arg form (`flags|0x10000-family`, 47 uses: {65536, 65792, 69632}):
  `[dst, value, 0, flags, n]` — suspected gradual/step write; semantics
  semantics unmodeled — TODO.

### 9.4 111/401 — cond sets result register; 401[n] matches register

Data-proven (ev6 add/remove split `#38[1]`/`#199[2]`, chains `#54[1]`→`#57[2]`):
cid 111 evaluates its (lhs,rhs,op) triples (OR'ed) and sets a **result
register**: 1 = true, 2 = false. `401 [n]` enters iff register == n; the
non-matching selectors skim to the next 401/420/499 at the same indent, and
`420` = unconditional else. vm previously collapsed false → 0 ("skim all"),
so else-branch bodies never executed.

### 9.5 213 JumpLabel — backward jumps required

Labels are collected from 212 into a per-frame map; 213 must consult the map
first (backward/retry loops) then forward-scan for not-yet-executed 212s.
Forward-only scanning broke every "collect-item-then-retry" flow
(ev11 `213 label '…' not found`; after the map fix ev11 runs 417 cmds, 19 writes).

### 9.6 Variant cell passing for 210 callee args

`210 [500000+id, opts, args...]` passes trailing args as **cells, not ints** —
string refs (3000066 family) must arrive as the referenced string cell.
`Vm::fetch_cell` mirrors the engine's generic getter (DB ref → db_get cell,
cself/sys bands → cell copy, else literal int); case 250's write value fetch
uses the same helper.

### 9.7 Stateless run limits (documented divergences, fail-loud)

- **Poll-wait loops** (ev510 shape: label top → read CData[10]-r37-f{cself12} →
  cond(`< 1`) → 401 → 213 jump-back): live engine waits for an *external*
  system common to flip the polled cell. Headless stateless runs have no
  external writer → step cap (2,000,000) aborts with a clear message. ev8/ev214
  both end inside ev510 this way.
- **ev9 gacha**: sys-var 2000070 (gacha-limit flag, set only deep in an
  indent-15 branch of some common, reset in another) + lever counters never
  materialize statelessly; 6⇄360 mutual recursion capped at kMaxDepth=256.
- Recursion cap raised 64 → 256: legitimate deep call chains exist (ev9 reached
  13,350 executed commands before the cap).

### 9.8 Previously decoded (kept from earlier sessions)

- Deferred-command executor `fcn.0043a5c0` (queue at 0xaa76fc): dispatch 498 →
  0x43c3eb, 213 → 0x43b2b6, cids 106..109 jump table (0x43c510, 4 entries),
  250 → 0x43b9e1, 401 → 0x43ad6d, 420 → 0x43ae0a, pseudo-ranges
  210000-210023 & 111000-111005 → 0x43aeb7.
- `fcn.00454380` = message-window renderer (width/font plumbing), NOT the
  cid-122 semantic executor. "122 flags 1025/513" notes = noise; flags&0xF
  op still executes as append.
- 210 opts values {0, 16, 3, 4, 2, 1, 4112, 0x1000001, 0x1000002}: unmodeled
  meaning; noted only outside {0, 16}.
- Packed eval_cmp op map: 1 '<', 2 '==', 3 '<=', 4 '>=', 5 '!=',
  6 bitAND-nonzero, 7 bit-superset (data-supported).

## 10. Stage-3 decode session (fcn.0043a5c0 full structure)

### 10.1 The cell-address model (the big correction)

**Everything in the engine's variable system is `int*` home-cell addresses,
not resolved values.** The generic resolver `0x43a240(mode, ctx, operand)`
maps an operand-encoded int to the address of its home cell and returns 0
for "no cell" (out-of-range DB ref etc.). Literals `< 1e6` are bound by
address too: the cid-121 prep stores `&record_elem` (the const cell) into
the work vector. The `skip if resolved == 0` checks are therefore
"skip whole command when any operand has no cell" — NOT "skip when value
is zero". wolfvm's `fetch_cell` (value-copy semantics) approximates this;
the true shape is reference binding.

### 10.2 fcn.0043a5c0 skeleton (decoded 2026-10-21)

- Outer task loop `[ebp-0x18]` over queue `0xaa76fc` (`0x4db750` = size,
  `0x4db810` = at(i); task+0x30 = its command vector, `0x4da3d0` = size,
  `0x4da490` = at(i)). Inner loop `[ebp-0x1c]` over commands; record =
  `[ebp-0x20]`; cid = record.elem0 → `[ebp-0x138]`. Record byte +0x10 =
  indent. Tail loop-continue = 0x43c485, task-continue = 0x43c48a.
- Every 40 tasks: `call 0x5470a0` (UI yield only — NOT a drain).
- Dispatch: cid == 0x1f2(498) → 0x43c3eb; cid band ≥ 213 → 250 ==
  0x43b9e1, 401(0x191) == 0x43ad6d, 420(0x1a4) == 0x43ae0a; cids 106..121
  byte table @0x43c520 → jump table @0x43c510, 4 cases:
  0 → 0x43a821 (cids 106/107: string compare vs const, rewrites elem0 =
  0x67(103) when matched, else skip); 1 → 0x43a87e (**cid 111**); 2 →
  0x43aec7 (**cid 121 + all 210000-band pseudo**); 3 → 0x43c485 skip
  (cids 108..110, 112..120 = no-ops).
- Pseudo-band acceptance (all → 0x43aeb7 = `elem0 = 0x79(121)` restore +
  fall into 121 handler, or → 0x43a86e = `elem0 = 0x6f(111)` + fall into
  111 handler): 210000-210003 (f00=0·f000=0..3), 210010-210013 (f00=1),
  210020-210023 (f00=2), 210030-210033 (f00=3), 210040+ ALL, and
  111000-111005 → 111. Skipped pseudo: 210004-210009, 210014-210019,
  210024-210029, 210034-210039. cid 497488/497489 → the 498 arm.

### 10.3 cid-121 handler (0x43aec7) — verbatim gates

1. `target == 1100000000` → skip whole record.
2. `flags & 0xFF != 0` → skip (observed low-byte uses: {4, 32, 0x3302}).
3. `flags & 0x10000` → skip **in this handler** (47 census uses; executed
   elsewhere — deferred site still unfound; wolfvm's instant-apply stays
   an approximation, note text revised).
4. `0x43c9b0(ctx, v1)` / `0x43c9b0(ctx, v2)` kind-check == 0 → skip.
5. `v1 ≥ 1e9` or `v2 ≥ 1e9` → skip (a DB-ref operand cannot be a source of
   the immediate path).
6. Prep: resize +0x34 vec(8) and +0x44 vec(3). **+0x44 = [dstCell*, v1*,
   v2*]** — target resolved first (skip if no cell), then each source:
   raw < 1e6 → bound to `&record_elem` (literal const cell); ≥1e6 →
   resolver address (0 → skip record).
7. Flags copy into +0x34: vec1 = (flags>>8)&0xF (`imul esi,0xa` confirmed
   at the 0x43b1b5 hidden crack), vec2 = (flags>>12)&0xF, vec0 = 1 (→ 2
   when f000 ∈ {0,1} and raw v2 == 0; marker meaning still unmodeled).
8. Dispatch on (f00, f000):
   - f00 ≥ 4 → only **f00 == 4** continues at 0x43b1df (requires raw
     v1 < 1e6, v2 < 1e6; f000==0 → skip if v1raw+v2raw == 0; f000==1 →
     skip if v1raw−v2raw == 0; else) → pseudo = 210000+40+f000. f00 ∈
     {5..15} → **skip**.
   - f00 ≤ 3 ∧ f000 ≤ 2 → pseudo = 210000 + f00*10 + f000, elem0 =
     pseudo, continue (requeue path).
   - f00 ≤ 3 ∧ f000 ≥ 3 → **skip**.
     - Census impact (recomputed 2026-10-22 from all_dump.txt, 11,174
       cid-121 records, 10,772 regex-matched; f00 = (flags>>8)&0xF the
       tens/accumulate digit, f000 = (flags>>12)&0xF the ones/base digit
       -- supersedes the older per-gate counts, which mislabeled the x285
       band as "flags 0x8000", a value that occurs **0** times):
       - EXECUTED (gate 8 continue arms): 0x0000->210000 x5105;
         0x0100->210010 x1741; 0x1000->210001 x1094; 0x0200->210020 x568;
         0x2000->210002 x241; 0x0400->210040 x215 (f00=4, zero-cancel
         gated); 0x0300->210030 x165; 0x2100->210012 x162; 0x1100->210011
         x145; 0x1200->210021 x61; 0x1400->210041 x17; 0x2200->210022
         x19; 0x1300->210031 x15; 0x2300->210032 x1 -- about 9,550
         uses (85%).
       - SKIP, ones f000 >= 3: 0x6000 x377, 0x3000 x234, 0x4000 x65,
         0x3100/0x3200/0x3300/0x3302 x44, 0x6100 x25, 0x6200 x10 ~ 753 --
         plausibly 2.25-dropped legacy opcodes (closes 11.4's f000=3 OPEN
         item: the dispatch band accepts 210003/13/23/33 but the tick
         selector maps them to the 0x0b skip arm; nothing executes).
       - SKIP, tens f00 >= 5: 0x0500-0x0800 x333, 0x1500-0x1800 x105,
         0x0A00/0x0B00 x4, 0x2600 x1, 0x6700 x2, 0xF900 x4 ~ 449.
       - SKIP, low byte <> 0: {0x0004, 0x0104, 0x0704, 0x0101, 0x0020,
         0x1020, 0x3302} = 21. fam16 (0x10000): 0 occurrences.
       wolfvm's implemented matrix + note-based skips match the engine
       gates; the old "wolfvm executes 0x6000 as rand-assign" concern is
       resolved (0x6000 -> skip; its x377 are noted, not executed).

### 10.4 The second interpreter (true entry 0x455650, see §11.2) — the TRUE arithmetic

A second dispatcher (state object `[ebp-0x10]+0x89c`) processes the
pseudo-cids with the same [dst*, v1*, v2*] +0x44 vec. Arms (JT
0x4575c7, byte map 0x4575f7 for cids 210001-210032; idx = cid−210001):

| pseudo | flags (f00,f000) | arm addr | semantics (decoded) |
|--------|------------------|----------|---------------------|
| 210000 | 0x0000 (0,0) | 0x455c3e | dst = v1 + v2 (v2 const 0 => v1-copy form) |
| 210001 | 0x1000 (0,1) | 0x455c88 | dst = v1 - v2 (SUB, x1094 uses) |
| 210002 | 0x2000 (0,2) | 0x455cd2 | dst = v1 * v2 |
| 210010 | 0x0100 (1,0) | 0x455d1d | dst += v1 + v2 (x1741 uses) |
| 210011 | 0x1100 (1,1) | 0x455d74 | dst += v1 - v2 (x145 uses) |
| 210012 | 0x2100 (1,2) | 0x455dcb | dst += v1 * v2 (x162 uses) |
| 210020 | 0x0200 (2,0) | 0x455e23 | dst -= v1 + v2 (x568 uses) |
| 210021 | 0x1200 (2,1) | 0x455e7c | dst -= v1 - v2 (x61 uses) |
| 210022 | 0x2200 (2,2) | 0x455ed5 | dst -= v1 * v2 (x19 uses) |
| 210030 | 0x0300 (3,0) | 0x455f2f | dst *= v1 + v2 (x165 uses) |
| 210031 | 0x1300 (3,1) | 0x455f87 | dst *= v1 - v2 (x15 uses) |
| 210032 | 0x2300 (3,2) | 0x455fdf | dst *= v1 * v2 (x1 use) |
| 210040 | 0x0400 (4,0) | 0x456038 | dst /= v1 + v2 (first-handler zero-cancel gated, x215) |
| 210041 | 0x1400 (4,1) | 0x456092 | dst /= v1 - v2 (same gating, x17) |

Matrix cross-checked 2026-10-22 against vm.cpp's signed-off comment
(pseudo = 210000 + f00*10 + f000, f00 = (flags>>8)&0xF the accumulate
digit {0:=, 1:+=, 2:-=, 3:*=, 4:/=}, f000 = (flags>>12)&0xF the base op
{0:+, 1:-, 2:x}; an imul/lea crack confirms the *10 + digit packing).
There are no arm rows for pseudo 210008/210009 (flags 0x8000/0x8100,
f000 = 8/9) or 210003/013/023/033 (f000 = 3): the tick selector maps
them to the 0x0b skip arm, and flags 0x8000 never occurs in census.
LABEL PITFALL (cost this session twice): an earlier draft carried the
+= arms under labels 210008/210009/210010. Recompute pseudo <-> flags
digit-wise before trusting any table cell edit.

### 10.5 cid-111 handler (0x43a87e) — real record layout decoded

Record = `[cid, count|hi, (A, B, terms)×N]`: elem1&0xF = triple count;
per triple i: elem[3i+2] resolved int (≥1.1e9 or resolver-0 → whole cond
aborts, +0x34 cleared, continue); elem[3i+3] normally bound (raw <1e6,
else resolver); if elem[3i+4] & 0x10 → **raw pointer bind** of elem[3i+3]
(no evaluate); each triple's flags&0xF appended to +0x34[2+i] (= per-term
op codes). The handler then **scans ahead in the task chain at the same
indent** (records +0x10 == this +0x10, `[ebp-0x44]`步行, bounded by 5
slots `[ebp-3c..]`), collecting the PCs of matching-indent 401(0x191) /
420(0x1a4) commands, **stopping at 499(0x1f3)/498(0x1f2)**; binds into
each found 401/420's +0x34 vec {resize 2; vec0 = 2; vec1 = pos}; a
second walk binds +0x34 vec8 = pos at a 420/499/498 terminator; finally
+0x34 vec0 = 1 and when vec1 == 1 the record's elem0 is rewritten to
111000 + vec2 (the 111000-band re-queue)
→ the deferred cond-binding machinery. wolfvm's 111-as-[op,A,B,flags]
register model is a different-shaped approximation that passes the
sweep; keep, but track divergence risk on cond-heavy events.

### 10.6 wolfvm fixes queued from this session

- **Applied this commit**: revert of the wrong "flags 0x100 = additive"
  `exec_setvar` edit (the engine routes flags 0x100 → pseudo 210010 in the
  second interpreter — arm decoding pending; do not model 0x100 additively),
  + reworded the 0x10000-family note to "engine-deferred; approximated
  instant" (§10.3 gate 3). Focused batch reproduces baseline classes:
  1/2/4/5/6/7/11/13/17/29/86/200/216/227/371/523 green-or-note,
  8/214/456/457/479/510 step-caps (456/457/479 stall in the row-name −3
  scan), 9 recursion-cap.
- 121 execute-skips exactly as §10.3 items 1/2/8 (biggest correctness
  win: target==1100000000 write-through bug + legacy-opcode true skips).
- 250 field −3 row-name read (still pending from earlier plan).
- DONE (earlier arms455 byte dumps, re-checked this checkpoint): arms
  210030-210042 = the *= and /= accumulate families (§10.4 table) —
  wolfvm's whole continue-band is arm-true.
- DONE (2026-10-22): cid-124 true handler = 0x45de60, byte-decoded to
  the writeback tail (§12.2) and re-modeled in vm.cpp; cid-122 executor
  identified = 0x45b670, partially decoded (§12.3); 210-callee 600xxx
  map-event band = noted headless no-op (ev581 now completes).
- Pending: where the 0x10000 family really executes (§10.3 gate 3);
  cid-122 op-2/bank arms (the ev212 + ev255/355 fails); ev457/ev510
  loop-advance mechanism.

## 11. Session checkpoint (thumb-drive safe)

### 11.1 First-interpreter loop mechanics (verbatim, rz_interp.txt 0x43a5c0)
- Outer loop `[ebp-0x18]` = task index over global list `0xaa76fc`
  (count via `0x4db750`); exit at `jnl 0x43c48f` (epilogue, `add esp
  0x184` ... `ret`). Every 40th task (`idiv 40; test edx; jnz` skips)
  calls `0x5470a0` = UI pump.
- Inner loop `[ebp-0x1c]` = command index reset to 0 **per task per
  sweep**; container = `0x4db810(0xaa76fc, task)+0x30`; count =
  `0x4da3d0(container)`, fetch record = `0x4da490(container, idx)`
  into `[ebp-0x20]`. cid = `*record` into `[ebp-0x138]`.
- Continues: `0x43c485 jmp 0x43a63d` (inner idx+1, same task) /
  `0x43c48a jmp 0x43a604` (outer idx+1, next task). Head cracks hide
  cmp-chains: visible `cmp 0xd5 (213) jz 0x43b2b6` = cid 213 IS a
  first-interp handler.
- Small-cid JT (key = cid-106, `cmp 0x0f jnbe skip`; byte map
  `0x43c520`, dword JT `0x43c510`): 106/107->`0x43a821`, 111->
  `0x43a87e`, **121 -> `0x43aec7` directly (handler head, works)**,
  108-110 + 112-120 -> skip. Then cmp 250 -> `0x43b9e1`, 401 ->
  `0x43ad6d`, 420 -> `0x43ae0a`.
- Pseudo-band acceptance verified as exact range cmps: bands
  {210000-3, 210010-13, 210020-23, 210030-33} -> `0x43aeb7`
  (restore elem0 = 121, fall into handler); 111000-111005 ->
  `0x43a86e`; 497488/497489 -> `0x43c3eb`.
- `0x43c9b0` = arg-kind validator; getter `0x4d95b0` = `&elem_k`
  (1847 e8 sites); `0x4d92c0` = clear-if-nonempty (wraps `0x4e5fa0`);
  `0x4d9490`/`0x4e5ef0` = resize. +0x34 = metadata array cleared +
  refilled every handler pass (NOT persistent); +0x44 = bind array
  (resize-only, persists until next prep).

### 11.2 "fcn.004556e0" never existed as an entry
- VA `0x4556e0` is a `call 0x4da420` instruction **mid-function**; that
  explains every failed caller search. True entry of the second-stage
  executor = `0x455650` (prologue, e8/e9 byte-scan: exactly **1 direct
  caller at `0x45413e`**). Its dispatch chain holds the verified arms
  (210000..210041) and bands 498/499/421/10001/111000/250000/497488.
- Caller context (`0x454132-0x45413e`): publishes thread-state to
  globals then `call 0x455650(state)` -> **bool in al**. Tick gates:
  `[0xaa6a80] == 0 -> exit`; `state->0x80c == 0 -> (if state->0x00
  byte != 0 && state->0x04 == 0 then call 0x453705 cleanup) exit`.
- Thread-state members named via global mirrors + format string
  `"Ev%d Common%d Line%d DB..."` (`0x8151f8`): **+0x08 = Line** (mirror
  `[0xaa1774]`), **+0x14 = Ev id** (`[0xaa1778]`), **+0x18 = Common id**
  (`[0xaa36f4]`), +0x80c = task/container pointer, +0x89c = cached
  active record (`0x4da420(state->0x80c, Line)`).

### 11.3 Two-stage pipeline conclusion (replaces "requeue mystery")
- cid-121 pass = PREP ONLY in the first interp: writes idempotent
  binds (+0x44 = [target,v1,v2] cells from unchanged elems), nibble
  metadata (+0x34), rewrites elem0 to pseudo, **inner index advances
  past the record** (continue 0x43a63d). The ARM MATH executes in the
  0x455650 per-line executor, which walks the same container and reads
  the +0x44 cells at that moment.
- Prep idempotence (elems never mutated; binds/nibbles refilled
  identically) => wolfvm's flat single-pass inline exec_setvar is
  semantically equivalent; retry/queue mechanics are engine overhead.

### 11.4 Uncommitted vm.cpp + ev11 regression
- `wolfvm/src/vm.cpp` holds the engine-truth exec_setvar (all 10.3
  gates + full 210x matrix, division guard). Builds clean.
- Focused batch post-rewrite: ev1 now completes; ev11 NEW step-cap
  (ev11<->ev19 bounce, 512 warns). Artifacts: `$env:TEMP\cd11.txt`
  (full dump, cmds=383), `cd19.txt` via commondump.
- OPEN (was being arbitrated when session cut): flags 0x3000 family
  (f000 = 3, e.g. ev11#20 `[1600042,1600040,1600041,12288]`):
  dispatch bands *accept* 210003/13/23/33 but handler skips f000>=3
  and tick selector = 0x0b skip => RHS-div third digit appears to run
  NOWHERE in this pipeline; semantically "0x3000 = violation" like the
  other true skips, but re-verify arm/table bytes before trusting.
- Next: finish f000=3 verdict; rerun focused batch; then ask user for
  full-601 sweep go-ahead.

## 12. <=241 dispatch map + cid-122/123/124/125 decoders (byte-verified 2026-10-22)

### 12.1 The corrected <=241 dispatch (second interpreter, tick chain) - trust this table

Selector byte at `0x4573F0 + (cid+1)` (do the hex-add explicitly), JT
`0x457334` (47 entries; idx46 = skip arm `0x45730b`):

| cid | sel byte | arm | target / meaning |
|-----|----------|-----|------------------|
| 111 | 0x08 | 0x455973 | -> key-input-side sidestep (verified) |
| 112 | 0x09 | 0x455980 | -> verified |
| 113-120 | 0x2e | 0x45730b | skip |
| 121 | 0x0a | 0x45598D | stub, tail jmp 0x457317 |
| 122 | 0x0b | 0x45599A | call **0x45b670** (string/text executor, 12.3) |
| 123 | 0x0c | 0x4559A7 | call **0x45dd40** (key-input executor, 12.4) |
| 124 | 0x0d | 0x4559B4 | call **0x45de60** (SetVarEx query+writeback, 12.2) |
| 125 | 0x0e | 0x4559C1 | call **0x45fb00** (key-flags-zone applicator, 12.4) |
| 126 | 0x0f | 0x4559CE | -> 0x45fe40 |
| 197 | 0x23 | 0x455AD2 | verified |
| 198 | 0x24 | 0x455ADF | verified |
| 201/202 | 0x2e | 0x45730B | skip |
| 210 | 0x25 | JT1[37]=0x455AEC | call arm (known 210 -> 0x4631F0 path) |

TWO SLIP TRAPS that cost real hours; both re-verified byte-exact:
1. The old per-handler attributions were off by one cid for {123, 124,
   125}: 0x45dd40 belongs to 123 (key-input), 0x45de60 to 124
   (SetVariableEx), 0x45fb00 to 125 (key-flags zones). The 124 ceiling
   "key-flags notes" wolfvm previously printed came from the 0x45fb00
   read and were simply the wrong command's semantics.
2. The selector add is 0x4573F0 + (cid+1) in HEX (0x3F0 + 0x7D = 0x46D,
   not 0x469). Always hex-add before reading bytes.

### 12.2 cid 124 = 0x45de60, fully decoded (query family + arithmetic writeback)

`target(args[0]) <- old (args[0]'s cell value) q-op (query result)`,
where args[1] is the mode word:
- bit0 = variant type byte for 0x490fc0 (0 in ALL census records);
- bit 0x10 = indirect target (cell value is itself a token; census: 0
  uses - note-only in wolfvm);
- (>>8)&0xF = writeback selector, 9-JT @0x45face, arms 0x45f599,
  0x45f5e9, 0x45f650, 0x45f6b7 (x, int64 via 0x511420), 0x45f723 (div),
  0x45f784 (mod), 0x45f7e5 (max), 0x45f854 (min), 0x45f8c0 (abs of
  result, old unused); div/mod guard: result==0 -> 1;
- (>>12)&0xF - 1 = query family, 4-JT @0x45f9da (guard (u32)sel > 3 ->
  **0x45f548 = `mov al,1; ret`: no query, NO writeback at all**):
  1 = 0x45df6c picture/object property query: obj = resolve(args[2])
      via 0x4577e0; obj==0 => result -1; args[3] <= 0x11 -> 18-JT
      @0x45f9ea (picture getters 0x480b10/0x480c60 on 0xaab044/45,
      tile globals 0x897284, screen helpers 0x4831f0/0x483210 on
      0xaa97b0, inner 4-JT 0x45fa32 on word[obj+0x50]+1, object
      fields +0x24..+0x254, hit-test clause 0x45e414);
  2 = 0x45e46f resolves args[2]/args[3] as two operands (coords) and
      picks an 11-JT @0x45fa42 on (args[1]>>16)&0xF: map-chip /
      position hit-tests (0x487ac0/0x483230 on 0xaa97b0,
      0x44ebf0/0x447060 on 0xaa6ea8, chip-table walks 0x4db380 with
      attribute byte reads);
  3 = 0x45edb7: raw args[2] <= 23 -> 24-JT @0x45fa6e system globals
      (key-state bits 0xaa705a..f -> 0/1/2, archive walkers
      0xaab0f8/0xaab104 + 0x5890a0/0x585a80, running-common list
      0xaa5eec with id+500000 addends, msg globals, Win32/net
      introspection); >23 -> skip query (result stays 0);
  4 = 0x45ed72: resolve(args[2]), resolve(args[3]) -> 0x491040
      system-register lookup on container 0xaa96c0.
Census (654 records, all targets cself refs): flags 0x4000 x531
  (fam4 sysreg, assign), 0x1000 x53 (fam1 picture, assign),
  0x3000 x32 (fam3 globals, assign), 0x82000 x15 (fam2 map, assign),
  0x4100 x7 (fam4, add), 0x4600 x6 (fam4, max), 0x2000 x5 (fam2
  map, assign), 0x42000 x4 (fam2 map, sub), 0x52000 x1 (fam2 map,
  assign). NONE of the census hits the family-OOB no-op.
Headless model in vm.cpp exec_setvarex: fam1 uses the engine's own
obj-miss result (-1); fams 2/3/4 absent-subsystem -> result 0 with
notes; writeback arithmetic engine-true (all 9 cases); indirect bit +
variant-type bit note-only. Apples-to-apples sweep (12.5): this model
changes ZERO per-id verdict classes vs the previous approximation -
the 531 16384-records already wrote 0 (uninit cself[11] = 0 before).

### 12.3 cid 122 = 0x45b670, FULL frame decode (string/text executor)

Opening: two string accumulators constructed (0x4d5cb0 ctors, literals
0x8154a6/0x8154a7). (flags&0xF00) bands {0x100,0x600,0x800,0x900,0xa00,
0xb00} route to 0x45b7a6: bit 0x10 picks resolved-then-to-string
(0x490b90 + 0x490c80) vs raw-args[0]-to-string (0x490c80 direct), both
appended into accumulator 1 ([ebp-0x34]) via 0x4d5d40. 0x45ba9c = the
message-window one-shot init (global 0xaa6df4 latch -> [0xaa6844]=1,
clamped {2..500}, 0x590230/0x590410 calls, state+0x860/+0x884 set from
(args[1]&0x1000) products).

Then op = flags&0xF <= 3 -> 4-JT @0x45dd29, dwords
[0x45b8c4, 0x45b955, 0x45b9b2, 0x45ac9b]:
- op0 (0x45b8c4): plain, no extra text (x547 census uses);
- op1 (0x45b955): appends text of args[2] (0x490c80 to-string) into
  accumulator 2 ([ebp-0x58]) (x130);
- op2 (0x45b9b2): kind-check 0x43c9b0(0xaa8ee8, args[2]); valid ->
  resolve (0x490b90) then to-string, invalid -> to-string of the raw
  token; BOTH flavors append into accumulator 2 - the kind-check only
  changes the fetch mode (x14, incl. flags=2 DB-string ref 1017000035
  = ev212#151, flags 0x302 = ev30#29);
- op3 (JT entry 0x45ac9b): mid-instruction alias inside 0x490fc0's
  call at 0x45ac9a (operand bytes 21 63 03 00) = engine dead path;
  x0 census uses in all 584 events.

All op arms jmp the common label into the SECOND band-dispatch on
(args[1]&0xF00) at 0x45bf5a, 10 arms: 0x200/0x300 -> 0x45c050
(bracket-template eval over accumulator 2 - find/replace loops with
literal "[" 0x8154b0 and bracket-family texts 0x8154b2/b4/b6/b7; then
(args[1]&0xF)==1 insert path, ==2 second kind-check + resolve
0x490650/0x490b90), 0x400 -> 0x45c5bb, 0x500 -> 0x45c841, 0x600 ->
0x45cc2c, 0x700 -> 0x45d29a, 0x800 -> 0x45d738, 0x900 -> 0x45d799,
0xa00 -> 0x45d8b2, 0xb00 -> 0x45d9aa, default -> 0x45dab2.

Default arm 0x45dab2 = the COMMIT (byte-read 2026-10-22):
0x426500(5x0, state->+0x10/+0x18/+0x14, std::string(acc2), retptr);
acc2 += result (0x4d5d40); acc1 := acc2 (0x4d5dd0); [ebp-0x440] =
(flags&0x10) ? resolve(args[0]) : args[0]; kind-check
0x43c9b0(0xaa8ee8, args[0]) -> valid: 0x490920(state, args[0],
str(acc1)) = THE cid-124 cell-write API; invalid: 0x490650 direct
string write; dtors + return 0x45dc1c+. 0x426500 = the mid-chain text
transformer (5 zero params + state window cfg); its display side
effects are headless no-ops. Net engine effect the VM must model: the
final text lands in cell args[0]. This is the SINGLE commit point in
the whole decoded executor (no other 0x490920/0x490650 in it).

wolfvm model (exec_setstr): cell(args[0]) = eval_str(args[2]) for op0;
+= eval_str(args[2]) for op1/op2 (both append flavors); eval_str
covers both op2 fetch modes (>=1e9 -> DB string, token bands -> cell
text). 0x426500/window config = headless no-op. op >= 3 -> note+fail
(dead path, x0 census). Closes the former ev212/ev255/ev355 fails.

### 12.4 cid 123 = key-input (0x45dd40); cid 125 = key-flags zones (0x45fb00)

123: `flags & 0x1000` -> numeric-input store to thread state+0x888;
`flags & 0x80` -> wait modes on state+0x858/0x85c (0x42bab0/0x42bc90);
default core = 0x454380 (129 census records).
125: the 0xaa72fc-0xaa7303 / 0xaa7403-0xaa7410 key-flags zone
applicator (2 census records; the earlier wolfvm "124 key-flags"
model was this command's decode misattributed to 124). No 126/215
records exist; wolfvm notes-and-continues on both.

### 12.5 Fresh full sweep (0..583, fixed id range) - the class arbiter

Method: new binary (this turn's edits) vs baseline binary (edits
inverted, temp tree), SAME harness + classifier, loop 0..583.

**Current sweep (v4, 2026-10-22): op-2 acceptance + 499-stray tolerance**
- NEW:  OK=484/584 (82.9%), stepcap=87 (14.9%), reccap=13 (2.2%),
  other=0 (!).
- BASE: OK=480, stepcap=84, reccap=13, other=7.
- Per-id class diff = EXACTLY the 7 predicted ids, zero regressions:
  ev203/207/253/542 OTHER->OK (499 stray tolerance, 12.6);
  ev212/255/355 OTHER->STEPCAP (op-2 unblocked -> deeper wait-loop
  class, 12.3). Every id with a clean path now terminates in defined
  state (OK / cap-abort); no unmodeled-failure class remains.
- Standing estimates after this sweep: CommonEvent VM ~88-90%
  (kernel decoded+validated for every executed band; reccap = deep
  recursion needing stateful runtime; stepcap = waits/ticks needing
  the map/tick runtime, Stage 3). Whole Android port ~35-45%:
  MapEvent VM, tick/picture/NDK shells all 0%.

**Current sweep (v6, 2026-10-22): 171 double-pc-advance fix + native sweep**
- Native `wolfvm sweep` re-baseline (full 584 ids, DataBase+CDataBase
  loaded): OK=485, stepcap=86, reccap=13, other=0, gap=0.
- Per-id class diff vs the archived v4 baseline: EXACTLY ONE id —
  ev168 STEPCAP->OK (171 BreakLoop double-pc-advance fix, same
  double-advance family as the 210 callee fix). ev581 (600xxx
  early-terminate) stayed OK; OTHER stayed 0; zero regressions.
- Confirmations: sweep v7 (db_set grow fix applied) =
  OK=485/86/13/other0/gap0 IDENTICAL to v6 (0 per-id diffs,
  compare_sweep.ps1) — the grow-rescan removal is semantics-neutral
  as designed. Runtime 2429 s -> 198.6 s end-to-end. Artifact:
  analysis/temp/full_sweep_v7.txt.
- Perf records made while re-running: the sweep initially took >40 min
  end-to-end. Two hot-loop fixes came first (flat per-cid census array
  with RAII flush + note-cap guards; ~10x), then single-id timing
  exposed the real dominator: `db_set`'s write-grow rescanned every row
  of the target type on each grow, so row-append loops were quadratic —
  ev66 alone spent 430 s under the 2e6 step cap (210 callee loop,
  db-write operands over thousands of grown rows). Grow is now O(1) per
  write (allocate-on-write; holes still read 0, so observable semantics
  unchanged) — confirmed by the v7 zero-diff re-run above.
- Baseline artifacts: analysis/temp/full_sweep_v6.txt,
  analysis/temp/full_sweep_v7.txt.

Previous sweep (v3, same-session archive; superseded by v4):
OK=480/584 (82.2%), stepcap=84, reccap=13, other=7; per-id diff vs
its baseline = exactly ONE id (ev581 OTHER->OK - the 210-callee
600xxx noted no-op). Reconciliation with the old 1..584-loop numbers
(OK=478/step84/rec13/other9): baseline + ev0 (OK) + ev584 excluded
(id-not-found harness artifact) = 479/84/13/8 exactly. Stable.

### 12.6 ev86 "499 without cond" - root cause + engine-true tolerance (2026-10-22)

The four OTHER fails (203/207/253/542) all inherit ONE common: ev86
"the death" (285 cmds). Structure: #2 opens an ind-0 select
([cself 1 == 1]); its case-1 body sets cself[54]=1 then does
JumpLabel (213) to the label set at #15 - which sits INSIDE a later
nested select's body (ind-2). The jump-crossing re-enters the body
region with selects whose openers were skipped on that path; ev86's
body end then carries trailing 499s (#280 ind-1, #282 ind-0 terminal)
that on such paths have NO matching open cond. wolfvm's push/pop model
had exactly matched this shape: normal path stack = [root, B8(ind0),
C216(ind2)]; #278 pops C216, #280 (ind-1) has nothing left to match ->
hard error "499 without cond (ev86 cmd 280)".

Engine truth ( combining the §10.5 deferred-bind decode with the fact
that this event ships and runs): EVERY 11 pre-binds its own 401/420
set (same-indent scan bounded by 499/498) and terminates at its own
499 - so a 499 reached without a matching open cond closes NOTHING.
The engine tolerates those strays as no-ops; the erase-label jump
path crosses #22/#169/#280-class strays routinely.

wolfvm fix: 499 = exact-indent match pops with contents (unchanged);
no match -> note("499 stray tolerated") + continue (was a hard error).
"Pop the topmost Cond regardless of indent" was REJECTED: it would
kill selects that still own active code (no-op is the honest empty
close). 4 close-inherit fails -> OK.

## 13. MapData/*.mps decoded (machine-verified 2026-10-22)

Scope: 105 files in `Data\MapData\` (7 eventless). All 105 consumed
byte-exactly by the sweep AND by the native `wolfvm mpsinfo` parser
(`src/map_project.{h,cpp}`; CLI: `mpsinfo <file.mps> [-v]`); totals agree across the two independent implementations: 676 event records,
1028 pages, 8931 commands.

### 13.0 Map-side command census (aggregate of all 105 files)

Per-cid totals across every page (native `mpsinfo -c` loop; the input
for Stage-2 decode prioritization). Map pages are dominated by calls
into common events — the map-object kernel itself is thin here:

- 210 CommonEvent x1879, 0 Blank x1849, 121 SetVariable x1078,
  250 Database x890, 401 ChoiceCase x785, 499 BranchEnd x670,
  111 VariableCondition x668, 106 DebugMessage x270, 103 Comment x252,
  124 SetVariableEx x248, 173 EraseEvent x106, 140 Sound x92,
  122 SetString x38, 420 ElseCase x36, 180 Wait x27,
  213 JumpLabel x16, 212 SetLabel x10, 150 Picture x6, 201 Move x3,
  242 ChipOverwrite x3, 126 BanInput x2, 112 StringCondition x2,
  125 AutoInput x1. (sum ~7214 commands)

Cids beyond the CE-executed/decoded set: **173 EraseEvent x106**
(approx semantics: turn the owning map-event/page activation off;
needs the MapX object runtime to be modeled — Stage 3), plus
**126 BanInput x2 / 125 AutoInput x1** (input-mask toggles; presentation
band for a headless census run). Everything else already runs through
the CommonEvent kernel.

### 13.0.1 mpssweep — all-maps headless page sweep (2026-10-22)

Every page of all 105 maps run through the CommonEvent VM kernel by the
new `mpssweep` CLI (synthetic page records, real CommonEvent.dat merged
so page 210-callees resolve; DB pairs loaded; fresh state per page;
DB restore-on-write isolation like `sweep`). Log:
analysis/temp/mpssweep_all.txt. Runtime ~132 s end-to-end.

- **1028/1028 pages classified: OK=868 (84.4%), stepcap=35,
  reccap=125, other=0, gap=0.**
- Reading (approx-runner caveat: page activation conditions are NOT
  modeled — every page is run unconditionally, so wait-spin pages the
  engine might never activate show up as stepcap, and deep 210 call
  chains as reccap; both are the SAME fault families the CE sweep's
  87 stepcap / 13 reccap ids match — pages inherit them through their
  210 callees. The results are a coverage report, not engine-failures).
- no OTHER anywhere: every page terminates in a defined state — the
  kernel pipeline is closed over the whole map data side as well.
- Stage-2 milestone: the map data side now flows through the same
  verified VM kernel; the next increment is the MapX object model
  (page conditions + EraseEvent 173 + tick/picture runtime, Stage 3).



### 13.1 Layout (facts)

- Header: `[10 zero bytes]["WOLFM"]` plain record (no dat crypt),
  `[i32 0][u8 0][u32 100][u32 1381][u8 0]`, a NUL-terminated SJIS
  string (copyright, "なし" everywhere), then
  `[i32 layers][i32 width][i32 height][i32 extra]`
  (layers in {0,2,3,4}; extra 0..23).
- Chip planes: 3 planes x W x H x u32 tokens, stacked right after the
  header (plane count 3 is NOT gated by `layers`).
- Strings inside records: `[i32 len incl NUL][bytes]` (CE string form).
- Event tail after the planes: either single byte 0x66 (7 eventless
  maps) or record list terminated by 0x66:
  - record := `"6F 39 30 00 00"` marker
    + `[i32 id][str name][i32 x][i32 y][i32 n_pages][i32 unk4]`
    + n_pages pages + `[0x70]` end tag.
  - page := `[0x79][i32 condition][str graphic_file][5B head]
    [4B flags][4 x i32 chips][16B][10B][i32 n_cmds]
    [n_cmds commands][`03 00 x6` trailer][0x7A]`.
  - commands = the shared CE command grammar (`[u8 argc-1][i32 cid]
    [args][indent][n_str][strings][optional move/route tail]`).
- Count invariants proven on every record/page: head `n_pages` ==
  structurally parsed pages (676/676); `n_cmds` guards its stream;
  zero unaccounted bytes in any file.

### 13.2 Value observations (semantics still open)

- Record (x,y) = map-chip position (e.g. 1-9 door event at (11,17)).
- `condition` field: -1 x1015 / 0 x12 / 52 x1; the non-(-1) pages are
  graphic-bearing door-like pages (Map011_*, Map020_1, Map000).
- chips form `pngIndex*100000 + chipPos`: PNG10 = 1,000,000,
  SystemGraphic door = PNG11 (1,100,000 band), 2,000,008-type ids
  appear as chip[0] on event pages (2,000,000 band).
- `graphic_file`: "" on invisible events; real paths on visible ones
  ("BasicData/光.png", "SystemGraphic/door.png").
- 5-byte page head byte0: 0x02 x868 / 0x08 x158 / 0x04 x2 (868 pages
  carry 02); byte1 {00 x465, 01 x451, 02 x111, FF x1}; bytes 2..4
  constant FF/00 bytes. graphic_flags[0]: 0x20 x455, 0x21 x473,
  0x31 x48, 0x51 x34, 0x11 x18. 10-byte block is mostly
  `03 03 03 00 XX 02 00 00 00 00` with XX = 0x23 x687, 0x20 x282,
  0x25 x39, 0x21 x11, 0x24 x8, 0x27 x1.
- unknown16[0..3] as LE i32 ("i0"): 0 x654, 1 x191, 2 x51, 3 x48,
  4 x18, 30 x50, others rare ({5,6,10,20,40,120,999999} x1..8);
  singleton pages: head[1]=0xFF on 1-2 rec=0, i0=999999 on
  Map000 rec=1 (see §14 for the position correlation).
- Map004 cross-check: 6 events ("地図", 演出 x4, 演出消滅) at
  (0,0)..(0,5), 11 pages, 68 commands — matches the hexdump rows.

NOTE: the old "MapXXX.dat" idea is dead — all per-map data lives in
the .mps files; no separate per-map .dat events exist anywhere.

## 14. Stage-3 map runtime: data surface + open engine semantics (2026-10-22)

### 14.0 Instrument: `mpspagewalk <Data/MapData-dir>` (committed)

One machine-clean line per page plus aggregate histograms, native code
(a shell-side census of this data kept corrupting rows — the native
CLI is the sanctioned instrument). Run over all 105 files:
**676 records / 1028 pages** — matches the §12 byte-level baseline
exactly (decoder fully consumed). Full artifact:
`wolfvm/analysis/temp/mps_pagewalk_1.txt`.

### 14.1 Page-field facts (data-side, verified by census)

Per page the decoder exposes scalar `condition` plus 5B head, 4+5B
flags, 16B unknown16, 10B block10. Aggregates:

- `condition`: -1 x1015 / 0 x12 / 52 x1 — no rich condition
  structures exist in this data.
- unknown16[0..3] LE i32 ("i0") == page position k for **863/1028
  pages**. Mismatches concentrate in two shapes:
  - `(k=0, i0 in {1,2,3,4,5,6,10,20,...})` — first page of a record
    carrying a "step/counter" value instead of its index, and
  - `(k = n-1, i0 = 30)` — the last page of multi-page records.
- **Last-page tuple signature**: on every inspected multi-page
  record the final page is `{i0=30 (0x1E), b4(bytes)=0x23,
  graphic_flags[0]=0x31}` (seen in 1-1 rec7 k5, 1-1_1 rec5/rec7 k4,
  1-2 rec5 k5, rec6 k4, ...). 30 recurs across files; it is NOT the
  page index and NOT the command count of that page.
- `b4` (block10[4]) classes correlate with position: 0x20 pages are
  near-perfect i0==idx (internal pages incl. first); 0x23 x687
  mostly single-page records' page 0 plus all "last pages"; 0x21
  x11 all first pages; 0x24/0x25/0x27 minor classes.
- Singletons: 1-2 rec=0 has head[1]=0xFF; Map000 rec=1 carries
  i0=999999 (a Wolf-typical max constant). Two distinct pages,
  both flagged for targeted Game.exe-loader review.

### 14.2 Engine reading (OPEN — TODO verify)

None of the above values has an engine interpretation yet:

- (TODO verify) Game.exe map-event loader: which bytes feed the
  page-activation condition evaluator, and what the scalar
  `condition` (-1/0/52) means at runtime (e.g. -1 = "no condition",
  0/52 = variable/state references?).
- (TODO verify) page-selection order: classic Wolf picks the
  highest-numbered page whose conditions all hold; this dataset's
  all-(-1) pages make that model suspect (a door would render its
  last page), so the selection state must live elsewhere — likely in
  the EraseEvent (cid 173, x106 uses in map pages) lifecycle and/or
  the v16/b10 fields above.
- Until verified, any page-choice model in wolfvm must be marked
  (approx) and MUST NOT be pinned by selftest as engine truth.
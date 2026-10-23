# Chip cell + tileset census — 2026-10-23 (first real-data round)

Method: `analysis/pin_probe.cpp` (built with MinGW g++ 15.2.0, `-std=c++17`) run
against the locally restored game data (Stage-0 worktree of the data branch;
the data itself is NOT committed here — see `.gitignore`). Inputs:
`Data/BasicData/TileSetData.dat` + all 105 `.mps` maps. Raw output:
`analysis/chip_cell_census_all.txt`.

## TileSetData.dat (mode 3, 6 records, byte-exact)

- record 0 (`\x8B\xF3` = "empty"): placeholder set — one path,
  `MapChip/Image2.png`, which is absent from the decrypted set
  (46 other MapChip pngs present). Its only resolvable-sheet count is 0.
- records 1/2/4/5: no strings at all; 24-entry int block, values 0..11 seq.
- record 3 (`\x83f\x83t\x83H` = "default"): the **only populated set** —
  16 sheet paths (`m1..m16`, moriV13.png first), 456-entry int block
  = {0 x419, 15 x16, 16 x21}.

## Chip cell (u32) structure — all 105 maps, 1,276,560 nonzero cells

- bits 24..31: 0 everywhere (distinct=1).
- bits 16..23: only 16 distinct band codes:
  {0x1, 0x3, 0x4, 0x6, 0x7, 0x9, 0xA, 0xC, 0xD, 0xF, 0x12, 0x13, 0x15, 0x16, 0x18, 0x0}
  (counts: band0=0 is rare (6809 cells); band1 dominates at 1,096,258 cells).
- exactly **510 distinct low16 values**; each pairs 1:1 with exactly one band
  code (0 of 510 seen with a second code — the pair function is well-defined).
- band 0: lo range **[1, 426], dense 1-based** — matches the moriV13.png grid
  (320x2200 = 8x55 chips @40px = 440 slots, a few ids unused).
- other bands: lo ranges **overlap** and are **not monotone** in lo
  (e.g. band3 lo [3594, 7836] vs band6 lo [6998, 11228]); spans ~3.0k–4.4k,
  band-row step ~3.4k–3.6k; nearest-band starts 3594 / 6998 / 10378 / 13568 /
  17162 / 20363 / 23958 / 28538 / 34464 / 37867 / 41450 / 44640 / 49434 / 55030 /
  58422.
- global low16 range **[1, 62652]** — far beyond any flat 16-sheet catalogue,
  which refutes the "low16 = chip index" renderer MODEL outright.

## Sheet geometry (PNG dims)

- moriV13.png 320x2200 (8x55); remaining 15 sheets 40x200 (1x5) or 80x200 (2x5).
- every dimension is a multiple of 40 → **chip size 40 is data-confirmed**
  (was a heuristic), consistent with band-0 dense ids 1..440.

## Renderer MODELs refuted / replaced in v0.6

1. "low16 in use, 0=skip, 1-based flat index" → cell is a 24-bit bitfield;
   band 0 (first sheet) is renderable today; bands != 0 are skipped and counted
   (`band=` in the session status line) until the map-draw path decodes them.
2. "per-map tileset = record 0" → un-correlated: `(layer_count, extra)` over
   105 maps spans (0..4, 0..23) against 6 records; renderer now picks the first
   record with resolvable sheets (lands on record 3 "default" for this data).
3. "chip size heuristic" → confirmed 40 by all-sheet divisibility.

## Next decode target

`Game.exe` map-draw path (the exe is available locally via the data worktree)
to decode bands 1..24 and the low16 structure; then event sprite graphics,
message/picture cids, Oboe audio.

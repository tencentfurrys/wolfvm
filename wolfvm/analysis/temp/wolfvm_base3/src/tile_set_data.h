// TileSetData.dat decoder — factual 2.2x layout, clean-room.
// Verified byte-by-byte against the Game.exe loader chain
//   (0x48a5d0 "read BasicData/TileSetData.dat" -> 0x48ac20 per-record parser):
//   [00][57 00 00 4F 4C 00 46 4D 00][tag][i32 recordCount]
//     tag 0x0A: 0xCF (mode 1) | 0xD0 (mode 2) | 0xD1 (mode 3); other -> error box
//     0x0B..0x0E: i32 recordCount
//   per record (parse starts at 0x0F):
//     m0:        [i32 len][len bytes]          (len includes the trailing NUL)
//     m1..m8:    [i32 len][len bytes] x 8      (always present, engine loop 1 i=0..7)
//     mode 3:    m9..m16: [i32 len][len bytes] x 8 (engine loop 2 i=8..15)
//                modes 1-2: strings 9..16 are NOT in the file; the engine fills
//                them from a compiled-in constant (0x816c79 placeholder).
//     [0xFF]  tag before flag block (engine checks this one strictly)
//     [i32 n][n x u8]                     flag-byte block (engine member +0x1dc)
//     [0xFF]  tag before int block        (engine reads it unchecked; the parsed
//                                          file always carries 0xFF, we assert)
//     [i32 n][n x (i32 mode>=2 | u8 mode 1)]  int block (engine member +0x1ec)
//   terminator: [0xCF] right after the last record (engine errors otherwise)
//   reserved header bytes 0/3/9 must be zero (also 2/6 by the magic constant).
//
// Engine evidence (Eris Dysnomia Game.exe, VA 0x48a5d0 = 0x48ac20):
//   - header check compares buffer[1..9] against W,O,L,F + interleave zeros
//   - tag switches a global mode (0x89728c = 1/2/3 for CF/D0/D1)
//   - count memcpy n=4, loop i<count -> 0x48ac20(this' record i)
//   - 0x48ac20: i32 -> copy that many bytes (string m0); loops i=0..7 and
//     (mode>=3) i=8..15 read [i32 len][len bytes] strings stored at
//     record+28*i+0x1c (std::string stride 0x1c); mode<3 stores the constant.
//   - then [ebp-0x439] = u8, *must* be 0xFF; i32 count -> member +0x1dc
//     resized, one memcpy of 1 per element
//   - second u8 (no check), i32 count -> member +0x1ec, one memcpy of
//     4 (mode>=2) or 1 (mode==1) per element
//   - loader final: u8 must be 0xCF (the file's last byte).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "filecoder.h"

namespace wolf {

struct TileSetRecord {
    std::string name;                        // m0 — group label (SJIS), e.g. 空 / デフォ
    std::vector<std::string> strings;        // m1..  — chip image paths (+1 more block for mode 3)
    std::vector<uint8_t> flag_bytes;         // FF block 1, n x u8 (engine +0x1dc)
    std::vector<int32_t> flag_ints;          // FF block 2, n x i32 for mode>=2 (engine +0x1ec)
};

struct TileSetDataDat {
    uint8_t tag = 0;                         // 0xCF / 0xD0 / 0xD1 -> mode 1/2/3
    std::vector<TileSetRecord> records;
    uint8_t terminator = 0xCF;

    static void parse(Reader& r, TileSetDataDat& out, std::string& err);
    int mode() const { return tag == 0xCF ? 1 : tag == 0xD0 ? 2 : tag == 0xD1 ? 3 : 0; }
};

} // namespace wolf

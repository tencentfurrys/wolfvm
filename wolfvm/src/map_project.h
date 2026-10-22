// MapData/*.mps ("map project", magic WOLFM) decoder — factual 2.25 layout,
// clean-room implementation. Layout below is machine-verified: all 105 files of
// the target game are consumed byte-exactly, with count cross-validation
// (head page count == structurally parsed pages on every record; n_cmds guards
// each page's command stream; route tails count-guarded like CommonEvent).
//
//   header:
//     [10 bytes, all zero][magic "WOLFM"]
//     [u32 =0][u8 =0][u32 =100][u32 =1381][u8 =0]
//     [SJIS string, NUL-terminated, not length-prefixed]        (copyright/author)
//     [u32 layer_count][u32 width][u32 height][u32 extra]
//   chip planes: 3 planes x width x height x u32 (stacked; layer_count is a
//   separate field and does not gate the plane count)
//   event tail:
//     record := "6F 39 30 00 00" | i32 id | str name | i32 x | i32 y |
//               i32 page_count | i32 unk | page*page_count | 0x70
//     page   := 0x79 | i32 condition | str graphic_file | 5B | 4B | 4 x i32 chips |
//               16B | 10B | i32 n_cmds | commands | "03 00 .. 00" | 0x7A
//     file ends with 0x66; a map without events goes straight from planes to 0x66
//   strings are [u32 len (incl NUL)][len-1 bytes][NUL] — same as CommonEvent.
//   Command streams use the shared Command::read grammar (argc-prefixed cid
//   streams with optional move/route tails).
#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "filecoder.h"
#include "command.h"

namespace wolf {

struct MapEventPage {
    int32_t condition = 0;               // page condition: -1 observed x1015, 0 x12, 52 x1
    std::string graphic_file;            // SJIS path, "" when the event has no graphic
    std::vector<uint8_t> graphic_head;   // 5 bytes: 02/08, 00..02, FF, 00, 00..02
    std::vector<uint8_t> graphic_flags;  // 4 bytes: 0x20 / 0x21 observed
    std::array<int32_t, 4> chips{};      // chip codes; form: pngIndex*100000 + position
    std::vector<uint8_t> unknown16;      // 16 raw bytes (page flags/scope, semantics open)
    std::vector<uint8_t> block10;        // 10 bytes, e.g. 03 03 03 00 23 02 00 00 00 00
    std::vector<Command> commands;
};

struct MapEventRecord {
    int32_t id = 0;
    std::string name;                    // SJIS (in JP games)
    int32_t x = 0;                       // map-tile position
    int32_t y = 0;
    int32_t page_count = 0;              // validated == pages.size() on every record
    int32_t unknown4 = 0;                // observed 0
    std::vector<MapEventPage> pages;
};

struct MapProjectDat {
    std::vector<uint8_t> head;           // 10 zero bytes (verified)
    int32_t field_15 = 0;                // u32 after magic, observed 0
    int32_t field_19 = 0;                // u8, observed 0
    int32_t field_100 = 100;             // u32, observed 100
    int32_t field_1381 = 1381;           // u32, observed 1381 (0x565)
    int32_t field_28 = 0;                // u8, observed 0
    std::string copyright;               // NUL-terminated SJIS, not length-prefixed
    int32_t layer_count = 0;             // observed {0,2,3,4}
    int32_t width = 0;
    int32_t height = 0;
    int32_t extra = 0;                   // observed 0..23
    std::vector<std::vector<uint8_t>> planes;  // 3 planes, each width*height*4 bytes
    std::vector<MapEventRecord> events;
    uint8_t file_terminator = 0x66;

    static void parse(Reader& r, MapProjectDat& out, std::string& err);
};

} // namespace wolf

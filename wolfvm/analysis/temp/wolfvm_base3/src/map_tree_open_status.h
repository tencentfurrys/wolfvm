// MapTreeOpenStatus.dat decoder — empirical clean-room layout for this build:
//   [20 bytes head (all-zero observed)][i32 count][count × u8 open-flag (0/1)][terminator 0x01]
// count is coupled to MapTree.dat's node count (56 here).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "filecoder.h"

namespace wolf {

struct MapTreeOpenStatusDat {
    std::vector<uint8_t> head;   // observed: 20 zero bytes
    int32_t count = 0;
    std::vector<uint8_t> flags;  // 0/1 per map-tree node, index = node id
    uint8_t terminator = 0;      // observed: 0x01

    static void parse(Reader& r, MapTreeOpenStatusDat& out, std::string& err);
};

} // namespace wolf

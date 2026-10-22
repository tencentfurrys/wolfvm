// MapTree.dat decoder — empirical clean-room layout for this build (Wolf 2.25 lineage):
//   [10 bytes head (all-zero observed)][marker byte 0x90][i32 count][count × (i32 parent, i32 id)][terminator 0xBC]
// No strings; the tree is a flat (parent, id) adjacency table. Byte-offset error contract.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "filecoder.h"

namespace wolf {

struct MapTreeNode {
    int32_t parent = 0;  // -1 = virtual root parent
    int32_t id = 0;
};

struct MapTreeDat {
    std::vector<uint8_t> head;   // observed: 10 zero bytes
    uint8_t marker = 0;          // observed: 0x90
    int32_t count = 0;
    std::vector<MapTreeNode> nodes;
    uint8_t terminator = 0;      // observed: 0xBC

    static void parse(Reader& r, MapTreeDat& out, std::string& err);
};

} // namespace wolf

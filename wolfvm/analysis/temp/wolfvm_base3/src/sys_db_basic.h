// SysDataBaseBasic.dat decoder — empirical clean-room layout for this build:
//   [10 zero-byte head][u8 head marker 0xC0][i32 typeCount]
//   per type: [i32 F][F × i32 indexinfo][i32 rowCapacity][R × row]
//   row cells are ints-first (int fields in order, then string fields), indexinfo
//   >= 0x07D0 = string field, otherwise int field.
//   Final byte: 0xC0 terminator (same value as head marker = bookend). (Factory-reset system DB: mostly empty cells.)
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "filecoder.h"

namespace wolf {

struct SysDbBasicType {
    std::vector<int32_t> indexinfos;
    int32_t row_capacity = 0;
    std::vector<std::vector<int32_t>> int_rows;
    std::vector<std::vector<std::string>> str_rows;
};

struct SysDbBasicDat {
    std::vector<uint8_t> head;    // observed: 10 zero bytes
    uint8_t head_marker = 0;      // observed: 0xC0 (bookends the file with the terminator)
    int32_t type_count = 0;
    std::vector<SysDbBasicType> types;
    uint8_t terminator = 0;       // observed: 0xC0
    size_t size_consumed = 0;

    static void parse(Reader& r, SysDbBasicDat& out, std::string& err);
};

} // namespace wolf

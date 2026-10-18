// Wolf Database pair decoder (.project + .dat) — factual 2.2x layout, clean-room.
//   .project: i32 typeCount; per type: name, fields{ name }, data{ name }, description,
//             i32 field_type_list_size + bytes, then 4 count-prefixed per-field sections.
//   .dat:     [00][57 00 00 4F 4C 00 46 4D 00][C1], i32 typeCount (== project count),
//             per type: i32 -2 separator, i32 unknown, i32 fieldCount + i32 indexinfo each,
//             i32 rowCount + per row (int-cells then string-cells, classified by indexinfo).
//             Final byte 0xC1.
// Observed indexinfo ranges: INT_START = 0x03E8 (int), STRING_START = 0x07D0 (string).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "filecoder.h"

namespace wolf {

struct DbField {
    std::string name;
    int32_t indexinfo = 0;
    uint8_t project_type = 0;
    std::string unknown1;
    std::vector<std::string> string_args;
    std::vector<int32_t> args;
    int32_t default_value = 0;

    bool is_string() const { return indexinfo >= 0x07D0; }
    bool is_int() const { return !is_string(); }
    int32_t index() const { return is_string() ? indexinfo - 0x07D0 : indexinfo - 0x03E8; }
};

struct DbData {
    std::string name;
    std::vector<int32_t> int_values;
    std::vector<std::string> string_values;
};

struct DbType {
    std::string name;
    std::string description;
    std::vector<DbField> fields;
    std::vector<DbData> data;
    int32_t field_type_list_size = 0;
    int32_t dat_unknown1 = 0;
};

struct DatabasePair {
    std::vector<DbType> types;

    // Reads both files via filecoder::Reader; cross-validates counts and full consumption.
    static bool parse_pair(const std::vector<uint8_t>& project_bytes,
                           const std::vector<uint8_t>& dat_bytes,
                           DatabasePair& out, std::string& err);
};

} // namespace wolf

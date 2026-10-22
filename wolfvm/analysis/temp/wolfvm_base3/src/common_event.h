// CommonEvent.dat ('C' class) decoder — factual 2.2x layout, clean-room.
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "filecoder.h"
#include "command.h"

namespace wolf {

struct CommonEventRecord {
    uint8_t indicator_head = 0x8E;
    int32_t id = 0;
    int32_t unknown1 = 0;
    std::vector<uint8_t> unknown2;       // 7 bytes
    std::string name;                    // raw (SJIS in JP games)
    std::vector<Command> commands;
    std::string unknown11;
    std::string description;             // trailing text note

    // Fixed tail geometry after the last command:
    //  0x8F, 10 strings, 0x8F, 10 bytes, 0x8F, 10 string-arrays,
    //  0x8F, 10 int-arrays, 0x1D raw, 100 strings, 0x91, string,
    //  then optional 0x92 segment
    std::vector<std::string> tail_strings_10;
    std::vector<uint8_t> tail_bytes_10;
    std::vector<std::vector<std::string>> tail_string_arrays_10;
    std::vector<std::vector<int32_t>> tail_int_arrays_10;
    std::vector<uint8_t> tail_raw_0x1D;
    std::vector<std::string> tail_strings_100;
    std::string tail_string_91;
    // optional 0x92 segment:
    bool has_92 = false;
    std::string tail_string_92a;
    int32_t tail_int_92 = 0;
};

struct CommonEventDat {
    std::vector<CommonEventRecord> events;
    static void parse(Reader& r, CommonEventDat& out, std::string& err);
};

} // namespace wolf

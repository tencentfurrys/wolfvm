// Game.dat ('M' class) decoder — fields verified against shipped 2.2x-era data.
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "filecoder.h"

namespace wolf {

struct GameDat {
    std::vector<uint8_t> unknown1;          // leading byte array
    int32_t file_version = 0;               // global game-file-format version (>=9 → version string)
    std::string title;
    std::string magic_string;               // expected "0000-0000"
    std::vector<uint8_t> unknown2;
    std::string font;                       // main font name (file exists as <name>.ttf)
    std::string subfonts[3];
    std::string default_pc_graphic;         // fallback actor image
    std::string version;                    // editor string, e.g. "2.24"-lineage (only stored if file_version >= 9)
    uint32_t trailing_size = 0;             // equals filesize - 1
    std::vector<uint8_t> rest;

    static GameDat parse(Reader& r, std::string& err);
};

bool is_plain_or_decrypt(std::vector<uint8_t>& data, std::string& err);
}

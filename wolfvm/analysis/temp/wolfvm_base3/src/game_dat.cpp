#include "game_dat.h"
#include <cstdio>
#include <cstring>

namespace wolf {

bool is_plain_or_decrypt(std::vector<uint8_t>& data, std::string& err) {
    if (data.empty()) { err = "empty file"; return false; }
    if (data[0] == 0) return true;
    if (data.size() < 10 + 9) { err = "too small for crypt header"; return false; }
    uint8_t header[10];
    std::memcpy(header, data.data(), 10);
    std::vector<uint8_t> body(data.begin() + 10, data.end());
    Reader r(std::move(body));
    try { r.decrypt_entry(header); }   // reference decrypts the body only, header stays raw
    catch (const std::exception& e) { err = std::string("decrypt failed: ") + e.what(); return false; }
    std::memcpy(data.data() + 10, body.data(), body.size());
    return true;
}

GameDat GameDat::parse(Reader& r, std::string& err) {
    GameDat g;
    try {
        static const uint8_t kMagic[] = { 0x57, 0x00, 0x00, 0x4F, 0x4C, 0x00, 0x46, 0x4D, 0x00 };
        if (r.u8() != 0x00) { err = "missing leading 0x00"; return g; }
        for (uint8_t m : kMagic)
            if (r.u8() != m) { err = "magic mismatch"; return g; }
        // Note: the file's class byte ('M') is the 0x4D at magic index 8.
        g.unknown1   = r.bytes();
        g.file_version = r.i32();
        g.title      = r.str();
        g.magic_string = r.str();
        if (g.magic_string != "0000-0000") { err = "magic_string mismatch"; return g; }
        g.unknown2   = r.bytes();
        g.font       = r.str();
        for (auto& s : g.subfonts) s = r.str();
        g.default_pc_graphic = r.str();
        if (g.file_version >= 9) g.version = r.str();
        g.trailing_size = static_cast<uint32_t>(r.i32());
        g.rest = r.raw(r.remaining());
    } catch (const std::exception& e) {
        err = std::string("parse error: ") + e.what();
        return g;
    }
    err.clear();
    return g;
}

} // namespace wolf

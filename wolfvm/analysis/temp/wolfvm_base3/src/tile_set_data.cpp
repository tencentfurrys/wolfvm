#include "tile_set_data.h"
#include <cstdio>
#include <stdexcept>

namespace wolf {

namespace {

void expect_u8(Reader& r, uint8_t v, const char* what) {
    const uint8_t got = r.u8();
    if (got != v) {
        char e[3], g[3];
        std::snprintf(e, 3, "%02X", v);
        std::snprintf(g, 3, "%02X", got);
        throw std::runtime_error(std::string(what) + ": expected 0x" + e +
                                 ", got 0x" + g + " at offset " +
                                 std::to_string(r.pos() - 1));
    }
}

int32_t read_count(Reader& r, const char* what, int32_t cap) {
    const int32_t n = r.i32();
    if (n < 0) throw std::runtime_error(std::string(what) + ": negative count at offset " +
                                        std::to_string(r.pos() - 4));
    if (n > cap) throw std::runtime_error(std::string(what) + ": absurd count " +
                                          std::to_string(n) + " at offset " +
                                          std::to_string(r.pos() - 4));
    return n;
}

// One in-file string: [i32 len][len bytes], len includes the trailing NUL.
std::string read_record_string(Reader& r, const char* what) {
    std::string s = r.str();
    (void)what;
    return s;
}

void parse_record(Reader& r, TileSetRecord& rec, int mode, const char* ctx) {
    rec.name = read_record_string(r, "record name");

    const int string_blocks = (mode >= 3) ? 16 : 8;   // m1..m8 (+m9..m16 for mode 3)
    for (int i = 0; i < string_blocks; ++i)
        rec.strings.push_back(read_record_string(r, ctx));

    expect_u8(r, 0xFF, "flag-block FF tag");

    int32_t n = read_count(r, "flag_bytes count", 100000000);
    rec.flag_bytes.resize(static_cast<size_t>(n));
    for (auto& b : rec.flag_bytes) b = r.u8();

    expect_u8(r, 0xFF, "flag-int block FF tag");     // engine skips this check;
                                                     // parsed files always carry 0xFF

    n = read_count(r, "flag_ints count", 100000000);
    if (mode >= 2) {
        rec.flag_ints.resize(static_cast<size_t>(n));
        for (auto& v : rec.flag_ints) v = r.i32();
    } else {
        // mode 1 stores one byte per element; widen into i32s
        rec.flag_ints.resize(static_cast<size_t>(n));
        for (auto& v : rec.flag_ints) v = static_cast<int32_t>(r.u8());
    }
}

} // namespace

void TileSetDataDat::parse(Reader& r, TileSetDataDat& out, std::string& err) {
    try {
        static const uint8_t kMagic[9] = { 0x57, 0x00, 0x00, 0x4F, 0x4C, 0x00, 0x46, 0x4D, 0x00 };
        if (r.u8() != 0x00) throw std::runtime_error("missing leading 0x00");
        for (uint8_t m : kMagic)
            if (r.u8() != m) throw std::runtime_error("header magic mismatch");
        // reserved header bytes 1(0x57) 3 6 9 are inside kMagic; byte 0 was the
        // leading 0x00; the engine additionally requires 2/6/9 == 0 (all hold).

        out.tag = r.u8();
        if (out.tag != 0xCF && out.tag != 0xD0 && out.tag != 0xD1) {
            char g[3];
            std::snprintf(g, 3, "%02X", out.tag);
            throw std::runtime_error(std::string("bad record tag 0x") + g +
                                     " (want CF/D0/D1) at offset " +
                                     std::to_string(r.pos() - 1));
        }
        const int mode = out.mode();

        const int32_t count = read_count(r, "record count", 1000000);
        out.records.reserve(static_cast<size_t>(count));
        for (int32_t i = 0; i < count; ++i) {
            out.records.emplace_back();
            parse_record(r, out.records.back(), mode, "record");

            if (i == 0 && count > 0 && r.remaining() == 0)
                throw std::runtime_error("unexpected EOF inside records");
        }

        expect_u8(r, 0xCF, "terminator");
        if (!r.eof())
            throw std::runtime_error("trailing bytes after 0xCF terminator at offset " +
                                     std::to_string(r.pos()));
    } catch (const std::exception& e) {
        err = e.what();
    }
}

} // namespace wolf

#include "map_project.h"
#include <cstdio>
#include <stdexcept>

namespace wolf {

namespace {

[[noreturn]] void fail(const std::string& what, size_t offset) {
    throw std::runtime_error(what + " at offset " + std::to_string(offset));
}

void expect_u8(Reader& r, uint8_t v, const char* what) {
    const uint8_t got = r.u8();
    if (got != v)
        fail(std::string(what) + ": expected 0x" + ([&]{ char b[3];
             std::snprintf(b, 3, "%02X", v); return std::string(b); })() +
             ", got 0x" + ([&]{ char b[3];
             std::snprintf(b, 3, "%02X", got); return std::string(b); })(),
             r.pos() - 1);
}

int32_t read_count(Reader& r, const char* what, int32_t cap) {
    const int32_t n = r.i32();
    if (n < 0)
        fail(std::string(what) + ": negative count", r.pos() - 4);
    if (n > cap)
        fail(std::string(what) + ": absurd count " + std::to_string(n), r.pos() - 4);
    return n;
}

// SJIS string terminated by an actual NUL byte (not length-prefixed).
std::string read_cstring(Reader& r, const char* what) {
    std::string s;
    for (;;) {
        if (r.eof())
            fail(std::string(what) + ": unterminated string", r.pos());
        const uint8_t c = r.u8();
        if (c == 0x00) break;
        s.push_back(static_cast<char>(c));
    }
    return s;
}

void expect_zero_run(Reader& r, size_t n, const char* what) {
    for (size_t i = 0; i < n; ++i) {
        const uint8_t c = r.u8();
        if (c != 0x00)
            fail(std::string(what) + ": expected 0x00 at +" + std::to_string(i) +
                 ", got 0x" + ([&]{ char b[3];
                 std::snprintf(b, 3, "%02X", c); return std::string(b); })(),
                 r.pos() - 1);
    }
}

MapEventPage parse_page(Reader& r) {
    MapEventPage p;
    expect_u8(r, 0x79, "page start tag");
    p.condition = r.i32();
    p.graphic_file = r.str();
    p.graphic_head = r.raw(5);
    p.graphic_flags = r.raw(4);
    for (auto& c : p.chips) c = r.i32();
    p.unknown16 = r.raw(16);
    p.block10 = r.raw(10);

    const int32_t n_cmds = read_count(r, "page command count", 500000);
    p.commands.resize(static_cast<size_t>(n_cmds));
    for (auto& c : p.commands) {
        std::string cerr;
        c = Command::read(r, cerr);
        if (!cerr.empty())
            fail("page command: " + cerr, r.pos());
    }
    expect_u8(r, 0x03, "page trailer");
    expect_zero_run(r, 6, "page trailer");
    expect_u8(r, 0x7A, "page end tag");
    return p;
}

void parse_record(Reader& r, MapEventRecord& rec) {
    rec.id = r.i32();
    rec.name = r.str();
    rec.x = r.i32();
    rec.y = r.i32();
    rec.page_count = read_count(r, "event page count", 100000);
    rec.unknown4 = r.i32();
    rec.pages.reserve(static_cast<size_t>(rec.page_count));
    for (int32_t k = 0; k < rec.page_count; ++k)
        rec.pages.push_back(parse_page(r));
    // The head page count is authoritative; after the last page the only legal
    // byte is the record terminator 0x70 (the structural scan cross-checks this).
    expect_u8(r, 0x70, "record end tag");
}

} // namespace

void MapProjectDat::parse(Reader& r, MapProjectDat& out, std::string& err) {
    try {
        out.head = r.raw(10);
        for (uint8_t c : out.head)
            if (c != 0) fail("head: expected 10 zero bytes", 0);
        static const char kMagic[5] = { 'W', 'O', 'L', 'F', 'M' };
        for (char m : kMagic)
            expect_u8(r, static_cast<uint8_t>(m), "WOLFM magic");

        out.field_15 = r.i32();
        out.field_19 = r.u8();
        out.field_100 = r.i32();
        if (out.field_100 != 100) fail("header: expected field 100", 20);
        out.field_1381 = r.i32();
        if (out.field_1381 != 1381) fail("header: expected marker 1381", 24);
        out.field_28 = r.u8();
        out.copyright = read_cstring(r, "copyright");
        out.layer_count = r.i32();
        out.width = r.i32();
        out.height = r.i32();
        out.extra = r.i32();
        if (out.width < 0 || out.height < 0)
            fail("header: negative map size", 38);
        if (static_cast<uint64_t>(out.width) * static_cast<uint64_t>(out.height) > 100000000u)
            fail("header: absurd map size", 38);

        out.planes.resize(3);
        const size_t plane_bytes =
            static_cast<size_t>(out.width) * static_cast<size_t>(out.height) * 4;
        for (auto& plane : out.planes)
            plane = r.raw(plane_bytes);

        // Event tail. After the planes: either 0x66 (no events) or an event
        // record whose marker begins with 0x6F.
        uint8_t lead = r.u8();
        if (lead == 0x66) {
            out.file_terminator = lead;
            if (!r.eof()) fail("trailing bytes after 0x66", r.pos());
        } else {
            if (lead != 0x6F)
                fail("expected event marker or 0x66", r.pos() - 1);
            for (;;) {   // one iteration per event record; lead 0x6F is consumed
                expect_u8(r, 0x39, "event marker");
                expect_u8(r, 0x30, "event marker");
                expect_zero_run(r, 2, "event marker");
                out.events.emplace_back();
                parse_record(r, out.events.back());

                const uint8_t end = r.u8();
                if (end == 0x66) {
                    out.file_terminator = end;
                    if (!r.eof()) fail("trailing bytes after 0x66", r.pos());
                    break;
                }
                if (end != 0x6F)
                    fail("expected 0x66 or next event marker", r.pos() - 1);
            }
        }
        if (!r.eof())
            fail("file not fully consumed", r.pos());
    } catch (const std::exception& e) {
        err = e.what();
    }
}

} // namespace wolf

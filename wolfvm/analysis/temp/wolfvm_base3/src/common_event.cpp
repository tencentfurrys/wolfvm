#include "common_event.h"
#include <cstdio>
#include <stdexcept>

namespace wolf {

namespace {
uint8_t expect(Reader& r, uint8_t v, const char* what) {
    (void)what;
    const uint8_t got = r.u8();
    if (got != v) {
        char e[3], g[3];
        std::snprintf(e, 3, "%02X", v);
        std::snprintf(g, 3, "%02X", got);
        throw std::runtime_error(std::string(what) + ": expected 0x" + e +
                                 ", got 0x" + g + " at offset " +
                                 std::to_string(r.pos() == 0 ? 0 : r.pos() - 1));
    }
    return got;
}
} // namespace

void CommonEventDat::parse(Reader& r, CommonEventDat& out, std::string& err) {
    try {
        static const uint8_t kMagic[9] = { 0x57, 0x00, 0x00, 0x4F, 0x4C, 0x00, 0x46, 0x43, 0x00 };
        if (r.u8() != 0x00) throw std::runtime_error("missing leading 0x00");
        for (uint8_t m : kMagic)
            if (r.u8() != m) throw std::runtime_error("header magic mismatch");
        // after the class byte ('C') we consumed 9 magic bytes + class byte lands via r;
        // Account order: leading 0x00, then bytes 1..9 are 0x57,0x00,0x00,0x4F,0x4C,0x00,0x46,0x43,0x00,
        // then the 0x8F terminator, then the event-count int.
        expect(r, 0x8F, "CommonEvents terminator");
        const int32_t ev_count = r.i32();
        if (ev_count < 0) throw std::runtime_error("negative event count");
        if (ev_count > 1'000'000) throw std::runtime_error("absurd event count");
        out.events.resize(static_cast<size_t>(ev_count));
        for (auto& ev : out.events) {
            expect(r, 0x8E, "event header indicator");
            ev.id        = r.i32();
            ev.unknown1  = r.i32();
            ev.unknown2  = r.raw(7);
            ev.name      = r.str();
            const int32_t n_cmds = r.i32();
            if (n_cmds < 0) throw std::runtime_error("negative command count");
            ev.commands.resize(static_cast<size_t>(n_cmds));
            for (auto& c : ev.commands) {
                std::string cerr;
                c = Command::read(r, cerr);
                if (!cerr.empty())
                    throw std::runtime_error("event id " + std::to_string(ev.id) + ": " + cerr);
            }
            ev.unknown11  = r.str();
            ev.description = r.str();

            expect(r, 0x8F, "tail tag");
            // All tail sections are count-prefixed arrays; validate each count.
            auto read_count = [&r](int expected, const char* what) {
                const int32_t n = r.i32();
                if (n != expected) {
                    char e[3], g[9];
                    std::snprintf(e, 3, "%02X", expected);
                    std::snprintf(g, 9, "%d", n);
                    throw std::runtime_error(std::string(what) + ": expected count " + e +
                                             ", got " + g + " at offset " + std::to_string(r.pos() - 4));
                }
                return n;
            };
            read_count(10, "tail strings");
            ev.tail_strings_10.resize(10);
            for (auto& s : ev.tail_strings_10) s = r.str();
            read_count(10, "tail bytes");
            ev.tail_bytes_10.resize(10);
            for (auto& b : ev.tail_bytes_10) b = r.u8();
            read_count(10, "tail string arrays");
            ev.tail_string_arrays_10.resize(10);
            for (auto& arr : ev.tail_string_arrays_10) {
                const int32_t n = r.i32();
                if (n < 0) throw std::runtime_error("negative tab array size");
                arr.resize(static_cast<size_t>(n));
                for (auto& s : arr) s = r.str();
            }
            read_count(10, "tail int arrays");
            ev.tail_int_arrays_10.resize(10);
            for (auto& arr : ev.tail_int_arrays_10) {
                const int32_t n = r.i32();
                if (n < 0) throw std::runtime_error("negative int array size");
                arr.resize(static_cast<size_t>(n));
                for (auto& i : arr) i = r.i32();
            }
            ev.tail_raw_0x1D = r.raw(0x1D);
            ev.tail_strings_100.resize(100);
            for (auto& s : ev.tail_strings_100) s = r.str();

            expect(r, 0x91, "tail separator E");
            ev.tail_string_91 = r.str();

            const uint8_t next = r.u8();
            if (next == 0x92) {
                ev.has_92 = true;
                ev.tail_string_92a = r.str();
                ev.tail_int_92 = r.i32();
                expect(r, 0x92, "optional tail closer");
            } else if (next != 0x91) {
                throw std::runtime_error(
                    std::string("optional tail indicator unexpected: 0x") +
                    [](uint8_t b){ static char s[3]; std::snprintf(s,3,"%02X",b); return std::string(s); }
                    (next));
            }
        }
        expect(r, 0x8F, "final file terminator");
        if (!r.eof()) throw std::runtime_error("unconsumed trailing bytes: " + std::to_string(r.remaining()));
    } catch (const std::exception& e) {
        err = std::string("CommonEvents.parse: ") + e.what();
    }
}

} // namespace wolf

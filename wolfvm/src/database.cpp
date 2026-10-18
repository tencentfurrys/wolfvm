#include "database.h"
#include <cstdio>
#include <stdexcept>

namespace wolf {

namespace {

std::string u8x(uint8_t v) {
    char b[5];
    std::snprintf(b, 5, "%02X", v);
    return std::string(b);
}

void parse_project(Reader& r, DatabasePair& out, std::string& err) {
    try {
        const int32_t n_types = r.i32();
        if (n_types < 0) throw std::runtime_error("negative type count");
        if (n_types > 100000) throw std::runtime_error("absurd type count");
        out.types.resize(static_cast<size_t>(n_types));
        for (auto& t : out.types) {
            t.name = r.str();
            int32_t n = r.i32();
            if (n < 0) throw std::runtime_error("negative field count");
            t.fields.resize(static_cast<size_t>(n));
            for (auto& f : t.fields) f.name = r.str();

            n = r.i32();
            if (n < 0) throw std::runtime_error("negative data count");
            t.data.resize(static_cast<size_t>(n));
            for (auto& d : t.data) d.name = r.str();

            t.description = r.str();

            t.field_type_list_size = r.i32();
            if (t.field_type_list_size < 0)
                throw std::runtime_error("negative field-type list size");
            for (int32_t i = 0; i < t.field_type_list_size; ++i) {
                const uint8_t b = r.u8();
                if (i < static_cast<int32_t>(t.fields.size()))
                    t.fields[static_cast<size_t>(i)].project_type = b;
            }

            n = r.i32();
            if (n != static_cast<int32_t>(t.fields.size()))
                throw std::runtime_error("per-field unknown1 count " + std::to_string(n) +
                                         " != field count " + std::to_string(t.fields.size()));
            for (auto& f : t.fields) f.unknown1 = r.str();

            n = r.i32();
            if (n != static_cast<int32_t>(t.fields.size()))
                throw std::runtime_error("per-field string_args count mismatch");
            for (auto& f : t.fields) {
                int32_t m = r.i32();
                if (m < 0) throw std::runtime_error("negative string_args size");
                f.string_args.resize(static_cast<size_t>(m));
                for (auto& s : f.string_args) s = r.str();
            }

            n = r.i32();
            if (n != static_cast<int32_t>(t.fields.size()))
                throw std::runtime_error("per-field args count mismatch");
            for (auto& f : t.fields) {
                int32_t m = r.i32();
                if (m < 0) throw std::runtime_error("negative args size");
                f.args.resize(static_cast<size_t>(m));
                for (auto& a : f.args) a = r.i32();
            }

            n = r.i32();
            if (n != static_cast<int32_t>(t.fields.size()))
                throw std::runtime_error("per-field default count mismatch");
            for (auto& f : t.fields) f.default_value = r.i32();
        }
        if (!r.eof())
            throw std::runtime_error("unconsumed trailing bytes: " + std::to_string(r.remaining()));
    } catch (const std::exception& e) {
        err = std::string("Database.project.parse: ") + e.what();
    }
}

void parse_dat(Reader& r, DatabasePair& out, std::string& err) {
    try {
        if (r.u8() != 0x00) throw std::runtime_error("missing leading 0x00");
        static const uint8_t kMagic[9] = { 0x57, 0x00, 0x00, 0x4F, 0x4C, 0x00, 0x46, 0x4D, 0x00 };
        for (uint8_t m : kMagic)
            if (r.u8() != m) throw std::runtime_error("db dat magic mismatch");
        if (r.u8() != 0xC1) throw std::runtime_error("expected head terminator 0xC1");

        const int32_t n_types = r.i32();
        if (n_types != static_cast<int32_t>(out.types.size()))
            throw std::runtime_error("dat type count " + std::to_string(n_types) +
                                     " != project type count " + std::to_string(out.types.size()));

        for (auto& t : out.types) {
            if (r.i32() != -2)
                throw std::runtime_error("type separator != -2 at offset " + std::to_string(r.pos() - 4));
            t.dat_unknown1 = r.i32();

            int32_t n = r.i32();
            if (n != static_cast<int32_t>(t.fields.size()))
                throw std::runtime_error("dat field count " + std::to_string(n) +
                                         " != project field count " + std::to_string(t.fields.size()));
            for (auto& f : t.fields) f.indexinfo = r.i32();

            n = r.i32();
            if (n != static_cast<int32_t>(t.data.size()))
                throw std::runtime_error("dat row count " + std::to_string(n) +
                                         " != project row count " + std::to_string(t.data.size()));
            for (auto& d : t.data) {
                for (const auto& f : t.fields)
                    if (f.is_int()) d.int_values.push_back(r.i32());
                for (const auto& f : t.fields)
                    if (f.is_string()) d.string_values.push_back(r.str());
            }
        }
        const uint8_t end = r.u8();
        if (end != 0xC1)
            throw std::runtime_error("expected end terminator 0xC1, got 0x" + u8x(end) +
                                     " at offset " + std::to_string(r.pos() - 1));
        if (!r.eof())
            throw std::runtime_error("unconsumed trailing bytes: " + std::to_string(r.remaining()));
    } catch (const std::exception& e) {
        err = std::string("Database.dat.parse: ") + e.what();
    }
}

} // namespace

bool DatabasePair::parse_pair(const std::vector<uint8_t>& project_bytes,
                              const std::vector<uint8_t>& dat_bytes,
                              DatabasePair& out, std::string& err) {
    std::string perr;
    Reader pr(project_bytes);                    // project parsed first (defines schema)
    parse_project(pr, out, perr);
    if (!perr.empty()) { err = perr; return false; }

    Reader dr(dat_bytes);
    std::string derr;
    parse_dat(dr, out, derr);
    if (!derr.empty()) { err = derr; return false; }
    return true;
}

} // namespace wolf

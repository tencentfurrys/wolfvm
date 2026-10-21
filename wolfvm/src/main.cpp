// wolfvm v0.3 — Stage 1: dat primitives Game.dat + CommonEvent.dat decoders verified on real data.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#include <algorithm>

#include "common_event.h"
#include "database.h"
#include "filecoder.h"
#include "game_dat.h"
#include "map_tree.h"
#include "map_tree_open_status.h"
#include "sys_db_basic.h"
#include "tile_set_data.h"

static std::vector<uint8_t> read_file(const char* path) {
    FILE* f = std::fopen(path, "rb");
    if (!f) { std::fprintf(stderr, "cannot open %s\n", path); std::exit(2); }
    std::fseek(f, 0, SEEK_END);
    long sz = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> buf(static_cast<size_t>(sz));
    if (sz > 0 && std::fread(buf.data(), 1, buf.size(), f) != buf.size()) {
        std::fprintf(stderr, "short read on %s\n", path); std::exit(2);
    }
    std::fclose(f);
    return buf;
}

static int cmd_datinfo(const char* path) {
    auto data = read_file(path);
    std::string err;
    if (!wolf::is_plain_or_decrypt(data, err)) {
        std::fprintf(stderr, "ERROR: %s\n", err.c_str());
        return 1;
    }
    if (data.size() < 10) { std::fprintf(stderr, "ERROR: too small\n"); return 1; }
    const uint8_t* b = data.data();
    const bool magic_ok = (b[0] == 0x00 && b[1] == 0x57 && b[2] == 0x00 && b[3] == 0x00 &&
                           b[4] == 'O' && b[5] == 'L' && b[6] == 0x00 && b[7] == 0x46);
    if (!magic_ok) { std::fprintf(stderr, "ERROR: not a plain WOLF dat record\n"); return 1; }
    std::printf("%s\n  size      : %zu\n  dat-class : %c\n", path, data.size(), b[8]);
    return 0;
}

static int cmd_gameinfo(const char* path) {
    auto data = read_file(path);
    std::string err;
    if (!wolf::is_plain_or_decrypt(data, err)) {
        std::fprintf(stderr, "ERROR: %s\n", err.c_str());
        return 1;
    }
    wolf::Reader r(std::move(data));
    wolf::GameDat g = wolf::GameDat::parse(r, err);
    if (!err.empty()) { std::fprintf(stderr, "ERROR: %s\n", err.c_str()); return 1; }
    std::printf("%s\n", path);
    std::printf("  file_version         : %d\n", g.file_version);
    std::printf("  title                : %s\n", g.title.c_str());
    std::printf("  magic_string         : %s\n", g.magic_string.c_str());
    std::printf("  unknown1             : %zu bytes\n", g.unknown1.size());
    std::printf("  unknown2             : %zu bytes\n", g.unknown2.size());
    std::printf("  font                 : %s\n", g.font.c_str());
    std::printf("  subfonts             : %s, %s, %s\n",
                g.subfonts[0].c_str(), g.subfonts[1].c_str(), g.subfonts[2].c_str());
    std::printf("  default_pc_graphic   : %s\n", g.default_pc_graphic.c_str());
    std::printf("  version              : %s\n", g.version.c_str());
    std::printf("  trailing_size        : %u\n", g.trailing_size);
    std::printf("  rest                 : %zu bytes\n", g.rest.size());
    return 0;
}

static int cmd_commoninfo(const char* path, int show) {
    auto data = read_file(path);
    std::string err;
    if (!wolf::is_plain_or_decrypt(data, err)) {
        std::fprintf(stderr, "ERROR: %s\n", err.c_str());
        return 1;
    }
    const size_t total = data.size();
    wolf::Reader r(std::move(data));
    wolf::CommonEventDat ce;
    wolf::CommonEventDat::parse(r, ce, err);
    if (!err.empty()) { std::fprintf(stderr, "ERROR: %s\n", err.c_str()); return 1; }
    std::printf("%s\n", path);
    std::printf("  events : %zu (file %zu bytes fully consumed)\n", ce.events.size(), total);
    std::printf("  cid histogram:\n");
    const int histogram_ids[] = { 101,102,103,111,112,121,122,124,130,140,150,162,170,180,
                                  201,210,212,213,220,240,250,270,290,300,401,420,421,498,499 };
    for (int id : histogram_ids) {
        unsigned count = 0;
        for (const auto& e : ce.events)
            for (const auto& c : e.commands)
                if (c.cid == static_cast<uint32_t>(id)) ++count;
        if (count) {
            wolf::Command probe;
            probe.cid = static_cast<uint32_t>(id);
            const char* nm = probe.name();
            std::printf("    %6u x cid %3d  (%s)\n", count, id, nm ? nm : "?");
        }
    }
    if (show > 0) {
        int shown = 0;
        for (const auto& e : ce.events) {
            if (shown++ >= show) break;
            std::printf("  event %5d  cmds=%4zu\n", e.id, e.commands.size());
        }
    }
    return 0;
}

static void print_tree(const std::map<int32_t, std::vector<int32_t>>& children,
                       int32_t id, int level) {
    for (int i = 0; i < level; ++i) std::fputs("  ", stdout);
    if (id == -1)
        std::fputs("root\n", stdout);
    else
        std::printf("node %d\n", id);
    const auto c = children.find(id);
    if (c != children.end())
        for (const int32_t child : c->second)
            print_tree(children, child, level + 1);
}

static int cmd_maptree(const char* path, bool tree) {
    auto data = read_file(path);
    std::string err;
    if (!wolf::is_plain_or_decrypt(data, err)) {
        std::fprintf(stderr, "ERROR: %s\n", err.c_str());
        return 1;
    }
    const size_t total = data.size();
    wolf::Reader r(std::move(data));
    wolf::MapTreeDat t;
    wolf::MapTreeDat::parse(r, t, err);
    if (!err.empty()) { std::fprintf(stderr, "ERROR: %s\n", err.c_str()); return 1; }

    std::printf("%s\n", path);
    std::printf("  head      : 10 bytes, all-zero (verified)\n");
    std::printf("  marker    : 0x%02X\n", t.marker);
    std::printf("  nodes     : %d (file %zu bytes fully consumed)\n", t.count, total);
    std::printf("  terminate : 0x%02X\n", t.terminator);
    if (t.nodes.empty()) return 0;

    std::map<int32_t, int32_t> parent_of;
    std::map<int32_t, std::vector<int32_t>> children;
    for (const auto& n : t.nodes) {
        if (!parent_of.emplace(n.id, n.parent).second)
            std::fprintf(stderr, "warning: duplicate map id %d\n", n.id);
        children[n.parent].push_back(n.id);
    }
    int max_depth = 0;
    for (const auto& n : t.nodes) {
        int d = 0;
        int32_t p = parent_of[n.id];
        while (p != -1 && parent_of.count(p) > 0 && d <= static_cast<int>(t.nodes.size())) {
            p = parent_of[p];
            ++d;
        }
        if (d > max_depth) max_depth = d;
    }
    std::printf("  max depth : %d\n", max_depth);
    if (tree) print_tree(children, -1, 0);
    return 0;
}

static int cmd_openstatus(const char* path) {
    auto data = read_file(path);
    std::string err;
    if (!wolf::is_plain_or_decrypt(data, err)) {
        std::fprintf(stderr, "ERROR: %s\n", err.c_str());
        return 1;
    }
    const size_t total = data.size();
    wolf::Reader r(std::move(data));
    wolf::MapTreeOpenStatusDat s;
    wolf::MapTreeOpenStatusDat::parse(r, s, err);
    if (!err.empty()) { std::fprintf(stderr, "ERROR: %s\n", err.c_str()); return 1; }

    unsigned open = 0;
    for (const uint8_t f : s.flags) open += f;
    std::printf("%s\n", path);
    std::printf("  head      : 20 bytes, all-zero (verified)\n");
    std::printf("  flags     : %d (file %zu bytes fully consumed)\n", s.count, total);
    std::printf("  open=1    : %u / %u\n", open, static_cast<unsigned>(s.flags.size()));
    std::printf("  terminate : 0x%02X\n", s.terminator);
    std::printf("  open map ids:");
    for (size_t i = 0; i < s.flags.size(); ++i)
        if (s.flags[i]) std::printf(" %zu", i);
    std::printf("\n");
    return 0;
}

static int cmd_dbinfo(const char* project_path, const char* dat_path) {
    auto pdata = read_file(project_path);
    auto ddata = read_file(dat_path);
    wolf::DatabasePair db;
    std::string err;
    if (!wolf::DatabasePair::parse_pair(pdata, ddata, db, err)) {
        std::fprintf(stderr, "ERROR: %s\n", err.c_str());
        return 1;
    }

    size_t n_fields = 0, n_rows = 0, n_int_cells = 0, n_str_cells = 0, ftsz_sum = 0;
    for (const auto& t : db.types) {
        n_fields += t.fields.size();
        n_rows += t.data.size();
        for (const auto& d : t.data) {
            n_int_cells += d.int_values.size();
            n_str_cells += d.string_values.size();
        }
        ftsz_sum += static_cast<size_t>(t.field_type_list_size);
    }
    std::printf("%s + %s\n", project_path, dat_path);
    std::printf("  types          : %zu (project %zu B + dat %zu B fully consumed)\n",
                db.types.size(), pdata.size(), ddata.size());
    std::printf("  fields         : %zu\n", n_fields);
    std::printf("  data rows      : %zu\n", n_rows);
    std::printf("  cells          : %zu int + %zu string\n", n_int_cells, n_str_cells);
    std::printf("  ftsz sum       : %zu (field-type list bytes)\n", ftsz_sum);
    std::printf("  per type:\n");
    unsigned i = 0;
    for (const auto& t : db.types) {
        size_t fi = 0, fs = 0;
        for (const auto& f : t.fields) f.is_int() ? ++fi : ++fs;
        std::printf("    type %3u: len(name)=%2zu fields %3zu (int %3zu / str %3zu) rows %4zu\n",
                    i, t.name.size(), t.fields.size(), fi, fs, t.data.size());
        ++i;
    }
    return 0;
}

static int cmd_sysbasic(const char* path) {
    // SysDataBaseBasic.dat is a plain custom record (lead = 10 zero bytes), so the
    // game-dat plaintext probe is not applied here.
    auto data = read_file(path);
    const size_t total = data.size();
    wolf::Reader r(std::move(data));
    wolf::SysDbBasicDat s;
    std::string err;
    wolf::SysDbBasicDat::parse(r, s, err);
    if (!err.empty()) { std::fprintf(stderr, "ERROR: %s\n", err.c_str()); return 1; }

    size_t n_int_cells = 0, n_str_cells = 0, n_nonempty_str = 0, max_str = 0;
    for (const auto& t : s.types)
        for (size_t row = 0; row < t.int_rows.size(); ++row) {
            n_int_cells += t.int_rows[row].size();
            for (const auto& sv : t.str_rows[row]) {
                ++n_str_cells;
                if (!sv.empty()) {
                    ++n_nonempty_str;
                    if (sv.size() > max_str) max_str = sv.size();
                }
            }
        }
    std::printf("%s\n", path);
    std::printf("  head      : 10 bytes, all-zero + marker 0xC0 (verified)\n");
    std::printf("  types     : %d (file %zu B fully consumed)\n", s.type_count, total);
    std::printf("  cells     : %zu int + %zu string\n", n_int_cells, n_str_cells);
    std::printf("  terminator: 0x%02X\n", s.terminator);
    std::printf("  per type:\n");
    unsigned i = 0;
    for (const auto& t : s.types) {
        size_t fi = 0, fs = 0;
        for (const int32_t ix : t.indexinfos) (ix >= 0x07D0 ? ++fs : ++fi);
        std::printf("    type %2u: fields %2zu (int %2zu / str %2zu) rows %3d (capacity)\n",
                    i, t.indexinfos.size(), fi, fs, t.row_capacity);
        ++i;
    }
    std::printf("  strings   : %zu cells, %zu non-empty (values withheld), max len %zu\n",
                n_str_cells, n_nonempty_str, max_str);
    return 0;
}

static int cmd_tileset(const char* path, bool verbose) {
    auto data = read_file(path);
    std::string err;
    if (!wolf::is_plain_or_decrypt(data, err)) {
        std::fprintf(stderr, "ERROR: %s\n", err.c_str());
        return 1;
    }
    const size_t total = data.size();
    wolf::Reader r(std::move(data));
    wolf::TileSetDataDat tsd;
    wolf::TileSetDataDat::parse(r, tsd, err);
    if (!err.empty()) { std::fprintf(stderr, "ERROR: %s\n", err.c_str()); return 1; }

    std::printf("%s\n", path);
    std::printf("  head      : 10 bytes magic (WOLF / class 'M') verified\n");
    std::printf("  tag       : 0x%02X (mode %d)\n", tsd.tag, tsd.mode());
    std::printf("  records   : %d (file %zu bytes fully consumed)\n",
                static_cast<int>(tsd.records.size()), total);
    std::printf("  terminate : 0x%02X\n", tsd.terminator);
    unsigned i = 0;
    for (const auto& rec : tsd.records) {
        size_t str_bytes = 0;
        for (const auto& s : rec.strings) str_bytes += s.size();
        std::printf("    record %u (mode %d): strings %zu (+m0 %zu B), string bytes %zu, "
                    "flag bytes %4zu, flag ints %4zu / %zu B\n",
                    i, tsd.mode(), rec.strings.size(), rec.name.size(), str_bytes,
                    rec.flag_bytes.size(), rec.flag_ints.size(), rec.flag_ints.size() * 4);
        if (verbose) {
            std::printf("      m0 len %zu (incl NUL)\n", rec.name.size() + 1);
            for (size_t k = 0; k < rec.strings.size(); ++k)
                std::printf("      m%zu len %zu\n", k + 1, rec.strings[k].size() + 1);
        }
        ++i;
    }
    return 0;
}

int main(int argc, char** argv) {
    if (argc >= 3 && std::strcmp(argv[1], "datinfo") == 0)    return cmd_datinfo(argv[2]);
    if (argc >= 3 && std::strcmp(argv[1], "gameinfo") == 0)   return cmd_gameinfo(argv[2]);
    if (argc >= 3 && std::strcmp(argv[1], "commoninfo") == 0) {
        int show = (argc >= 5 && !std::strcmp(argv[3], "-n")) ? std::atoi(argv[4]) : 0;
        return cmd_commoninfo(argv[2], show);
    }
    if (argc >= 3 && std::strcmp(argv[1], "maptree") == 0) {
        const bool tree = (argc >= 4 && !std::strcmp(argv[3], "-t"));
        return cmd_maptree(argv[2], tree);
    }
    if (argc >= 3 && std::strcmp(argv[1], "openstatus") == 0)
        return cmd_openstatus(argv[2]);
    if (argc >= 4 && std::strcmp(argv[1], "dbinfo") == 0)
        return cmd_dbinfo(argv[2], argv[3]);
    if (argc >= 3 && std::strcmp(argv[1], "sysbasic") == 0)
        return cmd_sysbasic(argv[2]);
    if (argc >= 3 && std::strcmp(argv[1], "tileset") == 0) {
        const bool verbose = (argc >= 4 && !std::strcmp(argv[3], "-v"));
        return cmd_tileset(argv[2], verbose);
    }
    std::fprintf(stderr, "usage: wolfvm datinfo <file.dat> | gameinfo <Game.dat> | "
                         "commoninfo <CommonEvent.dat> [-n N] | maptree <MapTree.dat> [-t] | "
                         "openstatus <MapTreeOpenStatus.dat> | dbinfo <X.project> <X.dat> | "
                         "sysbasic <SysDataBaseBasic.dat> | tileset <TileSetData.dat> [-v]\n");
    return 1;
}

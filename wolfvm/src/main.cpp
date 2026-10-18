// wolfvm v0.3 — Stage 1: dat primitives Game.dat + CommonEvent.dat decoders verified on real data.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#include <algorithm>

#include "common_event.h"
#include "filecoder.h"
#include "game_dat.h"
#include "map_tree.h"

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
    std::fprintf(stderr, "usage: wolfvm datinfo <file.dat> | gameinfo <Game.dat> | "
                         "commoninfo <CommonEvent.dat> [-n N] | maptree <MapTree.dat> [-t]\n");
    return 1;
}

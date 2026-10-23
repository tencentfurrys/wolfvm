// throwaway band-census: validate "band = 0-based sheet index, lo = 1-based grid id"
// against every .mps of the restored data tree + TileSetData sheet capacities.
#include <cstdio>
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "../../src/filecoder.h"
#include "../../src/map_project.h"
#include "../../src/tile_set_data.h"

namespace fs = std::filesystem;

static std::vector<uint8_t> read_file(const char* p) {
    FILE* f = std::fopen(p, "rb");
    if (!f) { std::fprintf(stderr, "open fail %s\n", p); std::exit(2); }
    std::fseek(f, 0, SEEK_END); long sz = std::ftell(f); std::fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> b((size_t)sz);
    if (sz && std::fread(b.data(), 1, (size_t)sz, f) != (size_t)sz) { std::exit(3); }
    std::fclose(f);
    return b;
}

static void png_size(const std::string& p, int& w, int& h) {
    w = h = 0;
    auto b = read_file(p.c_str());
    if (b.size() < 24 || b[0] != 0x89 || b[1] != 'P') return;
    w = (b[16] << 24) | (b[17] << 16) | (b[18] << 8) | b[19];
    h = (b[20] << 24) | (b[21] << 16) | (b[22] << 8) | b[23];
}

int main(int argc, char** argv) {
    if (argc < 4) { std::fprintf(stderr, "usage: bandcensus <Data-root> <TileSetData.dat> <record-idx>\n"); return 1; }
    const std::string root = argv[1];
    auto ts = read_file(argv[2]);
    wolf::Reader tr(std::move(ts));
    wolf::TileSetDataDat tsd;
    std::string err;
    wolf::TileSetDataDat::parse(tr, tsd, err);
    if (!err.empty()) { std::fprintf(stderr, "tileset parse: %s\n", err.c_str()); return 1; }
    const int rec = std::atoi(argv[3]);
    if (rec < 0 || (size_t)rec >= tsd.records.size()) { std::fprintf(stderr, "rec oob\n"); return 1; }
    const auto& paths = tsd.records[(size_t)rec].strings;

    // per-sheet grid capacity at chip=40
    std::vector<long long> cap(paths.size(), 0);
    std::vector<int> sw(paths.size(), 0), sh(paths.size(), 0);
    for (size_t i = 0; i < paths.size(); ++i) {
        const std::string rel = paths[i];
        const std::string full = root + "/" + rel;
        int w = 0, h = 0; png_size(full, w, h);
        sw[i] = w; sh[i] = h;
        cap[i] = (long long)(w / 40) * (h / 40);
        std::printf("sheet %2zu %-24s %4dx%4d cap=%lld\n", i, rel.c_str(), w, h, cap[i]);
    }

    // sweep all maps — TRUE decode: v = page*100000 + chip (decimal composition)
    std::map<uint32_t, std::map<std::string, long long>> per_page;
    long long total = 0, zero_total = 0, maps_ok = 0, maps_bad = 0, chip_max = 0;
    std::map<uint32_t, std::map<uint32_t, long long>> page_chips;   // page -> chip -> count
    for (const auto& it : fs::directory_iterator(root + "/MapData")) {
        if (!it.is_regular_file() || it.path().extension() != ".mps") continue;
        auto data = read_file(it.path().string().c_str());
        wolf::Reader r(std::move(data));
        wolf::MapProjectDat mp;
        std::string e2;
        wolf::MapProjectDat::parse(r, mp, e2);
        if (!e2.empty()) { std::printf("ERR %s: %s\n", it.path().filename().string().c_str(), e2.c_str()); ++maps_bad; continue; }
        ++maps_ok;
        for (const auto& plane : mp.planes) {
            const uint32_t n = static_cast<uint32_t>(plane.size() / 4);
            const uint32_t* cells = reinterpret_cast<const uint32_t*>(plane.data());
            for (uint32_t ci = 0; ci < n; ++ci) {
                const uint32_t v = cells[ci];
                if (v == 0u) { ++zero_total; continue; }
                ++total;
                const uint32_t page = v / 100000u;
                const uint32_t chip = v % 100000u;
                auto& P = per_page[page];
                ++P["n"];
                if (chip > chip_max) chip_max = chip;
                if (P["chipmin"] == 0 || (long long)chip < P["chipmin"]) P["chipmin"] = chip;
                if ((long long)chip > P["chipmax"]) P["chipmax"] = chip;
                ++page_chips[page][chip];
            }
        }
    }
    std::printf("\n== census: %lld maps ok/%lld bad, cells %lld (zero %lld skip), max chip %lld\n",
                maps_ok, maps_bad, total, zero_total, chip_max);
    std::printf("page     cells    chips   chipMin  chipMax\n");
    for (const auto& kv : per_page) {
        const auto& P = kv.second;
        const long long n  = P.count("n")       ? P.at("n")       : 0;
        const long long cmn = P.count("chipmin") ? P.at("chipmin") : 0;
        const long long cmx = P.count("chipmax") ? P.at("chipmax") : 0;
        std::printf("%4u  %-9lld %-7lld %-8lld %-8lld\n",
                    kv.first, n, (long long)page_chips.at(kv.first).size(), cmn, cmx);
    }
    // top chips per page (the "fill" chips), first 4 pages
    for (const auto& pkv : page_chips) {
        if (pkv.second.size() > 12) continue;
        std::printf("page %u chips:", pkv.first);
        for (const auto& c : pkv.second) std::printf(" %u(x%lld)", c.first, c.second);
        std::printf("\n");
    }
    return 0;
}

// throwaway: raw chip-cell dump for one .mps to decode the u32 structure.
#include <algorithm>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include "../../src/filecoder.h"
#include "../../src/map_project.h"

static std::vector<uint8_t> read_file(const char* p) {
    FILE* f = std::fopen(p, "rb");
    if (!f) { std::fprintf(stderr, "open fail %s\n", p); std::exit(2); }
    std::fseek(f, 0, SEEK_END); long sz = std::ftell(f); std::fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> b((size_t)sz);
    if (sz && std::fread(b.data(), 1, (size_t)sz, f) != (size_t)sz) std::exit(3);
    std::fclose(f);
    return b;
}

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: bandmap <file.mps>\n"); return 1; }
    auto data = read_file(argv[1]);
    wolf::Reader r(std::move(data));
    wolf::MapProjectDat mp;
    std::string err;
    wolf::MapProjectDat::parse(r, mp, err);
    if (!err.empty()) { std::fprintf(stderr, "parse: %s\n", err.c_str()); return 1; }

    std::printf("size %ux%u planes=%zu\n", mp.width, mp.height, mp.planes.size());
    std::map<uint32_t, uint32_t> uniq;   // v -> count, over all planes
    for (const auto& plane : mp.planes) {
        const size_t n = plane.size() / 4;
        const uint32_t* cells = reinterpret_cast<const uint32_t*>(plane.data());
        for (size_t i = 0; i < n; ++i) ++uniq[cells[i]];
    }
    std::printf("unique values: %zu\n", (size_t)uniq.size());
    uint32_t shown = 0;
    for (const auto& kv : uniq) {
        if (shown++ >= 300) { std::printf("... (%zu more)\n", (size_t)(uniq.size() - 300)); break; }
        const uint32_t band = (kv.first >> 16) & 0xFFu;
        const uint32_t lo = kv.first & 0xFFFFu;
        std::printf("v=%08x  b=%3u lo=%6u (x%u)\n", kv.first, band, lo, kv.second);
    }
    return 0;
}

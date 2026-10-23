// throwaway: dump TileSetData records to resolve page->sheet binding.
#include <cstdio>
#include <string>
#include <vector>

#include "../../src/filecoder.h"
#include "../../src/tile_set_data.h"

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
    if (argc < 2) { std::fprintf(stderr, "usage: recdump <TileSetData.dat>\n"); return 1; }
    auto ts = read_file(argv[1]);
    wolf::Reader r(std::move(ts));
    wolf::TileSetDataDat tsd;
    std::string err;
    wolf::TileSetDataDat::parse(r, tsd, err);
    if (!err.empty()) { std::fprintf(stderr, "parse: %s\n", err.c_str()); return 1; }
    std::printf("mode=%d records=%zu\n", tsd.mode(), tsd.records.size());
    int global = 0;
    for (size_t i = 0; i < tsd.records.size(); ++i) {
        const auto& rec = tsd.records[i];
        std::printf("rec %zu: mode=%d flags=%zu ints=%zu strings=%zu\n",
                    i, tsd.mode(), rec.flag_bytes.size(), rec.flag_ints.size(), rec.strings.size());
        for (size_t j = 0; j < rec.strings.size(); ++j) {
            std::string s = rec.strings[j];
            // strip path prefix for readability
            const size_t sl = s.find_last_of('/');
            std::string base = sl == std::string::npos ? s : s.substr(sl + 1);
            std::printf("  g=%3d s[%zu] %s\n", global++, j, base.c_str());
        }
    }
    return 0;
}

// pin_probe.cpp — renderer-MODEL pinning probe (analysis tool, clean-room).
//   mode "ts"  <TileSetData.dat>  : dump per-record paths/sizes (escaped bytes)
//   mode "maps" <MapData dir>     : per-map header CSV + plane cell histograms
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "tile_set_data.h"
#include "map_project.h"

namespace fs = std::filesystem;
using namespace wolf;

static std::string esc(const std::string& s) {
    std::string o;
    char buf[8];
    for (unsigned char c : s) {
        if (c < 0x80 && c >= 0x20 && c != '"') { o += (char)c; continue; }
        if (c == 0) break;  // NUL-padded tails
        std::snprintf(buf, sizeof buf, "\\x%02X", c);
        o += buf;
    }
    return o;
}

static std::vector<uint8_t> rd(const std::string& p, std::string& err) {
    FILE* f = std::fopen(p.c_str(), "rb");
    if (!f) { err = "open failed"; return {}; }
    std::fseek(f, 0, SEEK_END);
    const long l = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> v((size_t)l);
    if (l > 0 && std::fread(v.data(), 1, v.size(), f) != (size_t)l) { err = "short read"; std::fclose(f); return {}; }
    std::fclose(f);
    return v;
}

static int probe_ts(const char* path) {
    std::string err;
    auto b = rd(path, err);
    if (!err.empty()) { std::printf("ERR %s: %s\n", path, err.c_str()); return 1; }
    Reader r(std::move(b));
    TileSetDataDat tsd;
    TileSetDataDat::parse(r, tsd, err);
    if (!err.empty()) { std::printf("ERR parse: %s\n", err.c_str()); return 1; }
    std::printf("records=%zu mode=%d term=0x%02X\n", tsd.records.size(), tsd.mode(), tsd.terminator);
    for (size_t i = 0; i < tsd.records.size(); ++i) {
        const TileSetRecord& rec = tsd.records[i];
        std::printf("rec %zu name=\"%s\"\n", i, esc(rec.name).c_str());
        std::printf("  strings(%zu):", rec.strings.size());
        for (const std::string& s : rec.strings) std::printf(" [%s]", esc(s).c_str());
        std::printf("\n  flags: bytes=%zu ints=%zu", rec.flag_bytes.size(), rec.flag_ints.size());
        // flag-byte run signature (first 12 values)
        std::printf("  fb[");
        for (size_t k = 0; k < rec.flag_bytes.size() && k < 12; ++k) std::printf("%d,", rec.flag_bytes[k]);
        std::printf("\b ]\n");
        // int-block summary: distinct values + max
        std::map<int32_t, int> hi;
        int32_t mx = -1;
        for (int32_t v : rec.flag_ints) { ++hi[v]; if (v > mx) mx = v; }
        std::printf("  ints: max=%d distinct=%zu top:", mx, hi.size());
        int shown = 0;
        for (auto& kv : hi) {
            if (shown++ == 6) break;
            std::printf(" %dx%d,", kv.first, kv.second);
        }
        std::printf("\b \n");
    }
    return 0;
}

static int probe_maps(const char* dir) {
    std::map<std::pair<int, int>, int> lx_hist;          // (layer_count, extra) histogram
    std::map<int32_t, int> chip_top;                     // global low16 histogram (top values)
    std::map<uint32_t, int> high16_hist;                 // global (v >> 16) histogram
    std::map<int32_t, int> b16_23;                       // bits 16..23 histogram
    std::map<int32_t, int> b24_31;                       // bits 24..31 histogram
    std::map<std::pair<int32_t, int32_t>, int> lo_hi;    // (top low16, high16) co-occurrence
    std::map<std::pair<int32_t, int32_t>, int> cell_hi;  // full (low16, high16) census
    int32_t global_max = 0, global_min_non0 = 0x7FFFFFF;
    long long nonzero_cells = 0, total_cells = 0;
    int n_maps = 0, n_err = 0;
    for (auto& e : fs::directory_iterator(dir)) {
        if (!e.is_regular_file() || e.path().extension() != ".mps") continue;
        ++n_maps;
        std::string err;
        auto b = rd(e.path().string(), err);
        if (!err.empty()) { std::printf("ERR rd %s\n", e.path().filename().string().c_str()); ++n_err; continue; }
        Reader r(std::move(b));
        MapProjectDat mp;
        MapProjectDat::parse(r, mp, err);
        if (!err.empty()) { std::printf("ERR parse %s: %s\n", e.path().filename().string().c_str(), err.c_str()); ++n_err; continue; }
        const int L = mp.layer_count, X = mp.extra;
        ++lx_hist[{L, X}];
        int32_t mx = 0, mn0 = 0x7FFFFFF;
        size_t nz = 0, nz16 = 0;
        for (const auto& pl : mp.planes) {
            const size_t n = pl.size() / 4u;
            total_cells += (long long)n;
            for (size_t k = 0; k < n; ++k) {
                const uint32_t v = (uint32_t)pl[k * 4] | ((uint32_t)pl[k * 4 + 1] << 8) |
                                   ((uint32_t)pl[k * 4 + 2] << 16) | ((uint32_t)pl[k * 4 + 3] << 24);
                if (v == 0) continue;
                const int32_t lo = (int32_t)(v & 0xFFFFu);
                ++nonzero_cells; ++nz;
                ++chip_top[lo];
                if ((v >> 16) != 0u) { ++nz16; ++high16_hist[v >> 16]; }
                ++b16_23[(int32_t)((v >> 16) & 0xFFu)];
                ++b24_31[(int32_t)((v >> 24) & 0xFFu)];
                ++cell_hi[{lo, (int32_t)(v >> 16)}];   // full (lo, hi) census
                if (lo > mx) mx = lo;
                if (lo < mn0) mn0 = lo;
            }
        }
        if (mx > global_max) global_max = mx;
        if (nz && mn0 < global_min_non0) global_min_non0 = mn0;
        std::printf("%-12s %4dx%-4d L%d X%3d e=%3zu nz=%6zu max=%6d min0=%6d nz16=%4zu\n",
                    e.path().filename().string().c_str(), mp.width, mp.height, L, X,
                    mp.events.size(), nz, mx, nz == 0 ? 0 : mn0, nz16);
    }
    std::printf("\n== aggregate ==\n");
    std::printf("maps=%d errs=%d cells(%zux? planes)=%.1fM nonzero=%lld\n",
                n_maps, n_err, total_cells / 1000000.0, nonzero_cells);
    std::printf("(layer_count,extra) histogram:\n");
    for (auto& kv : lx_hist) std::printf("  L%d X%d x%d\n", kv.first.first, kv.first.second, kv.second);
    std::printf("global chip (low16) range: [%d, %d]\n", global_min_non0 == 0x7FFFFFF ? 0 : global_min_non0, global_max);
    std::printf("cells with high16 set: %lld of %lld\n", nonzero_cells, total_cells);
    std::printf("\nbits 16..23 distinct=%zu:", b16_23.size());
    { int shown = 0;
    for (auto& kv : b16_23) { if (shown++ == 20) break; std::printf(" %d x%d,", kv.first, kv.second); } }
    std::printf("bits 24..31 distinct=%zu:", b24_31.size());
    { int shown = 0;
    for (auto& kv : b24_31) { if (shown++ == 10) break; std::printf(" %d x%d,", kv.first, kv.second); } }
    {
        std::printf("\b \n(low16,high16) co-occurrence (1:many test) distinct=%zu\n", lo_hi.size());
        std::map<int32_t, int> lo_span;
        for (auto& kv : lo_hi) ++lo_span[kv.first.first];
        int multi = 0;
        for (auto& kv : lo_span) if (kv.second > 1) ++multi;
        std::printf("  low16 values seen with >1 high16: %d of %zu\n", multi, lo_span.size());
        // monotone / band test over the full census
        std::map<int32_t, std::map<int32_t, int>> lo2hi;   // lo -> hi histogram (full)
        for (auto& kv : cell_hi) lo2hi[kv.first.first][kv.first.second] += kv.second;
        long long multi_full = 0; int last_hi = -1; bool mono = true;
        std::map<int32_t, std::pair<int32_t,int32_t>> band; // hi -> (lo_min, lo_max)
        for (auto& l : lo2hi) {
            if (l.second.size() > 1) ++multi_full;
            const int32_t h = l.second.begin()->first;
            auto& b = band[h]; if (b.first == 0) b = {l.first, l.first};
            b.first = std::min(b.first, l.first); b.second = std::max(b.second, l.first);
            if (last_hi >= 0 && h < last_hi) mono = false;
            last_hi = h;
        }
        std::printf("  FULL: distinct lo=%zu  lo with >1 hi=%lld  bands monotone-in-lo=%s\n",
                    lo2hi.size(), multi_full, mono ? "yes" : "NO");
        // per-band: distinct lo count + the sorted (lo -> hi) walk with deltas
        for (auto& b : band)
            std::printf("    hi=%2d : lo [%6d, %6d]\n", b.first, b.second.first, b.second.second);
        {
            std::printf("\n  per-band distinct-lo list (first 14 each):\n");
            for (auto& b : band) {
                std::printf("    hi=%2d n=%zu:", b.first, lo2hi.empty() ? 0 : [&] {
                    int n = 0;
                    for (auto& l : lo2hi) if (l.second.begin()->first == b.first) ++n;
                    return n;
                }());
                int k = 0;
                for (auto& l : lo2hi) {
                    if (l.second.begin()->first != b.first) continue;
                    if (k++ >= 14) break;
                    std::printf(" %d", l.first);
                }
                std::printf("\n");
            }
        }
        {
            std::printf("\n  sorted walk (lo, hi, count, delta):\n");
            long long prev = -9999;
            int row = 0;
            for (auto& l : lo2hi) {
                ++row;
                if (row < 55 || row > 176) continue;
                const int32_t h = l.second.begin()->first;
                long long cnt = 0;
                for (auto& c : l.second) cnt += c.second;
                std::printf("    %6d -> h%2d x%6lld  d=%lld\n", l.first, h, cnt, l.first - prev);
                prev = l.first;
            }
        }
    }
    std::printf("high16 values top:");
    int shown = 0;
    for (auto& kv : high16_hist) { if (shown++ == 8) break; std::printf(" 0x%X x%d,", kv.first, kv.second); }
    std::printf("\nchip low16 top values:");
    shown = 0;
    for (auto it = chip_top.rbegin(); it != chip_top.rend() && shown < 15; ++it, ++shown)
        std::printf(" %d x%d,", it->first, it->second);
    std::printf("\n");
    return 0;
}

int main(int argc, char** argv) {
    if (argc >= 3 && std::strcmp(argv[1], "ts") == 0) return probe_ts(argv[2]);
    if (argc >= 3 && std::strcmp(argv[1], "maps") == 0) return probe_maps(argv[2]);
    std::printf("usage: pin_probe ts|maps <path>\n");
    return 2;
}

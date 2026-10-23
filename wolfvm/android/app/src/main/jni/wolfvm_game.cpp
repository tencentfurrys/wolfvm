// wolfvm_game.cpp — map session implementation (approx/MODEL unless noted).
// Verified facts used: the byte-exact dat-layer + .mps decoders in ../src and
// the engine page-pred (0x482240/0x44B530). Everything else is tagged MODEL.
#include "wolfvm_game.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <android/log.h>

#define LOG_TAG "wolfvm"
#define ALOG(c, ...) ((void)__android_log_print(ANDROID_LOG_##c, LOG_TAG, __VA_ARGS__))
#define ALOGI(...) ALOG(INFO, __VA_ARGS__)
#define ALOGW(...) ALOG(WARN, __VA_ARGS__)
#define ALOGE(...) ALOG(ERROR, __VA_ARGS__)

namespace fs = std::filesystem;
namespace wolf {

static std::vector<uint8_t> read_file(const std::string& path, std::string& err) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) { err = "cannot open " + path; return {}; }
    std::fseek(f, 0, SEEK_END);
    const long sz = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> buf((size_t)sz);
    if (sz > 0 && std::fread(buf.data(), 1, buf.size(), f) != buf.size()) {
        std::fclose(f);
        err = "short read " + path;
        return {};
    }
    std::fclose(f);
    return buf;
}

// Path resolution: dat strings are plain ASCII-ish relative paths from the
// game root (MODEL; SJIS-only paths fall back to Data/-prefix probing).
static std::string resolve(const std::string& root, const std::string& rel) {
    const std::string clean = rel.empty() ? rel :
        (rel[0] == '/' ? rel.substr(1, rel.size() - 1) : rel);
    if (clean.empty()) return {};
    for (const char* base : { "", "Data/", "data/" }) {
        const std::string cand = root + "/" + base + clean;
        std::error_code ec;
        if (fs::exists(cand, ec)) return cand;
    }
    return {};
}

GameSession::OwnedDb GameSession::load_db_pair(const std::vector<std::string>& candidates) {
    OwnedDb od;
    for (const std::string& stem : candidates) {
        const std::string proj = resolve(deps_.root, stem + ".project");
        const std::string dat = resolve(deps_.root, stem + ".dat");
        if (proj.empty() || dat.empty()) continue;
        std::string e1, e2;
        auto p = read_file(proj, e1), d = read_file(dat, e2);
        if (!e1.empty() || !e2.empty()) continue;
        od.pair = std::make_unique<DatabasePair>();
        if (DatabasePair::parse_pair(p, d, *od.pair, e1)) { od.ok = true; break; }
        od.pair.reset();
    }
    return od;
}

int GameSession::detect_chip() {
    return 40;   // MODEL fallback while the engine chip-size decode is pending
}

std::string GameSession::load() {
    // 1) .mps under play (verified decoder; 105-map sweep used this path).
    std::string err;
    auto mps = read_file(deps_.map_path, err);
    if (!err.empty()) return err;
    Reader r(std::move(mps));
    MapProjectDat::parse(r, map_, err);
    if (!err.empty()) return "map parse: " + err;

    // 2) tilesets (verified decoder) — MODEL: first record is the map's set;
    //    the engine's per-map set link stays TODO verify.
    auto tsd_file = resolve(deps_.root, "Data/BasicData/TileSetData.dat");
    if (tsd_file.empty()) tsd_file = resolve(deps_.root, "BasicData/TileSetData.dat");
    if (!tsd_file.empty()) {
        auto tsd = read_file(tsd_file, err);
        if (err.empty()) {
            Reader rt(std::move(tsd));
            TileSetDataDat::parse(rt, tilesets_, err);
            if (!err.empty()) return "TileSetData: " + err;
        }
    }
    if (tilesets_.records.empty()) return "no TileSetData: " + (err.empty() ? "file missing" : err);

    // Pinned (Eris data census 2026-10): TileSetDat carries placeholder records
    // (empty strings, or a single blank-image path) plus the populated group —
    // the .mps header fields (layer_count/extra) do NOT link per-map tilesets
    // (checked across all 105 maps). The engine's link stays RE-open; until
    // then use the first record with resolvable sheets (target: record 3,
    // named デフォ, 16 sheets; system default 1-based strings m1..m16 order).
    std::vector<uint32_t> capacity;                    // per-sheet px capacity placeholder
    for (size_t i = 0; i < tilesets_.records.size() && chip_paths_.empty(); ++i) {
        deps_.tileset_index = (int)i;
        for (const std::string& rel : tilesets_.records[i].strings) {
            const std::string abs = resolve(deps_.root, rel);
            if (abs.empty()) { ALOGW("chip sheet missing: %s", rel.c_str()); continue; }
            chip_paths_.push_back(abs);
            capacity.push_back(0);
        }
    }
    if (chip_paths_.empty()) return "no tileset record has resolvable sheet files";

    // 4) database pairs (optional but cheap: same verified decoders).
    for (const char* stem : { "Data/BasicData/DataBase", "Data/BasicData/CDataBase",
                              "Data/BasicData/SysDatabase" }) {
        OwnedDb od = load_db_pair({ std::string(stem) });
        if (od.ok) owneds_.push_back(std::move(od.pair));
    }
    // CommonEvent.dat joins when the session-side consumer decodes land —
    // the sweep-side kernel already validated the CE stream itself.

    // 5) map geometry (verified .mps fields)
    const int w = map_.width, h = map_.height;
    if (w <= 0 || h <= 0) return "map has no planes";

    // 6) live object layer on the engine page-pred (verified worker).
    rt_ = std::make_unique<MapRuntime>(map_);
    tick_count_ = 0;
    return {};
}

bool GameSession::renderer_init(void* window, int32_t w, int32_t h) {
    renderer_.chip_size = detect_chip();
    renderer_.sheets.assign(chip_paths_.size(), ChipSheet{});
    return renderer_.init(window, w, h);
}

void GameSession::renderer_resize(int32_t w, int32_t h) {
    renderer_.resize(w, h);
    cam_x_ = std::min(cam_x_, (float)std::max(0, map_.width * renderer_.chip_size - w));
    cam_y_ = std::min(cam_y_, (float)std::max(0, map_.height * renderer_.chip_size - h));
}

bool GameSession::renderer_upload_sheet(int idx, const void* rgba, int w, int h) {
    return renderer_.upload_sheet(idx, rgba, w, h);
}

void GameSession::renderer_upload_done() {
    // Engine-true (data census 2026-10-23, all 105 maps): chip cell value =
    // page*100000 + chip (decimal composition). Page 0 = base sheet with
    // 1-based grid ids (1..426 vs cap 440); pages 1..16 = AT sheets listed
    // after moriV13 in the tileset record, chip = 4-digit autotile edge code
    // (fill = 4444). code->variant rule inside an AT sheet = Game.exe
    // map-draw path, RE-open; the renderer draws AT grid 0 meanwhile (MODEL).
    std::vector<std::vector<uint32_t>> planes32;
    planes32.reserve(map_.planes.size());
    for (const auto& bytes : map_.planes) {
        std::vector<uint32_t> p32(bytes.size() / 4u);
        for (size_t k = 0; k < p32.size(); ++k) {
            const size_t o = k * 4u;
            p32[k] = (uint32_t)bytes[o] | ((uint32_t)bytes[o + 1] << 8) |
                     ((uint32_t)bytes[o + 2] << 16) | ((uint32_t)bytes[o + 3] << 24);
        }
        planes32.push_back(std::move(p32));
    }
    renderer_.build_plane_meshes(planes32, map_.width, map_.height);
    atlas_ready_ = true;
}

void GameSession::renderer_shutdown() {
    atlas_ready_ = false;
    renderer_.shutdown();
}

void GameSession::tick_frame() {
    float dx, dy; uint32_t taps, longs, dbls;
    input_.consume_frame(dx, dy, taps, longs, dbls);
    if (renderer_.vpw > 0 && renderer_.vph > 0) {
        const float maxX = std::max(0.f, (float)(map_.width * renderer_.chip_size) - renderer_.vpw);
        const float maxY = std::max(0.f, (float)(map_.height * renderer_.chip_size) - renderer_.vph);
        cam_x_ = std::clamp(cam_x_ + dx, 0.f, maxX);
        cam_y_ = std::clamp(cam_y_ + dy, 0.f, maxY);
    }
    // Stage-3 object layer live tick (engine page-pred, verified worker) —
    // active_page recomputes each frame; taps/longs bind to the engine's input
    // namespace when those cids decode (TODO verify — gesture floor for now).
    (void)taps; (void)longs; (void)dbls;
    if (rt_) rt_->tick(tick_count_++, pred_);

    if (atlas_ready_)
        renderer_.draw(cam_x_, cam_y_, &first_frame_);
}

GameSession::Diag GameSession::diag() const {
    Diag d;
    d.map_w = map_.width; d.map_h = map_.height;
    d.chip = renderer_.chip_size;
    d.events = map_.events.size();
    size_t act = 0, erased = 0;
    if (rt_) {
        for (const auto& o : rt_->objects()) {
            if (o.erased) { ++erased; }
            else if (o.active_page >= 0) { ++act; }
        }
    }
    d.active_objects = act;
    d.erased_objects = erased;
    d.missing_cells = renderer_.missing_cells;
    d.oob_cells = renderer_.oob_cells;
    d.band_cells = renderer_.band_cells;
    d.sheets = renderer_.sheets.size();
    for (const auto& s : renderer_.sheets) if (s.ready) ++d.sheets_ready;
    d.tileset = deps_.tileset_index;
    d.rows_tileset = (int)tilesets_.records.size();
    d.map_loaded = map_.width > 0;
    d.atlas_ready = atlas_ready_;
    d.cam_x = cam_x_; d.cam_y = cam_y_;
    return d;
}

} // namespace wolf

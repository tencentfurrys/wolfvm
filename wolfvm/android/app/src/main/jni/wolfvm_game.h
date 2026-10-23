// wolfvm_game.h — headless-capable map session (Stage 4 round 2, approx/MODEL).
// Owns: dat layer load (CE/DB pairs/MapTree/TileSet), the .mps under play,
// the live engine page-pred object layer tick, the gesture camera, and the
// GLES renderer lifecycle on a dedicated thread.
#pragma once
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "database.h"
#include "map_project.h"
#include "map_runtime.h"
#include "tile_set_data.h"
#include "wolfvm_gles.h"
#include "wolfvm_input.h"

namespace wolf {

struct SessionDeps {
    std::string root;           // game data root (…/WolfVM-style tree)
    std::string map_path;       // absolute path of the .mps under play
    int tileset_index = 0;      // MODEL: which TileSetData record drives chips
};

class GameSession {
public:
    explicit GameSession(const SessionDeps& d) : deps_(d), pred_(vars_) {}
    ~GameSession() = default;   // thread lifecycle lives in the JNI Session

    // dat load (call once, no GL yet). Returns "" on success or the error.
    std::string load();   // dat layer; "" on success, else error text
    const std::vector<std::string>& chip_paths() const { return chip_paths_; }

    // renderer handoff (game thread side). window = ANativeWindow* (borrowed).
    bool renderer_init(void* window, int32_t w, int32_t h);
    void renderer_resize(int32_t w, int32_t h);
    bool renderer_upload_sheet(int idx, const void* rgba, int w, int h);
    void renderer_upload_done();      // builds meshes from the loaded map
    void renderer_shutdown();

    void tick_frame();                // one frame: input + objects + draw

    InputState& input() { return input_; }

    // diagnostics for the console overlay
    struct Diag {
        int map_w = 0, map_h = 0;
        int chip = 40;
        size_t events = 0, active_objects = 0, erased_objects = 0;
        size_t missing_cells = 0, oob_cells = 0, band_cells = 0;
        size_t sheets = 0, sheets_ready = 0;
        int tileset = 0, rows_tileset = 0;
        bool map_loaded = false, atlas_ready = false;
        float cam_x = 0, cam_y = 0;
    };
    Diag diag() const;

    void set_frame_gate(bool on) { atlas_ready_ = on; }

private:
    struct OwnedDb {
        std::unique_ptr<DatabasePair> pair;
        bool ok = false;
    };
    OwnedDb load_db_pair(const std::vector<std::string>& candidates);

    SessionDeps deps_;
    InputState input_;
    MapProjectDat map_;
    MapVarStore vars_;
    EnginePagePred pred_;                     // persistent (vars_ outlives it)
    std::unique_ptr<MapRuntime> rt_;          // built in load() (needs the map)
    TileSetDataDat tilesets_;
    std::vector<std::string> chip_paths_;     // resolved sheet files of the selected set
    Renderer renderer_;
    std::vector<DatabasePair*> dbs_;          // non-owning handles into owneds_
    std::vector<std::unique_ptr<DatabasePair>> owneds_;
    std::atomic<bool> atlas_ready_{ false };
    float cam_x_ = 0, cam_y_ = 0;
    uint64_t tick_count_ = 0;
    bool first_frame_ = true;
    std::string load_err_;

    static int detect_chip();                 // MODEL fallback engine-less default
};

} // namespace wolf

// map_runtime.h — Stage-3 HEADLESS map-object model (skeleton).
//
// STATUS CAVEAT (read first): the real WOLF MapX object lifecycle — page
// selection order, erase persistence window, event triggers — is NOT yet
// engine-decoded. Everything here is a MODEL shaped to the verified data
// facts (ENGINE_ANALYSIS §14) so the verified engine work will plug into
// it later. Model semantics are pinned by selftest as MODEL pins, never
// as engine claims; anything used by the CLI prints an (approx) banner.
//
// Data-verified inputs this stands on (see map_project.h / ENGINE_ANALYSIS):
//   - page scalar `condition`: -1 x1015 / 0 x12 / 52 x1 in the target data;
//   - per-page raw fields (v16 / block10 / head / flags) are structured but
//     engine-open (TODO verify Game.exe map-event loader).
#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>

#include "map_project.h"

namespace wolf {

// Pluggable page-condition predicate. The engine's true condition inputs
// (variable bank, flags, map state) are not wired yet (TODO verify), so
// implementations must document which approximation they make.
struct MapPagePred {
    virtual ~MapPagePred() {}
    // MODEL (approx, TODO verify Game.exe): whether page k of a n-page
    // record should count as satisfied at evaluation time `now`.
    virtual bool page_ok(const MapEventPage& p, size_t k, size_t n, uint64_t now) = 0;
};

// Default data-derived predicate: the scalar condition band is an open
// question, so treat cond == -1 ("no explicit condition datum", 1015/1028
// pages) as satisfied and cond != -1 (0 x12, 52 x1) as NOT satisfied —
// conservative: the anomalous singletons stay inert until decoded.
struct CondDefaultPred final : MapPagePred {
    bool page_ok(const MapEventPage& p, size_t, size_t, uint64_t) override {
        return p.condition == -1;
    }
};

// Page pick: highest index whose predicate holds wins; -1 when none.
// (approx) — classic Wolf/editor-documented top-down pick; Game.exe decode
// pending. NOTE: under CondDefaultPred this activates the LAST page of
// multi-page all-(-1) records (e.g. the 1-1 door would show its final
// page), which contradicts observed play — exactly the contradiction
// ENGINE_ANALYSIS §14.2 records; the model keeps it explicit.
inline int pick_page(const MapEventRecord& e, MapPagePred& pred, uint64_t now) {
    for (int k = static_cast<int>(e.pages.size()) - 1; k >= 0; --k)
        if (pred.page_ok(e.pages[static_cast<size_t>(k)], static_cast<size_t>(k),
                         e.pages.size(), now))
            return k;
    return -1;
}

// One map event object's runtime-visible MODEL state. `rec` points into
// the MapProjectDat the runtime was built from (that map must outlive it).
struct MapEventObj {
    const MapEventRecord* rec = nullptr;
    bool erased = false;      // cid 173 hook (approx; persistence model)
    int active_page = -1;     // model pick; -1 = none active
};

// Headless object layer: state only — no rendering, no triggering.
class MapRuntime {
public:
    explicit MapRuntime(const MapProjectDat& mp) : mp_(&mp) {
        for (const auto& e : mp.events) objs_.push_back(MapEventObj{&e, false, -1});
    }

    // Re-evaluate page selection for all objects. MODEL: erased stays
    // erased (sticky) — engine's un-erase window is TODO verify.
    void tick(uint64_t now, MapPagePred& pred) {
        for (auto& o : objs_) {
            if (o.erased) { o.active_page = -1; continue; }
            o.active_page = pick_page(*o.rec, pred, now);
        }
    }

    // cid 173 EraseEvent hook (approx): engine truth — "erase the running
    // event" and its persistence/re-arm semantics — is TODO verify.
    void erase(int32_t id) {
        for (auto& o : objs_)
            if (o.rec->id == id) { o.erased = true; o.active_page = -1; }
    }

    const std::vector<MapEventObj>& objects() const { return objs_; }
    const MapProjectDat& map() const { return *mp_; }

private:
    const MapProjectDat* mp_;
    std::vector<MapEventObj> objs_;
};

} // namespace wolf

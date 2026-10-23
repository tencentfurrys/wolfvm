// map_runtime.h — Stage-3 HEADLESS map-object model (skeleton).
//
// STATUS: page selection is now ENGINE-TRUE (0x44B530 scan + 0x482240
// condition evaluator, ENGINE_ANALYSIS §14.3). Still MODEL: erase
// persistence window, trigger dispatch, and the self-key = record id
// assumption (obj+0x24 is a constructor argument, loader not traced). Everything here is a MODEL shaped to the verified data
// facts (ENGINE_ANALYSIS §14) so the verified engine work will plug into
// it later. Model semantics are pinned by selftest as MODEL pins, never
// as engine claims; anything used by the CLI prints an (approx) banner.
//
// Data-verified inputs this stands on (see map_project.h / ENGINE_ANALYSIS):
//   - page scalar `condition`: -1 x1015 / 0 x12 / 52 x1 in the target data;
//   - per-page raw fields (v16 / block10 / head / flags) are structured but
//     engine-open (TODO verify Game.exe map-event loader) — NOTE (§12.7):
//     the >=1e6 argument band in cids 173/210 is a SELF-INDEXED event key
//     (evSlot*10 + sub), so 1e6-band values are IDs, never scalar options.
#pragma once

#include <cstdint>
#include <cstddef>
#include <map>
#include <vector>

#include "map_project.h"

namespace wolf {

// Variable reader the page predicate pulls condition operands from. The
// engine routes both operands through 0x43F0B0 (the generic value getter);
// a reader implementation stands in for that getter.
struct MapVarReader {
    virtual ~MapVarReader() {}
    virtual long long read(long long key) = 0;
};

// Minimal standalone reader: keys < 1e6 are literals (0x43F0B0 range
// ladder), every other key is a cell defaulting to 0. Map self vars live
// at 1e6 + self_key*10 + n (n < 10) after rebasing.
struct MapVarStore final : MapVarReader {
    std::map<long long, long long> cells;
    long long read(long long key) override {
        if (key >= 0 && key < 1000000) return key;
        auto it = cells.find(key);
        return it == cells.end() ? 0 : it->second;
    }
    void set(long long key, long long v) { cells[key] = v; }
    void set_self(int32_t self_key, int n, long long v) {
        cells[1000000LL + (long long)self_key * 10 + n] = v;
    }
};

// Pluggable page-condition predicate. `self_key` is the object's self-var
// key (Game.exe obj+0x24, the value 0x482240 receives as its argument).
struct MapPagePred {
    virtual ~MapPagePred() {}
    virtual bool page_ok(const MapEventPage& p, size_t k, size_t n,
                         int32_t self_key, uint64_t now) = 0;
};

// Legacy MODEL predicate (pre-decode): cond == -1 satisfied. Kept only so
// the old MODEL pins stay reproducible; superseded by EnginePagePred.
struct CondDefaultPred final : MapPagePred {
    bool page_ok(const MapEventPage& p, size_t, size_t, int32_t, uint64_t) override {
        return p.condition == -1;
    }
};

// ENGINE-TRUE page condition (Game.exe 0x482240, byte-read 2026-10-22):
//   for i in 0..3:
//     if !(op[i] & 1)            -> return true   (first disabled term ends
//                                                   the list: prefix semantics)
//     L = lhs[i]; if 1e6 <= L < 1.2e6:      L = 1e6 + slot*10 + L%10
//                 if 9.19e6 <= L < 9.2e6:   L = 9.1e6 + slot*10 + L%10
//     R = rhs[i]; same rebasing but only for 1.1e6 <= R < 1.2e6 (and 9.19e6 band)
//     a = get(L); b = get(R)                (0x43F0B0 both)
//     cmp (op>>4): 0 a>b 1 a>=b 2 a==b 3 a<=b 4 a<b 5 a!=b 6 (a&b)==rhs_raw
//     comparator > 6 -> false; any failed term -> false
//   all four enabled + passed -> true
struct EnginePagePred final : MapPagePred {
    explicit EnginePagePred(MapVarReader& rd) : rd_(rd) {}

    static long long rebase_lhs(long long v, long long slot) {
        if (v >= 1000000 && v < 1200000) return 1000000 + slot * 10 + v % 10;
        if (v >= 9190000 && v < 9200000) return 9100000 + slot * 10 + v % 10;
        return v;
    }
    static long long rebase_rhs(long long v, long long slot) {
        if (v >= 1100000 && v < 1200000) return 1000000 + slot * 10 + v % 10;
        if (v >= 9190000 && v < 9200000) return 9100000 + slot * 10 + v % 10;
        return v;
    }

    bool eval(const MapPageCond& c, int32_t slot) {
        for (const auto& t : c.terms) {
            if (!t.enabled()) return true;
            const long long a = rd_.read(rebase_lhs(t.lhs, slot));
            const long long b = rd_.read(rebase_rhs(t.rhs, slot));
            bool ok = false;
            switch (t.comparator()) {
                case 0: ok = a >  b; break;
                case 1: ok = a >= b; break;
                case 2: ok = a == b; break;
                case 3: ok = a <= b; break;
                case 4: ok = a <  b; break;
                case 5: ok = a != b; break;
                case 6: ok = (a & b) == (long long)t.rhs; break;   // raw rhs field
                default: ok = false; break;
            }
            if (!ok) return false;
        }
        return true;
    }

    bool page_ok(const MapEventPage& p, size_t, size_t, int32_t self_key, uint64_t) override {
        return eval(page_conditions(p), self_key);
    }

private:
    MapVarReader& rd_;
};

// Page pick (ENGINE-TRUE, Game.exe 0x44B530): objects whose erase byte
// (obj+0xBE) is set keep page -1; otherwise scan k = n-1 .. 0 and take the
// first page whose condition holds; -1 when none (object has no page).
inline int pick_page(const MapEventRecord& e, MapPagePred& pred, uint64_t now) {
    for (int k = static_cast<int>(e.pages.size()) - 1; k >= 0; --k)
        if (pred.page_ok(e.pages[static_cast<size_t>(k)], static_cast<size_t>(k),
                         e.pages.size(), e.id, now))
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

    // cid 173 EraseEvent hook. Engine truth (ENGINE_ANALYSIS §12.7,
    // byte-verified): handler 0x462DA0 = self-slot erase carrying TWO args
    // (slot key + fade param); sets the fade triple obj{0x250,0x254,0x258}
    // and the display word 0x44; the erased object fades per-tick
    // (0x44BAF0) until BYTE[0xBE]=1 + vtable callback; common-event
    // context is FORBIDDEN (user-visible message, no erase executed).
    // This MODEL: sticky erase with no fade phase (approx), (TODO verify
    // full persistence/re-arm window in the map loader session).
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

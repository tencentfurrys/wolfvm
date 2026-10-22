// Wolf VM — Stage 2 interpreter core for CommonEvent.dat command streams.
// Pure C++17, headless. Semantics cross-checked against Game.exe (Rizin) where
// possible; data-derived semantics are marked with (data) below.
#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#include "common_event.h"
#include "database.h"

namespace wolf {

// ---------------------------------------------------------------------------
// Cell — variant cell. CSELF slots (1600000+n) are shared between int and
// string banks (data): cid 250 writes a DB string into 1600005 (event 5) while
// cid 112 compares the same address as text; the engine converts by context.
// ---------------------------------------------------------------------------
struct Cell {
    long long i = 0;            // int copy (default 0)
    std::string s;              // string copy (default "")
    bool touched_i = false;
    bool touched_s = false;

    long long as_int() const;           // numeric view (parses string cells)
    std::string as_str() const;         // string view (formats int cells)
    void set_i(long long v);
    void set_s(const std::string& v);
};

// One common-event activation.
struct Frame {
    int32_t common_id = 0;
    size_t pc = 0;
    Cell cself[100];                            // 1600000+n (n < 100)
    std::vector<struct Ctx> branch;             // open condition/loop contexts

    // Return-value pair (engine state fields +0x8A0/+0x8A4). Set by the cid-210
    // bit24 call path (Game.exe 0x463777..0x4639ED) on the CALLER side; consumed
    // once, at the callee's completion (0x453B36):
    //   op   = raw caller-side operand (absolute cell key; 0 = no binding)
    //   slot = callee's declared return cell (record 0xBCC; CommonEvent.dat 0x92 tail)
    // Copy-back: slot%100 in [5,9] -> string write, else int write.
    struct RetBind {
        long long op = 0;
        int32_t slot = -1;
        bool active = false;
    } ret;
};

enum class CtxKind { Cond, LoopTimes, LoopOpen };

struct Ctx {
    CtxKind kind = CtxKind::Cond;
    size_t indent = 0;                          // opener command indent
    size_t open_pc = 0;                         // opener command index
    size_t start_pc = 0;                        // loops: first body command
    long long case_target = 0;                  // Cond: case that matched (0=none)
    bool body_done = false;                     // Cond: a body already ran
    long long remain = 0;                       // LoopTimes left
};

// Global state: cells outside the cself window + runtime databases.
// Address banks (engine: Game.exe 0x43f0b0 range ladder):
//   < 1600000            : normal ints/strings (also *literal constants* in args)
//   1600000+n  (n<100)   : cself of the executing common event
//   2000000..2999999     : system variable ints
//   3000000..3999999     : system strings
//   >= 1e9               : packed DB refs (Vm::eval_db)
struct GameState {
    std::map<long long, Cell> gvar;
    const DatabasePair* ldb = nullptr;          // DataBase      [1.1e9, 1.2e9)
    const DatabasePair* sdb = nullptr;          // SysDatabase   [1.0e9, 1.1e9)
    const DatabasePair* cdb = nullptr;          // CDataBase     [1.3e9, 1.4e9)
};

struct TraceSink {
    bool enabled = false;
    size_t lines = 0;
    void log(const std::string& s);
};

struct RunResult {
    bool ok = false;
    std::string err;
    size_t executed = 0;
    size_t steps = 0;
    size_t db_reads = 0;
    size_t db_writes = 0;
    std::map<uint32_t, size_t> cid_counts;
    std::vector<std::string> warnings;
};

// DB access record (cid 250 + packed-ref 121/111 targets).
struct DbOp {
    bool write = false;
    int32_t which = 0;      // 1=DataBase, 2=SysDatabase, 3=CDataBase
    int32_t type = 0, row = 0, field = 0;
    uint32_t flags = 0;
    long long written = 0;
    std::string written_s;
};

class Vm {
public:
    explicit Vm(GameState& st) : st_(st) {}
    void load(const CommonEventDat& ce) {
        ce_ = &ce;
        index_by_id_.clear();
        for (size_t k = 0; k < ce.events.size(); ++k)
            index_by_id_[ce.events[k].id] = k;
    }
    void set_trace(TraceSink* tr) { tr_ = tr; }
    void set_step_cap(size_t cap) { step_cap_ = cap; }   // harness knob (0 = default 2e6)
    void set_db_ops(std::vector<DbOp>* out) { dbops_ = out; }

    bool run(int32_t common_id, RunResult& out);

private:
    const CommonEventDat* ce_ = nullptr;
    GameState& st_;
    TraceSink* tr_ = nullptr;
    std::vector<DbOp>* dbops_ = nullptr;

    std::unordered_map<int32_t, size_t> index_by_id_;
    std::vector<Frame> stack_;
    std::vector<std::map<std::string, size_t>> labels_stack_;   // per-frame labels
    RunResult* res_ = nullptr;
    std::string spot_;      // last statement location for diagnostics
    size_t steps_ = 0;
    size_t step_cap_ = 2000000;
    uint32_t rng_seed_ = 0x1234567u;

    // cself size observed as 100 empty strings in the event tail (data).
    static constexpr size_t kCselfCap = 100;
    // string-arg slots land at cself[5+i] (engine band 15000000+id*100+5+i,
    // cid-210 handler 0x46359F/0x463640).
    static constexpr size_t kCselfRetStrBase = 5;

    const CommonEventRecord* ev(int32_t id) const;
    Frame& frame() { return stack_.back(); }
    Cell& gcell(long long addr) { return st_.gvar[addr]; }
    Cell& ccell(size_t n);
    Cell& cell_for(long long addr);             // bank resolution incl. cself
    bool is_cself(long long addr) const {
        return addr >= 1600000 && addr < 1600000LL + (long long)kCselfCap;
    }

    long long eval_int(long long addr);
    std::string eval_str(long long addr);
    Cell fetch_cell(long long addr);             // variant-preserving operand resolve
    DbOp eval_db(long long packed);             // >=1e9 ref -> cells
    bool db_get(const DbOp& o, Cell& out);
    bool db_set(const DbOp& o, const Cell& v);

    bool exec_setvar(const Command& c);
    bool exec_setvarex(const Command& c);
    bool exec_setstr(const Command& c);
    bool exec_cond(const Command& c, long long& case_target);   // 0 = none
    bool cond_true_int(const Command& c, long long& case_target);
    bool cond_case_str(const Command& c, long long& case_no);
    int  eval_cmp(long long lhs, long long rhs, int32_t op);
    int  eval_cmp_str(const std::string& lhs, const std::string& rhs, int32_t op);

    // Structure scanning (indent-tree; 2.2x files carry no skip counts)
    size_t find_next_token(size_t from, size_t indent);         // 401/420/499
    size_t find_tree_end(size_t from, size_t indent);           // 498
    size_t fwd_label(size_t from, const std::string& name) const;

    bool db_cell_target(const DbOp& o) const;                   // type/row sanity

    void note(const std::string& s);
    void tlog(const std::string& s);
    long long rand_ll(long long lo, long long hi);
};

} // namespace wolf

// Wolf VM — Stage 2 interpreter implementation. Pure C++17, headless.
#include "vm.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace wolf {

// ---------------------------------------------------------------------------
// Cell
// ---------------------------------------------------------------------------
long long Cell::as_int() const {
    if (touched_i) return i;
    if (touched_s) {
        size_t p = 0;
        bool neg = false;
        if (p < s.size() && (s[p] == '-' || s[p] == '+')) { neg = (s[p] == '-'); ++p; }
        long long v = 0;
        for (; p < s.size() && s[p] >= '0' && s[p] <= '9'; ++p) v = v * 10 + (s[p] - '0');
        return neg ? -v : v;
    }
    return 0;
}
std::string Cell::as_str() const {
    if (touched_s) return s;
    if (touched_i) {
        char b[32];
        std::snprintf(b, sizeof(b), "%lld", i);
        return std::string(b);
    }
    return std::string();   // untouched: empty (engine default)
}
void Cell::set_i(long long v) { i = v; touched_i = true; }
void Cell::set_s(const std::string& v) { s = v; touched_s = true; }

void TraceSink::log(const std::string& s) {
    if (!enabled) return;
    ++lines;
    std::fputs(s.c_str(), stdout);
    std::fputc('\n', stdout);
}
// ---------------------------------------------------------------------------
// Vm helpers
// ---------------------------------------------------------------------------
void Vm::note(const std::string& s) {
    if (res_ && res_->warnings.size() < 512) res_->warnings.push_back(spot_.size() ? spot_ + " " + s : s);
}
void Vm::tlog(const std::string& s) {
    if (tr_) tr_->log(s);
}
long long Vm::rand_ll(long long lo, long long hi) {
    if (hi <= lo) return lo;
    rng_seed_ = rng_seed_ * 1664525u + 1013904223u;
    long long span = hi - lo + 1;
    return lo + (long long)((rng_seed_ >> 8) % (uint32_t)span);
}

const CommonEventRecord* Vm::ev(int32_t id) const {
    auto it = index_by_id_.find(id);
    if (it == index_by_id_.end()) return nullptr;
    return &ce_->events[it->second];
}

Cell& Vm::ccell(size_t n) {
    if (stack_.empty()) { static Cell dummy; return dummy; }
    if (n >= kCselfCap) {
        note("cself index beyond cap: 1600000+" + std::to_string(n));
        n = kCselfCap - 1;
    }
    return stack_.back().cself[n];
}

Cell& Vm::cell_for(long long addr) {
    if (is_cself(addr)) return ccell((size_t)(addr - 1600000));
    return gcell(addr);
}

// ---------------------------------------------------------------------------
// DB access (engine-verified packed-ref decode at Game.exe 0x43f9c9):
//   [1.0e9,1.1e9)  SysDatabase · [1.1e9,1.2e9) DataBase · [1.3e9,1.4e9) CDataBase
//   type = (v % base) / 1000000 · row = (v % 1000000) / 100 · field = v % 100
// ---------------------------------------------------------------------------
DbOp Vm::eval_db(long long packed) {
    DbOp o{};
    long long base = 0, which = 0;
    // Engine-verified (resolver 0x43a240 disasm): 1.0e9 -> 0xaa75f0 DataBase,
    // 1.1e9 -> 0xaa9498 CData, 1.3e9 -> 0xaa7670 Sys. Decoded bands:
    // [1e9,1.1e9) [1.1e9,1.2e9) [1.3e9,1.4e9); rem: type*1e6 + row*100 + field.
    if (packed >= 1000000000LL && packed < 1100000000LL)      { which = 1; base = 1000000000LL; }
    else if (packed >= 1100000000LL && packed < 1200000000LL) { which = 3; base = 1100000000LL; }
    else if (packed >= 1300000000LL && packed < 1400000000LL) { which = 2; base = 1300000000LL; }
    else {
        note("db ref outside known bases: " + std::to_string(packed));
        return o;   // which == 0
    }
    long long rem = packed % base;
    o.which = (int32_t)which;
    o.type  = (int32_t)(rem / 1000000);
    o.row   = (int32_t)((rem % 1000000) / 100);
    o.field = (int32_t)(rem % 100);
    return o;
}

bool Vm::db_get(const DbOp& o, Cell& out) {
    const DatabasePair* p = (o.which == 1) ? st_.ldb : (o.which == 2) ? st_.sdb
                            : (o.which == 3) ? st_.cdb : nullptr;
    if (!p) { note("db_get: database not loaded (which=" + std::to_string(o.which) + ")"); return false; }
    if (o.type < 0 || o.type >= (int32_t)p->types.size()) {
        note("db_get: type out of range " + std::to_string(o.type));
        return false;
    }
    const DbType& t = p->types[(size_t)o.type];
    if (o.row < 0 || o.row >= (int32_t)t.data.size()) {
        note("db_get: row out of range (type " + std::to_string(o.type) + " row " + std::to_string(o.row) + ")");
        return false;
    }
    const DbData& d = t.data[(size_t)o.row];
    int32_t fi = o.field;
    if (fi < 0 || fi >= (int32_t)t.fields.size()) {
        note("db_get: field out of range (type " + std::to_string(o.type) + " fields " +
             std::to_string(t.fields.size()) + " field " + std::to_string(fi) +
             " flags " + std::to_string(o.flags) + " which " + std::to_string(o.which) + ")");
        return false;
    }
    if (t.fields[(size_t)fi].is_string()) {
        int32_t idx = t.fields[(size_t)fi].index();
        out.set_s(idx >= 0 && idx < (int32_t)d.string_values.size() ? d.string_values[(size_t)idx] : std::string());
    } else {
        int32_t idx = t.fields[(size_t)fi].index();
        out.set_i(idx >= 0 && idx < (int32_t)d.int_values.size() ? d.int_values[(size_t)idx] : 0);
    }
    ++res_->db_reads;
    return true;
}

bool Vm::db_set(const DbOp& o, const Cell& v) {
    const DatabasePair* p = (o.which == 1) ? st_.ldb : (o.which == 2) ? st_.sdb
                            : (o.which == 3) ? st_.cdb : nullptr;
    if (!p) { note("db_set: database not loaded"); return false; }
    if (o.type < 0 || o.type >= (int32_t)p->types.size()) { note("db_set: bad type"); return false; }
    DbType& t = const_cast<DbType&>(p->types[(size_t)o.type]);
    if (o.field < 0 || o.field >= (int32_t)t.fields.size()) { note("db_set: bad field"); return false; }
    const DbField& f = t.fields[(size_t)o.field];
    if (o.row < 0) { note("db_set: negative row " + std::to_string(o.row)); return false; }
    // The engine checks type-count (0x480100) and field-count (0x480250) only —
    // rows grow on write (runtime row expansion; see ev7 row 63/78 clears).
    if (o.row >= (int32_t)t.data.size()) {
        int32_t nints = 0, nstrs = 0;
        for (const DbField& fl : t.fields) {
            int32_t ix = fl.index();
            if (ix < 0) continue;
            if (fl.is_string()) nstrs = std::max(nstrs, ix + 1);
            else nints = std::max(nints, ix + 1);
        }
        t.data.resize((size_t)o.row + 1);
        for (size_t k = 0; k < t.data.size(); ++k) {
            if ((int32_t)t.data[k].int_values.size() < nints) t.data[k].int_values.resize((size_t)nints, 0);
            if ((int32_t)t.data[k].string_values.size() < nstrs) t.data[k].string_values.resize((size_t)nstrs);
        }
    }
    DbData& d = t.data[(size_t)o.row];
    if (f.is_string()) {
        int32_t idx = f.index();
        if (idx >= 0 && idx < (int32_t)d.string_values.size()) d.string_values[(size_t)idx] = v.as_str();
    } else {
        int32_t idx = f.index();
        if (idx >= 0 && idx < (int32_t)d.int_values.size()) d.int_values[(size_t)idx] = (int32_t)v.as_int();
    }
    ++res_->db_writes;
    return true;
}

// ---------------------------------------------------------------------------
// Operand evaluation
// ---------------------------------------------------------------------------
long long Vm::eval_int(long long addr) {
    if (addr >= 1000000000LL) {
        DbOp o = eval_db(addr);
        Cell c;
        if (o.which != 0 && db_get(o, c)) return c.as_int();
        return 0;
    }
    if (is_cself(addr)) return ccell((size_t)(addr - 1600000)).as_int();
    if (addr >= 2000000 && addr < 3000000) return gcell(addr).as_int();
    if (addr >= 3000000 && addr < 4000000) return gcell(addr).as_int();   // sys string as int
    if (addr >= 1600100 && addr < 2000000) {
        note("operand in unused bank 1.6M..2M: " + std::to_string(addr));
        return addr;
    }
    if (addr >= 8000000 && addr < 9000000) {
        // Random operand band: 8,000,000+n → rand(1..n) (engine 0x43f5ae pattern)
        long long n = addr - 8000000;
        if (n <= 0) return 0;
        return rand_ll(1, n);
    }
    if (addr >= 9000000 && addr < 10000000LL) {
        // Engine sound/map state band: (addr-base)/10 × (addr%10) decomposition.
        note("engine-state operand 9M band (approx 0): " + std::to_string(addr));
        return 0;
    }
    if (addr >= 10000000LL && addr < 20000000LL) {
        note("engine-state operand 10M..20M band (approx 0): " + std::to_string(addr));
        return 0;
    }
    return addr;   // plain literal (this data writes no normal vars in 121/124)
}

Cell Vm::fetch_cell(long long addr) {
    // Variant-preserving operand resolve (engine 0x43a240 generic getter):
    // DB ref -> db_get cell; cself/sys bands -> cell copy; plain literal -> int.
    Cell out;
    if (addr >= 1000000000LL) {
        DbOp o = eval_db(addr);
        if (o.which != 0) db_get(o, out);
    } else if (is_cself(addr)) {
        out = ccell((size_t)(addr - 1600000));
    } else if (addr >= 2000000 && addr < 4000000) {
        out = gcell(addr);
    } else {
        out.set_i((int32_t)addr);
    }
    return out;
}

std::string Vm::eval_str(long long addr) {
    if (is_cself(addr)) return ccell((size_t)(addr - 1600000)).as_str();
    if (addr >= 3000000 && addr < 4000000) return gcell(addr).as_str();
    if (addr >= 1000000000LL) {
        DbOp o = eval_db(addr);
        Cell c;
        if (o.which != 0 && db_get(o, c)) return c.as_str();
        return "";
    }
    if (addr >= 2000000 && addr < 3000000) return std::to_string(eval_int(addr));
    if (addr >= 1600100 && addr < 1000000000LL) { note("string op on non-string bank: " + std::to_string(addr)); return ""; }
    return "";   // literal strings arrive via string_args
}

// ---------------------------------------------------------------------------
// Comparisons (Wolf ops: empirically verified 1 = '<', 2 = '==', 5 = '!=';
// others best-effort, loudly noted when hit)
// ---------------------------------------------------------------------------
int Vm::eval_cmp(long long lhs, long long rhs, int32_t op) {
    switch (op & 0x0F) {
        case 0: return lhs > rhs ? 1 : 0;             // 等しい/より大きい? — 200?? rare
        case 1: return lhs < rhs ? 1 : 0;             // (data: loop bounds)
        case 2: return lhs == rhs ? 1 : 0;            // (data: candidate matchers)
        case 3: return lhs <= rhs ? 1 : 0;
        case 4: return lhs >= rhs ? 1 : 0;
        case 5: return lhs != rhs ? 1 : 0;            // (data: chain tail)
        case 6: return (lhs & rhs) != 0 ? 1 : 0;      // bit AND non-zero
        case 7: return (lhs & rhs) == rhs ? 1 : 0;    // bit superset
        case 8: return (lhs % rhs) == 0 ? 1 : 0;
        case 9: return (lhs % rhs) != 0 ? 1 : 0;
        default:
            note("111 unknown op " + std::to_string(op));
            return -1;
    }
}

int Vm::eval_cmp_str(const std::string& lhs, const std::string& rhs, int32_t op) {
    switch (op & 0x0F) {
        case 0: case 1: return lhs == rhs ? 1 : 0;
        case 5: return lhs != rhs ? 1 : 0;
        case 6: return lhs.find(rhs) != std::string::npos ? 1 : 0;
        case 7: return lhs.rfind(rhs, 0) == 0 ? 1 : 0;
        default:
            note("112 unknown op " + std::to_string(op));
            return -1;
    }
}

// 111: [opcount(|0x10), (lhs, rhs, op) × opcount]; triples OR'ed (data).
bool Vm::cond_true_int(const Command& c, long long& case_target) {
    int32_t hdr = c.args[0];
    int cnt = hdr & 0x0F;
    if (cnt == 0) {
        if (hdr & ~0x0F) note("111 arg0 " + std::to_string(hdr) + " with zero ops");
        case_target = 0;
        return false;
    }
    bool any = false;
    size_t k = 1;
    for (int t = 0; t < cnt; ++t) {
        if (k + 2 >= c.args.size()) { note("111 truncated triple (arg0 " + std::to_string(hdr) + ")"); return false; }
        long long lhs = eval_int(c.args[k]);
        long long rhs = eval_int(c.args[k + 1]);
        int r = eval_cmp(lhs, rhs, c.args[k + 2]);
        if (r < 0) { case_target = 0; return false; }
        any = any || (r == 1);
        k += 3;
    }
    // Engine model (ev6 add/remove split, ev6#54/#57 chains): the cond sets a
    // result register — 1 = true, 2 = false — and 401[n] matches register == n.
    case_target = any ? 1 : 2;
    return any;
}

// 112: string matcher; cond value selects which candidate matched.
bool Vm::cond_case_str(const Command& c, long long& case_no) {
    int32_t hdr = c.args[0];
    int lefts = (hdr & 0x0F) == 0 ? 1 : (hdr & 0x0F);
    case_no = 0;
    if (c.string_args.empty()) { note("112 without candidate strings"); return false; }
    for (size_t ci = 0; ci < c.string_args.size(); ++ci) {
        // One candidate per case: left cells OR'ed. Left operands in args[1..].
        for (int t = 0; t < lefts && t + 1 < (int)c.args.size(); ++t) {
            std::string l = eval_str(c.args[(size_t)t + 1]);
            if (l == c.string_args[ci]) { case_no = (long long)(ci + 1); return true; }
        }
    }
    return false;
}

bool Vm::exec_cond(const Command& c, long long& case_target) {
    case_target = 0;
    if (c.args.empty()) { note(c.cid == 111 ? "111 without args" : "112 without args"); return false; }
    if (c.cid == 111) return cond_true_int(c, case_target);
    return cond_case_str(c, case_target);   // sets case_no directly; 0 = no case
}

// ---------------------------------------------------------------------------
// Structure scanning
// ---------------------------------------------------------------------------
size_t Vm::find_next_token(size_t from, size_t indent) {
    const auto& cmds = ce_->events[index_by_id_.at(stack_.back().common_id)].commands;
    while (from < cmds.size()) {
        if (cmds[from].indent == indent) {
            uint32_t ci = cmds[from].cid;
            if (ci == 401 || ci == 420 || ci == 499) break;
        }
        ++from;
    }
    return from;
}

size_t Vm::find_tree_end(size_t from, size_t indent) {
    const auto& cmds = ce_->events[index_by_id_.at(stack_.back().common_id)].commands;
    while (from < cmds.size() && !(cmds[from].indent == indent && cmds[from].cid == 498))
        ++from;
    return from;
}

size_t Vm::fwd_label(size_t from, const std::string& name) const {
    const auto& cmds = ce_->events[index_by_id_.at(stack_.back().common_id)].commands;
    for (size_t p = from; p < cmds.size(); ++p)
        if (cmds[p].cid == 212 && !cmds[p].string_args.empty() && cmds[p].string_args[0] == name)
            return p;
    return (size_t)-1;
}

// ---------------------------------------------------------------------------
// Statement handlers
// ---------------------------------------------------------------------------
bool Vm::exec_setvar(const Command& c) {
    const long long flags = (c.args.size() > 3) ? c.args[3] : 0;
    const long long target = c.args[0];
    const long long op1raw = (c.args.size() > 1) ? c.args[1] : 0;
    const long long op2raw = (c.args.size() > 2) ? c.args[2] : 0;
    // -- ENGINE-VERIFIED gates (ENGINE_ANALYSIS §10.3/§10.5, all from disasm):
    // low byte set -> skip; reserved target 1100000000 -> skip; the 0x10000
    // family is skipped by the immediate handler and runs at a still-unfound
    // deferral site (wolfvm approximates by applying the same pseudo-matrix
    // op as the least-wrong model). Nibble digits: tens f00 = (flags>>8)&0xF
    // selects the accumulate mode {0:assign,1:+=,2:-=,3:*=,4:/=}; ones f000
    // = (flags>>12)&0xF selects the base op {0:+,1:-,2:x}. The handler
    // rewrites elem0 to pseudo = 210000 + f00*10 + f000 (vec1*0xa + vec2 +
    // 0x33450, imul/lea verified) and the paired second interpreter
    // dispatches 210000 directly (dst=v1+v2), then via byte selector map
    // 0x4575f7 + jump table 0x4575c7: 210001=-, 210002=x, 210008=+=v1+v2,
    // 210009=+=v1-v2, 210010=+=v1*x, 210020=-=v1+v2, 210021=-=v1-v2,
    // 210022=-=v1*x, 210030=x=v1+v2, 210031=x=v1-v2, 210032=x=v1*x;
    // 210003..7/210011..17/210023..29 map to selector 0x0b = skip arm
    // (0x45730b); 210040=(dst/(v1+v2)) and 210041=(dst/(v1-v2)) dispatch
    // directly; 210042+ is rejected in the handler before a pseudo exists.
    if (flags & 0xFF) {
        note("121 skip (engine gate: low byte) target " + std::to_string(target) +
             " flags " + std::to_string(flags));
        return true;
    }
    if (target == 1100000000LL) {
        note("121 skip (engine gate: reserved target) flags " + std::to_string(flags));
        return true;
    }
    const int f00 = (int)((flags >> 8) & 0xF);   // tens: accumulate mode
    const int f000 = (int)((flags >> 12) & 0xF); // ones: base op {0:+,1:-,2:x}
    const long long v1 = eval_int(op1raw);
    const long long v2 = eval_int(op2raw);
    // Engine no-arm cases (byte selector 0x0b -> skip arm): pseudo 210011,
    // 210012 (and the 210003..7 / 210023..29 bands that the handler never
    // emits from cid-121). The deferred 0x10000 family reuses the same
    // mapping as its approximation.
    if (f00 == 1 && f000 >= 1) {
        // pseudo 210011/210012: handler accepts, second interpreter skips.
        note("121 skip (engine: pseudo " + std::to_string(210000LL + f00 * 10LL + f000) +
             " no arm) flags " + std::to_string(flags));
        return true;
    }
    Cell result;
    if (f00 == 4) {
        // Handler rejects raw >= 1e6 operands outright, cancels f000=0 when
        // the raw sum is 0 and f000=1 when the raw difference is 0, and never
        // emits a pseudo for f000 >= 2. Only then the second interpreter's
        // verified arms divide the CURRENT dst cell by (v1+v2) or (v1-v2).
        if (op1raw >= 1000000LL || op2raw >= 1000000LL) {
            note("121 skip (engine f00=4: raw >= 1e6) target " + std::to_string(target) +
                 " flags " + std::to_string(flags));
            return true;
        }
        if (f000 >= 2) {
            note("121 skip (engine f00=4: no pseudo for f000 " + std::to_string(f000) +
                 ") flags " + std::to_string(flags));
            return true;
        }
        if (f000 == 0 && op1raw + op2raw == 0) {
            note("121 skip (engine f00=4: raw v1+v2 == 0) target " + std::to_string(target));
            return true;
        }
        if (f000 == 1 && op1raw - op2raw == 0) {
            note("121 skip (engine f00=4: raw v1-v2 == 0) target " + std::to_string(target));
            return true;
        }
        Cell cur = fetch_cell(target);
        if (cur.touched_s)
            note("121 divide over string cell " + std::to_string(target));
        const long long den = (f000 == 0) ? (v1 + v2) : (v1 - v2);
        if (den == 0) {
            note("121 divide by zero (dst " + std::to_string(target) + ")");
            return false;
        }
        result.set_i(cur.i / den);
    } else if (f00 >= 5 || f000 >= 3) {
        // Legacy/dropped opcodes: the engine performs NOTHING for these
        // records (census skips: tens 0x0500-0x0800 x340, 0x6000 x414 — the
        // old "rand-loop wobble" model was a misread — and ones-digit
        // f000>=3 incl. 0x3000 x234, 0x8000 x285, 0x3200 ...).
        note("121 skip (engine: dropped family f00 " + std::to_string(f00) +
             " f000 " + std::to_string(f000) + ") target " + std::to_string(target) +
             " flags " + std::to_string(flags) +
             (flags & 0x10000 ? " deferred-family" : ""));
        return true;
    } else {
        long long out;
        switch (210000LL + f00 * 10LL + f000) {
            case 210000: out = v1 + v2; break;                    // assign
            case 210001: out = v1 - v2; break;
            case 210002: out = v1 * v2; break;
            case 210010: case 210020: case 210021: case 210022:
            case 210030: case 210031: case 210032: {
                const long long box =
                    (f000 == 0) ? (v1 + v2) : (f000 == 1) ? (v1 - v2) : (v1 * v2);
                Cell cur = fetch_cell(target);
                if (cur.touched_s)
                    note("121 accumulate over string cell " + std::to_string(target));
                switch (f00) {
                    case 1: out = cur.i + box; break;             // dst +=
                    case 2: out = cur.i - box; break;             // dst -=
                    case 3: out = cur.i * box; break;             // dst *=
                    default: out = cur.i; break;
                }
                break;
            }
            default:
                note("121 unreachable pseudo " +
                     std::to_string(210000LL + f00 * 10LL + f000) + " flags " +
                     std::to_string(flags));
                return false;
        }
        result.set_i(out);
    }

    if (target >= 1000000000LL) {
        DbOp o = eval_db(target);
        if (o.which == 0) { note("121 bad dbref target " + std::to_string(target)); return false; }
        if (c.args.size() >= 7) {
            // 7-arg matrix form (data-proven, 12 uses): [DB-pack, value, ?, flags,
            //   mode (1=plain, 4=rand pairs with flags 0x6000), 0, ROW].
            // Pack's row-part is overridden by args[6]; type/field stay from the pack
            // (all observed packs are bare DataBase bases, so field ambiguity is moot).
            o.row = (int32_t)eval_int(c.args[6]);
            uint32_t m7 = (uint32_t)c.args[4];
            if (m7 != 1 && m7 != 4)
                note("121 7-arg mode " + std::to_string(m7) + " unmodeled (row " +
                     std::to_string(o.row) + ")");
        }
        if (!db_set(o, result)) return false;
        if (dbops_) { DbOp rec = o; rec.write = true; rec.written = result.i; rec.written_s = result.as_str(); dbops_->push_back(rec); }
        return true;
    }
    cell_for(target).set_i(result.i);
    return true;
}

bool Vm::exec_setvarex(const Command& c) {
    // 124 (SetVariableEx) — partial: masks 16384 / 12288 / 532480 observed.
    // v1 copies the primary operand; deviations are loud.
    const long long target = c.args[0];
    const int32_t mask = (c.args.size() > 1) ? c.args[1] : 0;
    long long value = 0;
    if (c.args.size() >= 3) value = eval_int(c.args[2]);
    if (c.args.size() >= 4)
        note("124 extra operand " + std::to_string(c.args[3]) + " (mask " + std::to_string(mask) + ")");
    if (mask != 16384 && mask != 12288 && mask != 532480)
        note("124 mask " + std::to_string(mask) + " unknown");
    cell_for(target).set_i(value);
    return true;
}

bool Vm::exec_setstr(const Command& c) {
    // 122: [target, flags, var-addr?] with templates in string_args (data):
    //   a3s0 → value = cell at args[2]; a2s1/s2 → literal templates.
    //   flags&0xF: 0 assign, 1 append; higher bits reported.
    const long long target = c.args[0];
    const int32_t flags = (c.args.size() > 1) ? c.args[1] : 0;
    const int32_t op = flags & 0x0F;
    std::string value;
    if (c.args.size() >= 3) value = eval_str(c.args[2]);
    else if (!c.string_args.empty()) value = c.string_args[0];
    if (flags > 0x0F) note("122 flags " + std::to_string(flags));
    Cell& tc = cell_for(target);
    if (op == 0) {
        tc.set_s(value);
    } else if (op == 1) {
        tc.set_s(tc.as_str() + value);
    } else {
        note("122 op " + std::to_string(op) + " (flags " + std::to_string(flags) + ") undecoded");
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Main loop
// ---------------------------------------------------------------------------
bool Vm::run(int32_t common_id, RunResult& out) {
    res_ = &out;
    steps_ = 0;
    out.ok = false;
    out.err.clear();
    if (ce_ == nullptr) { out.err = "vm not loaded"; return false; }
    const CommonEventRecord* e = ev(common_id);
    if (!e) { out.err = "common event id not found: " + std::to_string(common_id); return false; }

    stack_.clear();
    labels_stack_.clear();
    stack_.push_back(Frame{});
    stack_.back().common_id = common_id;
    labels_stack_.emplace_back();

    const size_t kMaxDepth = 256;
    while (!stack_.empty()) {
        if (++steps_ > step_cap_) {
            out.err = "step cap exceeded (" + std::to_string(step_cap_) + ") — runaway loop";
            return false;
        }
        Frame& f = stack_.back();
        const CommonEventRecord* cur = ev(f.common_id);
        if (!cur) { out.err = "lost event " + std::to_string(f.common_id); return false; }
        if (f.pc >= cur->commands.size()) {
            size_t id = f.common_id;
            stack_.pop_back();
            labels_stack_.pop_back();
            if (stack_.empty()) break;
            // caller pc was advanced by the 210 pusher already — return must not bump again
            tlog("<= common " + std::to_string(id) + " returned");
            continue;
        }
        const Command& c = cur->commands[f.pc];
        const size_t indent = c.indent;
        const uint32_t cid = c.cid;
        ++out.cid_counts[cid];
        ++out.executed;
        if (tr_ && tr_->enabled) {
            char b[160];
            std::snprintf(b, sizeof(b), "step ev%d pc%zu ind%d cid%u args%d", f.common_id,
                          f.pc, (int)indent, cid, (int)c.args.size());
            std::fprintf(stderr, "%s\n", b);
        }

        switch (cid) {
            case 0:     // Blank
            case 99:    // Checkpoint
            case 103:   // Comment
            case 106:   // DebugMessage (headless: trace only)
                ++f.pc;
                break;

            case 111:
            case 112: {
                long long case_target = 0;
                exec_cond(c, case_target);
                Ctx ctx;
                ctx.kind = CtxKind::Cond;
                ctx.indent = indent;
                ctx.open_pc = f.pc;
                ctx.case_target = case_target;
                f.branch.push_back(ctx);
                if (case_target == 0) {
                    // No case matches: skim 401 bodies; leave handled at 420/499.
                    // Jump straight past the 401/420 header and let their
                    // handlers skip/execute; place pc on first token.
                    size_t tok = find_next_token(f.pc + 1, indent);
                    if (tok >= cur->commands.size()) {
                        out.err = "condition without 499 (ev " + std::to_string(f.common_id) + " cmd " + std::to_string(f.pc) + ")";
                        return false;
                    }
                    if (cur->commands[tok].cid == 499) {
                        // directly at the end marker
                        f.pc = tok;              // 499 handler pops ctx
                    } else {
                        f.pc = tok;              // 401/420 handler decides
                    }
                } else {
                    ++f.pc;   // body of the first matching case
                }
                break;
            }

            case 121:
                if (!exec_setvar(c)) {
                    out.err = "121 failed ev" + std::to_string(f.common_id) + " cmd " + std::to_string(f.pc);
                    return false;
                }
                ++f.pc;
                break;
            case 122:
                if (!exec_setstr(c)) {
                    out.err = "122 failed ev" + std::to_string(f.common_id) + " cmd " + std::to_string(f.pc);
                    return false;
                }
                ++f.pc;
                break;
            case 124:
                if (!exec_setvarex(c)) {
                    out.err = "124 failed ev" + std::to_string(f.common_id) + " cmd " + std::to_string(f.pc);
                    return false;
                }
                ++f.pc;
                break;

            case 170:   // StartLoop (open-ended, exits on 171/172)
            case 176: { // StartLoop variant
                Ctx ctx;
                ctx.kind = CtxKind::LoopOpen;
                ctx.indent = indent;
                ctx.open_pc = f.pc;
                ctx.start_pc = f.pc + 1;
                f.branch.push_back(ctx);
                ++f.pc;
                break;
            }
            case 179: { // LoopTimes: [count-operand], body to 498
                long long cnt = eval_int(c.args[0]);
                if (cnt <= 0) {
                    size_t e498 = find_tree_end(f.pc + 1, indent);
                    f.pc = (e498 >= cur->commands.size() ? cur->commands.size() : e498 + 1);
                } else {
                    Ctx ctx;
                    ctx.kind = CtxKind::LoopTimes;
                    ctx.indent = indent;
                    ctx.open_pc = f.pc;
                    ctx.start_pc = f.pc + 1;
                    ctx.remain = cnt;
                    f.branch.push_back(ctx);
                    ++f.pc;
                }
                break;
            }
            case 171: { // BreakLoop
                bool found = false;
                for (size_t k = f.branch.size(); k-- > 0;) {
                    if (f.branch[k].kind != CtxKind::Cond) {
                        size_t e498 = find_tree_end(f.branch[k].start_pc, f.branch[k].indent);
                        f.pc = (e498 >= cur->commands.size() ? cur->commands.size() : e498 + 1);
                        f.branch.resize(k);
                        found = true;
                        break;
                    }
                }
                if (!found) { out.err = "171 BreakLoop without loop"; return false; }
                ++f.pc;
                break;
            }
            case 498: { // LoopEnd
                if (!f.branch.empty() && f.branch.back().kind != CtxKind::Cond &&
                    f.branch.back().indent == indent) {
                    Ctx& ctx = f.branch.back();
                    if (ctx.kind == CtxKind::LoopTimes) {
                        if (ctx.remain > 1) {
                            --ctx.remain;
                            f.pc = ctx.start_pc;
                        } else {
                            f.branch.pop_back();
                            ++f.pc;
                        }
                    } else {
                        f.pc = ctx.start_pc;    // LoopOpen: rewind (exit via 171)
                    }
                } else {
                    // Loop frame mismatch (should be impossible with balanced
                    // structure): pop cond contexts defensively.
                    if (!f.branch.empty() && f.branch.back().kind == CtxKind::Cond)
                        f.branch.pop_back();
                    ++f.pc;
                }
                break;
            }

            case 401: { // ChoiceCase: [case#]
                // The cond ctx may sit BELOW loop ctxs pushed inside a case body
                // (401 chains without 499 separators: selector bodies flow into
                // the next selector through open-ended loop bodies).
                long long k401 = (long long)f.branch.size() - 1;
                while (k401 >= 0 && !(f.branch[k401].kind == CtxKind::Cond &&
                                      f.branch[k401].indent == indent)) --k401;
                if (k401 < 0) {
                    out.err = "401 without open cond (ev" + std::to_string(f.common_id) +
                              " cmd " + std::to_string(f.pc) + ")";
                    return false;
                }
                Ctx& ctx = f.branch[k401];
                int32_t case_no = c.args.empty() ? 1 : c.args[0];
                if (ctx.body_done) {
                    // A body ran: skim to the next chain token (the 499 itself
                    // pops the context when reached — single pop point).
                    size_t tok = find_next_token(f.pc + 1, indent);
                    if (tok >= cur->commands.size()) { out.err = "401 chain unterminated"; return false; }
                    f.pc = tok;
                    break;
                }
                if (case_no == ctx.case_target) {
                    ctx.body_done = true;
                    ++f.pc;
                } else {
                    size_t tok = find_next_token(f.pc + 1, indent);
                    if (tok >= cur->commands.size()) { out.err = "401 chain unterminated"; return false; }
                    f.pc = tok;
                }
                break;
            }
            case 420: { // ElseCase
                // Mirror the 401 search: the cond ctx may sit below loop ctxs
                // pushed inside open-ended case bodies (499-less chains).
                long long k420 = (long long)f.branch.size() - 1;
                while (k420 >= 0 && !(f.branch[k420].kind == CtxKind::Cond &&
                                      f.branch[k420].indent == indent)) --k420;
                if (k420 < 0) {
                    out.err = "420 without open cond (ev" + std::to_string(f.common_id) +
                              " cmd " + std::to_string(f.pc) + ")";
                    return false;
                }
                Ctx& ctx = f.branch[k420];
                if (ctx.body_done) {
                    size_t tok = find_next_token(f.pc + 1, indent);
                    if (tok >= cur->commands.size()) { out.err = "420 chain unterminated"; return false; }
                    f.pc = tok;
                } else {
                    ctx.body_done = true;
                    ++f.pc;
                }
                break;
            }
            case 499: { // BranchEnd
                bool matched = false;
                for (size_t k = f.branch.size(); k-- > 0;) {
                    if (f.branch[k].kind == CtxKind::Cond && f.branch[k].indent == indent) {
                        f.branch.resize(k);
                        matched = true;
                        break;
                    }
                }
                if (!matched) { out.err = "499 without cond (ev" + std::to_string(f.common_id) +
                                             " cmd " + std::to_string(f.pc) + ")"; return false; }
                ++f.pc;
                break;
            }

            case 212: { // SetLabel (this event only; forward jumps scan from here on)
                if (!c.string_args.empty())
                    labels_stack_.back().insert_or_assign(c.string_args[0], f.pc + 1);
                ++f.pc;
                break;
            }
            case 213: { // JumpLabel — backward via the 212-collected map, forward via scan
                const std::string& nm = c.string_args.empty() ? std::string() : c.string_args[0];
                auto it = labels_stack_.back().find(nm);
                size_t l = (it != labels_stack_.back().end()) ? it->second : fwd_label(f.pc + 1, nm);
                if (l == (size_t)-1) { out.err = "213 label '" + nm + "' not found"; return false; }
                f.pc = l;
                break;
            }

            case 210: { // CommonEvent call: [500000+id, opts, args...]
                long long raw = c.args.empty() ? 0 : c.args[0];
                if (raw < 500000 || raw > 599999) {
                    out.err = "210 bad id encoding " + std::to_string(raw);
                    return false;
                }
                int32_t callee = (int32_t)(raw - 500000);
                if (!ev(callee)) { out.err = "210 callee missing: " + std::to_string(callee); return false; }
                uint32_t opts = (uint32_t)c.args[1];
                if (opts != 0 && opts != 16)
                    note("210 opts 0x" + [](uint32_t v){ std::string s; const char* h = "0123456789ABCDEF"; for (int i = 0; i < 8; ++i) { s = h[v & 0xF] + s; v >>= 4; } return s; }(opts));
                if (stack_.size() >= kMaxDepth) { out.err = "210 recursion cap"; return false; }
                Frame nf{};
                nf.common_id = callee;
                stack_.push_back(nf);
                labels_stack_.emplace_back();
                for (size_t k = 2; k < c.args.size(); ++k) {
                    stack_.back().cself[std::min(k - 2, (size_t)99)] = fetch_cell(c.args[k]);
                }
                // Caller pc advance AFTER the push (f reference died in realloc)
                stack_[stack_.size() - 2].pc += 1;
                tlog("=> common " + std::to_string(callee) + " called");
                break;
            }

            case 250: { // Database (engine: fcn.0043a5c0 @ 0x43b9e1 — decoded)
                // args = [type, row, field, flags, value]
                // (flags>>8)&0xF = DB select — engine objects 0xaa9498/0xaa7670/0xaa75f0:
                //   0 = CDataBase, 1 = SysDatabase, 2 = DataBase (proven: CData[2]=603 rows,
                //   CData[6]=100 int fields, CData[42]=1 string field; DataBase has 57/88)
                // (flags>>12)&0xF = mode: 1 = READ cell -> variable(args[4]); 0 = WRITE value(args[4])
                //   (row+value operands >=1e6 resolved via generic getter, engine 0x43c077/0x43c0f7)
                // (flags>>4)&0xF = mode A (observed {0,1..7} on writes; semantics unmodeled)
                // low nibble (0xF) = variant, engine guard requires 0
                // bits 0x10000/0x20000/0x40000 = type/row/field is a NAME string (unmodeled -> note)
                // out-of-range cell = silent skip in engine (bounds -> 0x43c485 loop continue)
                spot_ = "ev" + std::to_string(f.common_id) + "#" + std::to_string(f.pc);
                const int32_t flags = (c.args.size() > 3 ? c.args[3] : 0);
                const int32_t sel = (flags >> 8) & 0xF;
                DbOp o{};
                o.which = (sel == 0) ? 3 : (sel == 1) ? 2 : (sel == 2) ? 1 : 0;  // 1=ldb 2=sdb 3=cdb
                o.type  = (int32_t)eval_int(c.args[0]);
                o.flags = (uint32_t)flags;
                if (sel >= 3) { note("250: db-select nibble " + std::to_string(sel) + " unmodeled — skipped"); ++f.pc; break; }
                o.row   = (int32_t)eval_int(c.args[1]);
                o.field = (int32_t)eval_int(c.args[2]);   // engine 0x43c177: field resolves through the shared prep too
                if (flags & 0xF0000 || flags & 0xF) {
                    note("250: flags 0x" + [](uint32_t v){ std::string s; const char* h = "0123456789ABCDEF"; for (int i = 0; i < 8; ++i) { s = h[v & 0xF] + s; v >>= 4; } return s; }(flags) +
                         " name/variant bits — skipped");
                    ++f.pc; break;
                }
                // (flags>>4)&0xF = value-marshall flavor (0=expr, 2/6=litteral variants) — non-semantic here.
                const long long target = (c.args.size() > 4) ? c.args[4] : 0;
                const int32_t modeB = (flags >> 12) & 0xF;
                if (modeB >= 2) {
                    note("250: mode B " + std::to_string(modeB) + " unmodeled — skipped");
                    ++f.pc; break;
                }
                if (o.row == -3 || o.field == -3) { note("250: -3 keep-sentinel row/field — skipped"); ++f.pc; break; }
                if (o.field < 0) {
                    note("250: negative field " + std::to_string(o.field) + " (row-name?) unmodeled — skipped");
                    ++f.pc; break;
                }
                if (modeB == 1) {
                    // READ: cell -> target variable (kind follows the field's declared type)
                    Cell v;
                    if (!db_get(o, v)) {
                        note("250: read skipped out-of-bounds (type " + std::to_string(o.type) +
                             " row " + std::to_string(o.row) + " field " + std::to_string(o.field) + ")");
                        ++f.pc; break;
                    }
                    Cell& tc = cell_for(target);
                    if (v.touched_s) tc.set_s(v.s); else tc.set_i(v.i);
                    if (dbops_) { DbOp rec = o; rec.write = false; dbops_->push_back(rec); }
                } else {
                    // WRITE: value operand -> cell. Variants resolve as cells (kind-preserving),
                    // plain literals become ints — matches engine's eval-if->=1e6 + variant marshal.
                    Cell src = fetch_cell(target);
                    if (!db_set(o, src)) {
                        note("250: write skipped out-of-bounds (type " + std::to_string(o.type) +
                             " row " + std::to_string(o.row) + " field " + std::to_string(o.field) + ")");
                    }
                    if (dbops_) { DbOp rec = o; rec.write = true; rec.written = (int32_t)src.as_int(); rec.written_s = src.as_str(); dbops_->push_back(rec); }
                }
                ++f.pc;
                break;
            }

            default: {
                switch (cid) {
                    case 101: case 102: case 140: case 150: case 151: case 160:
                    case 180: case 201: case 202: case 221: case 222: case 240:
                    case 241: case 242: case 270: case 280: case 281: case 290:
                    case 300: case 123:
                        // presentation / environment — trace-no-op for now
                        break;
                    default:
                        note("unhandled cid " + std::to_string(cid) + " (ev" +
                             std::to_string(f.common_id) + " cmd " + std::to_string(f.pc) + ")");
                        break;
                }
                ++f.pc;
                break;
            }
        }
    }

    out.steps = steps_;
    out.ok = true;
    return true;
}

} // namespace wolf

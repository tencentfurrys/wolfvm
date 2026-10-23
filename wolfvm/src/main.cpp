// wolfvm v0.3 — Stage 1: dat primitives Game.dat + CommonEvent.dat decoders verified on real data.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>
#include <tuple>
#include <string>
#include <vector>
#include <algorithm>

#include "common_event.h"
#include "database.h"
#include "filecoder.h"
#include "game_dat.h"
#include "map_project.h"
#include "map_runtime.h"
#include "map_tree.h"
#include "vm.h"
#include "map_tree_open_status.h"
#include "sys_db_basic.h"
#include "tile_set_data.h"

static std::vector<uint8_t> read_file(const char* path) {
    FILE* f = std::fopen(path, "rb");
    if (!f) { std::fprintf(stderr, "cannot open %s\n", path); std::exit(2); }
    std::fseek(f, 0, SEEK_END);
    long sz = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> buf(static_cast<size_t>(sz));
    if (sz > 0 && std::fread(buf.data(), 1, buf.size(), f) != buf.size()) {
        std::fprintf(stderr, "short read on %s\n", path); std::exit(2);
    }
    std::fclose(f);
    return buf;
}

static int cmd_datinfo(const char* path) {
    auto data = read_file(path);
    std::string err;
    if (!wolf::is_plain_or_decrypt(data, err)) {
        std::fprintf(stderr, "ERROR: %s\n", err.c_str());
        return 1;
    }
    if (data.size() < 10) { std::fprintf(stderr, "ERROR: too small\n"); return 1; }
    const uint8_t* b = data.data();
    const bool magic_ok = (b[0] == 0x00 && b[1] == 0x57 && b[2] == 0x00 && b[3] == 0x00 &&
                           b[4] == 'O' && b[5] == 'L' && b[6] == 0x00 && b[7] == 0x46);
    if (!magic_ok) { std::fprintf(stderr, "ERROR: not a plain WOLF dat record\n"); return 1; }
    std::printf("%s\n  size      : %zu\n  dat-class : %c\n", path, data.size(), b[8]);
    return 0;
}

static int cmd_gameinfo(const char* path) {
    auto data = read_file(path);
    std::string err;
    if (!wolf::is_plain_or_decrypt(data, err)) {
        std::fprintf(stderr, "ERROR: %s\n", err.c_str());
        return 1;
    }
    wolf::Reader r(std::move(data));
    wolf::GameDat g = wolf::GameDat::parse(r, err);
    if (!err.empty()) { std::fprintf(stderr, "ERROR: %s\n", err.c_str()); return 1; }
    std::printf("%s\n", path);
    std::printf("  file_version         : %d\n", g.file_version);
    std::printf("  title                : %s\n", g.title.c_str());
    std::printf("  magic_string         : %s\n", g.magic_string.c_str());
    std::printf("  unknown1             : %zu bytes\n", g.unknown1.size());
    std::printf("  unknown2             : %zu bytes\n", g.unknown2.size());
    std::printf("  font                 : %s\n", g.font.c_str());
    std::printf("  subfonts             : %s, %s, %s\n",
                g.subfonts[0].c_str(), g.subfonts[1].c_str(), g.subfonts[2].c_str());
    std::printf("  default_pc_graphic   : %s\n", g.default_pc_graphic.c_str());
    std::printf("  version              : %s\n", g.version.c_str());
    std::printf("  trailing_size        : %u\n", g.trailing_size);
    std::printf("  rest                 : %zu bytes\n", g.rest.size());
    return 0;
}

static std::string escape_bytes(const std::string& s) {
    std::string out;
    out.reserve(s.size() * 4);
    char hex[5];
    for (unsigned char c : s) {
        if (c >= 0x20 && c <= 0x7E && c != '"' && c != '\\') {
            out.push_back(static_cast<char>(c));
        } else {
            std::snprintf(hex, 5, "\\x%02X", c);
            out += hex;
        }
    }
    return out;
}

static int cmd_commondump(const char* path, int event_id, bool histogram) {
    auto data = read_file(path);
    std::string err;
    if (!wolf::is_plain_or_decrypt(data, err)) {
        std::fprintf(stderr, "ERROR: %s\n", err.c_str());
        return 1;
    }
    wolf::Reader r(std::move(data));
    wolf::CommonEventDat ce;
    wolf::CommonEventDat::parse(r, ce, err);
    if (!err.empty()) { std::fprintf(stderr, "ERROR: %s\n", err.c_str()); return 1; }

    if (histogram) {
        std::map<uint32_t, unsigned> h;
        for (const auto& e : ce.events)
            for (const auto& c : e.commands) ++h[c.cid];
        std::vector<std::pair<uint32_t, unsigned>> v(h.begin(), h.end());
        std::sort(v.begin(), v.end(),
                  [](const auto& a, const auto& b) { return a.second > b.second; });
        unsigned total = 0;
        for (const auto& p : v) total += p.second;
        std::printf("event count %zu, command count %u, distinct cids %zu\n",
                    ce.events.size(), total, v.size());
        for (const auto& p : v) {
            wolf::Command probe;
            probe.cid = p.first;
            const char* nm = probe.name();
            std::printf("  %8u x cid %5u  (%s)\n", p.second, p.first, nm ? nm : "?");
        }
        return 0;
    }

    const wolf::CommonEventRecord* ev = nullptr;
    if (event_id >= -1) {
        for (const auto& e : ce.events)
            if (e.id == event_id) { ev = &e; break; }
    }
    if (!ev) {
        std::fprintf(stderr, "no event with id %d (have %zu events)\n", event_id, ce.events.size());
        return 1;
    }
    std::printf("event %d  name \"%s\"  cmds=%zu  desc \"%s\"\n",
                ev->id, escape_bytes(ev->name).c_str(), ev->commands.size(),
                escape_bytes(ev->description).c_str());
    if (event_id >= 0) {
        std::printf("  tail: has92 %d  str92a \"%s\"  int92 %d\n",
                    ev->has_92 ? 1 : 0,
                    escape_bytes(ev->tail_string_92a).c_str(),
                    ev->tail_int_92);
        std::printf("  tail: bytes10 [");
        for (size_t b = 0; b < ev->tail_bytes_10.size(); ++b)
            std::printf("%s%02X", b ? " " : "", ev->tail_bytes_10[b]);
        std::printf("]  str91 \"%s\"\n", escape_bytes(ev->tail_string_91).c_str());
        std::printf("  tail: first_strs_10: ");
        for (size_t s = 0; s < ev->tail_strings_10.size() && s < 3; ++s)
            std::printf("[%zu]\"%s\" ", s, escape_bytes(ev->tail_strings_10[s]).c_str());
        std::printf("\n");
    }
    for (size_t i = 0; i < ev->commands.size(); ++i) {
        const wolf::Command& c = ev->commands[i];
        std::printf("    #%-5zu ind %u cid %5u (%s) args [", i, c.indent, c.cid,
                    c.name() ? c.name() : "?");
        for (size_t a = 0; a < c.args.size(); ++a)
            std::printf("%s%d", a ? ", " : "", c.args[a]);
        std::printf("]");
        for (size_t s = 0; s < c.string_args.size(); ++s)
            std::printf(" str%zu [%s]", s, escape_bytes(c.string_args[s]).c_str());
        if (c.has_move_tail) {
            std::printf(" move(fg 0x%02X, route n=%zu)", c.move_flags, c.route.size());
        }
        std::printf("\n");
    }
    return 0;
}

static int cmd_commonstat(const char* path) {
    auto data = read_file(path);
    std::string err;
    if (!wolf::is_plain_or_decrypt(data, err)) {
        std::fprintf(stderr, "ERROR: %s\n", err.c_str());
        return 1;
    }
    wolf::Reader r(std::move(data));
    wolf::CommonEventDat ce;
    wolf::CommonEventDat::parse(r, ce, err);
    if (!err.empty()) { std::fprintf(stderr, "ERROR: %s\n", err.c_str()); return 1; }

    // Collect per-cid: pattern counts, per-arg histograms (bucketed by address bank),
    // and raw value histograms for interesting slots.
    struct Stat {
        std::map<std::string, unsigned> shapes;
        std::map<uint32_t, unsigned> f3;          // arg[3] raw values
        std::map<uint32_t, unsigned> f1;          // arg[1] raw values
        std::map<uint32_t, unsigned> f0;          // arg[0] raw values
        unsigned n = 0;
    };
    std::map<uint32_t, Stat> st;

    auto bucket = [](int32_t v) -> std::string {
        if (v < 0) return "neg";
        if (v < 1600000) return "normal(<1.6M)";
        if (v < 1600100) return "cself(1.6M+0..99)";
        if (v < 1700000) return "cselfHigh?1.6M..1.7M";
        if (v < 2000000) return "zone(1.7M..2M)";
        if (v < 3000000) return "sysVar(2M)";
        if (v < 3100000) return "sysStr?(3M)";
        return "huge";
    };

    for (const auto& e : ce.events) {
        for (const auto& c : e.commands) {
            Stat& s = st[c.cid];
            ++s.n;
            std::string sh = "a";
            sh += std::to_string(c.args.size());
            sh += "s";
            sh += std::to_string(c.string_args.size());
            ++s.shapes[sh];
            if (!c.args.empty()) ++s.f0[static_cast<uint32_t>(c.args[0])];
            if (c.args.size() > 1) ++s.f1[static_cast<uint32_t>(c.args[1])];
            if (c.args.size() > 3) ++s.f3[static_cast<uint32_t>(c.args[3])];
        }
    }

    static const uint32_t interesting[] = { 111,112,121,122,124,250,401,420,499,170,176,179,210,212,213,99 };
    for (uint32_t cid : interesting) {
        auto it = st.find(cid);
        if (it == st.end()) continue;
        const Stat& s = it->second;
        std::printf("cid %u  n=%u\n", cid, s.n);
        std::fputs("  shapes:", stdout);
        for (const auto& p : s.shapes) std::printf(" %s=%u", p.first.c_str(), p.second);
        std::printf("\n");
        if (cid == 121 || cid == 124) {
            // correlate high flag bits with operand ranges: what IS bit 0x100/0x200?
            std::map<uint32_t, std::map<std::string, std::map<std::string, unsigned>>> d1;
            auto acc1 = [](std::map<std::string, unsigned>& m, int32_t v) {
                if (v >= 1600000 && v < 1600100) ++m["cself"];
                else if (v >= 2000000 && v < 3000000) ++m["sys"];
                else if (v >= 1000000000) ++m["db"];
                else if (v < 1600000) ++m["plain"];
                else ++m["other"];
            };
            for (const auto& e2 : ce.events)
                for (const auto& c2 : e2.commands) {
                    if (c2.cid != cid) continue;
                    const uint32_t fl = (c2.args.size() > 3) ? static_cast<uint32_t>(c2.args[3]) : 0;
                    // key = opcode+modifier bits only (keep opcode-ish 0x6000+, drop 0x100/0x200/0x400/0x800?)
                    const uint32_t key = fl & ~0x0F00u;
                    if (c2.args.size() > 1) acc1(d1[key][ "a1"], c2.args[1]);
                    if (c2.args.size() > 2) acc1(d1[key][ "a2"], c2.args[2]);
                }
            for (const auto& g : d1) {
                std::printf("  flag&~F00=%u:", g.first);
                for (const auto& slot : g.second) {
                    std::printf(" %s[", slot.first.c_str());
                    bool first = true;
                    for (const auto& q : slot.second) { std::printf("%s%s=%u", first ? "" : " ", q.first.c_str(), q.second); first = false; }
                    std::printf("]");
                }
                std::printf("\n");
            }
        }
        if (cid == 121 || cid == 124) {
            std::fputs("  arg0 buckets:", stdout);
            std::map<std::string, unsigned> b;
            for (const auto& p : s.f0) b[bucket(static_cast<int32_t>(p.first))] += p.second;
            for (const auto& p : b) std::printf(" %s=%u", p.first.c_str(), p.second);
            std::printf("\n");
            std::fputs("  arg0 max:", stdout);
            if (!s.f0.empty()) std::printf(" %u", s.f0.rbegin()->first);
            std::printf("\n");
        }
        if (cid == 121 || cid == 124) {
            for (const auto& e2 : ce.events)
                for (size_t ci = 0; ci < e2.commands.size(); ++ci) {
                    const auto& c2 = e2.commands[ci];
                    if (c2.cid != cid) continue;
                    bool special = c2.args.size() != 4;
                    if (!special && c2.args.size() == 4) {
                        const int32_t v1 = c2.args[1];
                        if (v1 < 0) special = true;
                        else if (v1 >= 1600100 && v1 < 2000000) special = true;
                        else if (v1 >= 3000000 && v1 < 1000000000) special = true;
                    }
                    if (!special) continue;
                    std::printf("  special ev%d #%zu:", e2.id, ci);
                    for (int32_t a : c2.args) std::printf(" %d", a);
                    for (const auto& s2 : c2.string_args) {
                        std::string esc;
                        for (unsigned char ch : s2) { char b[8]; std::snprintf(b, sizeof(b), "\\x%02X", ch); esc += b; }
                        std::printf(" s[%s]", esc.c_str());
                    }
                    std::printf("\n");
                }
        }
        if (cid == 111 || cid == 112) {
            std::fputs("  arg0:", stdout);
            for (const auto& p : s.f0) std::printf(" %u=%u", p.first, p.second);
            std::printf("\n");
            std::map<uint32_t, unsigned> ops;   // op opcode histogram: every 3rd arg bucket
            for (const auto& e2 : ce.events)
                for (const auto& c2 : e2.commands)
                    if (c2.cid == cid)
                        for (size_t k = 0; k < c2.args.size(); ++k)
                            if (k >= 1) ++ops[static_cast<uint32_t>(c2.args[k])];
            std::fputs("  argvals:", stdout);
            for (const auto& p : ops) std::printf(" %u=%u", p.first, p.second);
            std::printf("\n");
        }
        if (cid == 122) {
            std::fputs("  arg1:", stdout);
            for (const auto& p : s.f1) std::printf(" %u=%u", p.first, p.second);
            std::printf("\n");
        }
        if (cid == 124) {
            unsigned shown = 0;
            for (const auto& e2 : ce.events) {
                if (shown >= 40) break;
                for (size_t ci = 0; ci < e2.commands.size(); ++ci) {
                    if (shown >= 40) break;
                    const auto& c2 = e2.commands[ci];
                    if (c2.cid != 124) continue;
                    std::printf("  sample ev%d #%zu:", e2.id, ci);
                    for (int32_t a : c2.args) std::printf(" %d", a);
                    std::printf("\n");
                    ++shown;
                }
            }
        }
        if (cid == 250) {
            std::fputs("  arg3:", stdout);
            for (const auto& p : s.f3) std::printf(" %u=%u", p.first, p.second);
            std::printf("\n");
        }
        if (cid == 401) {
            std::fputs("  arg0:", stdout);
            for (const auto& p : s.f0) std::printf(" %u=%u", p.first, p.second);
            std::printf("\n");
        }
        if (cid == 179 || cid == 170 || cid == 176 || cid == 171 || cid == 498 || cid == 122) {
            unsigned shown = 0;
            for (const auto& e2 : ce.events)
                for (size_t ci = 0; ci < e2.commands.size(); ++ci) {
                    if (shown >= 12) break;
                    const auto& c2 = e2.commands[ci];
                    if (c2.cid != cid) continue;
                    std::printf("  l-sample ev%d #%zu:", e2.id, ci);
                    for (int32_t a : c2.args) std::printf(" %d", a);
                    for (const auto& s2 : c2.string_args)
                        if (!s2.empty()) std::printf(" s[%s]", s2.c_str());
                    std::printf("\n");
                    ++shown;
                }
        }
    }
    return 0;
}

static wolf::DatabasePair* load_db_pair(const char* projPath, const char* datPath, std::string& err) {
    auto proj = read_file(projPath);
    auto dat = read_file(datPath);
    wolf::DatabasePair* pair = new wolf::DatabasePair();
    if (!wolf::DatabasePair::parse_pair(proj, dat, *pair, err)) { delete pair; return nullptr; }
    std::fprintf(stderr, "[db pair ok types=%zu] %s\n", pair->types.size(), projPath);
    return pair;
}

static int cmd_commonrun(const char* cepath, int32_t id, const char* dbProjPath, const char* dbDatPath,
                         const char* cdbProjPath, const char* cdbDatPath, bool verbose, size_t cap) {
    auto cedata = read_file(cepath);
    std::string err;
    if (!wolf::is_plain_or_decrypt(cedata, err)) { std::fprintf(stderr, "ERROR: %s\n", err.c_str()); return 1; }
    wolf::Reader r(std::move(cedata));
    wolf::CommonEventDat ce;
    wolf::CommonEventDat::parse(r, ce, err);
    if (!err.empty()) { std::fprintf(stderr, "ERROR: %s\n", err.c_str()); return 1; }

    wolf::GameState st;
    if (dbProjPath && *dbProjPath) {
        std::fprintf(stderr, "[load db pair] %s\n", dbProjPath);
        std::string err2;
        wolf::DatabasePair* pair = load_db_pair(dbProjPath, dbDatPath, err2);
        if (!pair) { std::fprintf(stderr, "ERROR: db pair: %s\n", err2.c_str()); return 1; }
        st.ldb = pair;   // DataBase
    }
    if (cdbProjPath && *cdbProjPath) {
        std::fprintf(stderr, "[load cdb pair] %s\n", cdbProjPath);
        std::string err2;
        wolf::DatabasePair* pair = load_db_pair(cdbProjPath, cdbDatPath, err2);
        if (!pair) { std::fprintf(stderr, "ERROR: cdb pair: %s\n", err2.c_str()); return 1; }
        st.cdb = pair;   // CDataBase
    }

    wolf::Vm vm(st);
    vm.load(ce);
    wolf::TraceSink tr;
    tr.enabled = verbose;
    vm.set_trace(&tr);
    if (cap) vm.set_step_cap(cap);

    wolf::RunResult res;
    if (!vm.run(id, res)) {
        std::fprintf(stderr, "RUN FAILED: %s\n", res.err.c_str());
        std::fprintf(stderr, "warnings=%zu executed=%zu\n", res.warnings.size(), res.executed);
        for (size_t k = 0; k < res.warnings.size() && k < 8; ++k)
            std::fprintf(stderr, "  warn: %s\n", res.warnings[k].c_str());
        return 1;
    }

    std::printf("common %d executed=%zu steps=%zu db_reads=%zu db_writes=%zu warnings=%zu\n",
                id, res.executed, res.steps, res.db_reads, res.db_writes, res.warnings.size());
    std::fputs("per-cid:", stdout);
    for (const auto& p : res.cid_counts) std::printf(" %u:%zu", p.first, p.second);
    std::printf("\n");
    for (const auto& w : res.warnings)
        std::printf("  warn: %s\n", w.c_str());

    if (verbose) {
        // Final cself state of the top-level event
        std::printf("final globals (changed cells):\n");
        std::printf("  (cself frames discarded; use -t trutrace for in-run detail)\n");
    }
    return 0;
}

static int cmd_commoninfo(const char* path, int show) {
    auto data = read_file(path);
    std::string err;
    if (!wolf::is_plain_or_decrypt(data, err)) {
        std::fprintf(stderr, "ERROR: %s\n", err.c_str());
        return 1;
    }
    const size_t total = data.size();
    wolf::Reader r(std::move(data));
    wolf::CommonEventDat ce;
    wolf::CommonEventDat::parse(r, ce, err);
    if (!err.empty()) { std::fprintf(stderr, "ERROR: %s\n", err.c_str()); return 1; }
    std::printf("%s\n", path);
    std::printf("  events : %zu (file %zu bytes fully consumed)\n", ce.events.size(), total);
    std::printf("  cid histogram:\n");
    const int histogram_ids[] = { 101,102,103,111,112,121,122,124,130,140,150,162,170,180,
                                  201,210,212,213,220,240,250,270,290,300,401,420,421,498,499 };
    for (int id : histogram_ids) {
        unsigned count = 0;
        for (const auto& e : ce.events)
            for (const auto& c : e.commands)
                if (c.cid == static_cast<uint32_t>(id)) ++count;
        if (count) {
            wolf::Command probe;
            probe.cid = static_cast<uint32_t>(id);
            const char* nm = probe.name();
            std::printf("    %6u x cid %3d  (%s)\n", count, id, nm ? nm : "?");
        }
    }
    if (show > 0) {
        int shown = 0;
        for (const auto& e : ce.events) {
            if (shown++ >= show) break;
            std::printf("  event %5d  cmds=%4zu\n", e.id, e.commands.size());
        }
    }
    return 0;
}

static void print_tree(const std::map<int32_t, std::vector<int32_t>>& children,
                       int32_t id, int level) {
    for (int i = 0; i < level; ++i) std::fputs("  ", stdout);
    if (id == -1)
        std::fputs("root\n", stdout);
    else
        std::printf("node %d\n", id);
    const auto c = children.find(id);
    if (c != children.end())
        for (const int32_t child : c->second)
            print_tree(children, child, level + 1);
}

static int cmd_maptree(const char* path, bool tree) {
    auto data = read_file(path);
    std::string err;
    if (!wolf::is_plain_or_decrypt(data, err)) {
        std::fprintf(stderr, "ERROR: %s\n", err.c_str());
        return 1;
    }
    const size_t total = data.size();
    wolf::Reader r(std::move(data));
    wolf::MapTreeDat t;
    wolf::MapTreeDat::parse(r, t, err);
    if (!err.empty()) { std::fprintf(stderr, "ERROR: %s\n", err.c_str()); return 1; }

    std::printf("%s\n", path);
    std::printf("  head      : 10 bytes, all-zero (verified)\n");
    std::printf("  marker    : 0x%02X\n", t.marker);
    std::printf("  nodes     : %d (file %zu bytes fully consumed)\n", t.count, total);
    std::printf("  terminate : 0x%02X\n", t.terminator);
    if (t.nodes.empty()) return 0;

    std::map<int32_t, int32_t> parent_of;
    std::map<int32_t, std::vector<int32_t>> children;
    for (const auto& n : t.nodes) {
        if (!parent_of.emplace(n.id, n.parent).second)
            std::fprintf(stderr, "warning: duplicate map id %d\n", n.id);
        children[n.parent].push_back(n.id);
    }
    int max_depth = 0;
    for (const auto& n : t.nodes) {
        int d = 0;
        int32_t p = parent_of[n.id];
        while (p != -1 && parent_of.count(p) > 0 && d <= static_cast<int>(t.nodes.size())) {
            p = parent_of[p];
            ++d;
        }
        if (d > max_depth) max_depth = d;
    }
    std::printf("  max depth : %d\n", max_depth);
    if (tree) print_tree(children, -1, 0);
    return 0;
}

static int cmd_openstatus(const char* path) {
    auto data = read_file(path);
    std::string err;
    if (!wolf::is_plain_or_decrypt(data, err)) {
        std::fprintf(stderr, "ERROR: %s\n", err.c_str());
        return 1;
    }
    const size_t total = data.size();
    wolf::Reader r(std::move(data));
    wolf::MapTreeOpenStatusDat s;
    wolf::MapTreeOpenStatusDat::parse(r, s, err);
    if (!err.empty()) { std::fprintf(stderr, "ERROR: %s\n", err.c_str()); return 1; }

    unsigned open = 0;
    for (const uint8_t f : s.flags) open += f;
    std::printf("%s\n", path);
    std::printf("  head      : 20 bytes, all-zero (verified)\n");
    std::printf("  flags     : %d (file %zu bytes fully consumed)\n", s.count, total);
    std::printf("  open=1    : %u / %u\n", open, static_cast<unsigned>(s.flags.size()));
    std::printf("  terminate : 0x%02X\n", s.terminator);
    std::printf("  open map ids:");
    for (size_t i = 0; i < s.flags.size(); ++i)
        if (s.flags[i]) std::printf(" %zu", i);
    std::printf("\n");
    return 0;
}

static int cmd_dbinfo(const char* project_path, const char* dat_path,
                      int dump_type, int dump_row) {
    auto pdata = read_file(project_path);
    auto ddata = read_file(dat_path);
    wolf::DatabasePair db;
    std::string err;
    if (!wolf::DatabasePair::parse_pair(pdata, ddata, db, err)) {
        std::fprintf(stderr, "ERROR: %s\n", err.c_str());
        return 1;
    }

    size_t n_fields = 0, n_rows = 0, n_int_cells = 0, n_str_cells = 0, ftsz_sum = 0;
    for (const auto& t : db.types) {
        n_fields += t.fields.size();
        n_rows += t.data.size();
        for (const auto& d : t.data) {
            n_int_cells += d.int_values.size();
            n_str_cells += d.string_values.size();
        }
        ftsz_sum += static_cast<size_t>(t.field_type_list_size);
    }
    std::printf("%s + %s\n", project_path, dat_path);
    std::printf("  types          : %zu (project %zu B + dat %zu B fully consumed)\n",
                db.types.size(), pdata.size(), ddata.size());
    std::printf("  fields         : %zu\n", n_fields);
    std::printf("  data rows      : %zu\n", n_rows);
    std::printf("  cells          : %zu int + %zu string\n", n_int_cells, n_str_cells);
    std::printf("  ftsz sum       : %zu (field-type list bytes)\n", ftsz_sum);
    std::printf("  per type:\n");
    unsigned i = 0;
    for (const auto& t : db.types) {
        size_t fi = 0, fs = 0;
        for (const auto& f : t.fields) f.is_int() ? ++fi : ++fs;
        std::printf("    type %3u: len(name)=%2zu fields %3zu (int %3zu / str %3zu) rows %4zu\n",
                    i, t.name.size(), t.fields.size(), fi, fs, t.data.size());
        ++i;
    }
    if (dump_type >= 0) {
        if (dump_type < 0 || (size_t)dump_type >= db.types.size()) {
            std::fprintf(stderr, "type out of range\n");
        } else {
            const auto& t = db.types[(size_t)dump_type];
            if (dump_row < 0 || (size_t)dump_row >= t.data.size()) {
                std::fprintf(stderr, "row out of range (rows %zu)\n", t.data.size());
            } else {
                const auto& d = t.data[(size_t)dump_row];
                std::printf("type %d row %d: %zu int cells / %zu string cells\n",
                            dump_type, dump_row, d.int_values.size(), d.string_values.size());
                for (size_t k = 0; k < d.int_values.size(); ++k)
                    if (d.int_values[k] != 0)
                        std::printf("  int[%zu] = %lld\n", k, (long long)d.int_values[k]);
                for (size_t k = 0; k < d.string_values.size(); ++k)
                    if (!d.string_values[k].empty())
                        std::printf("  str[%zu] = \"%s\"\n", k, d.string_values[k].c_str());
            }
        }
    }
    return 0;
}

static int cmd_sysbasic(const char* path) {
    // SysDataBaseBasic.dat is a plain custom record (lead = 10 zero bytes), so the
    // game-dat plaintext probe is not applied here.
    auto data = read_file(path);
    const size_t total = data.size();
    wolf::Reader r(std::move(data));
    wolf::SysDbBasicDat s;
    std::string err;
    wolf::SysDbBasicDat::parse(r, s, err);
    if (!err.empty()) { std::fprintf(stderr, "ERROR: %s\n", err.c_str()); return 1; }

    size_t n_int_cells = 0, n_str_cells = 0, n_nonempty_str = 0, max_str = 0;
    for (const auto& t : s.types)
        for (size_t row = 0; row < t.int_rows.size(); ++row) {
            n_int_cells += t.int_rows[row].size();
            for (const auto& sv : t.str_rows[row]) {
                ++n_str_cells;
                if (!sv.empty()) {
                    ++n_nonempty_str;
                    if (sv.size() > max_str) max_str = sv.size();
                }
            }
        }
    std::printf("%s\n", path);
    std::printf("  head      : 10 bytes, all-zero + marker 0xC0 (verified)\n");
    std::printf("  types     : %d (file %zu B fully consumed)\n", s.type_count, total);
    std::printf("  cells     : %zu int + %zu string\n", n_int_cells, n_str_cells);
    std::printf("  terminator: 0x%02X\n", s.terminator);
    std::printf("  per type:\n");
    unsigned i = 0;
    for (const auto& t : s.types) {
        size_t fi = 0, fs = 0;
        for (const int32_t ix : t.indexinfos) (ix >= 0x07D0 ? ++fs : ++fi);
        std::printf("    type %2u: fields %2zu (int %2zu / str %2zu) rows %3d (capacity)\n",
                    i, t.indexinfos.size(), fi, fs, t.row_capacity);
        ++i;
    }
    std::printf("  strings   : %zu cells, %zu non-empty (values withheld), max len %zu\n",
                n_str_cells, n_nonempty_str, max_str);
    return 0;
}

static int cmd_tileset(const char* path, bool verbose) {
    auto data = read_file(path);
    std::string err;
    if (!wolf::is_plain_or_decrypt(data, err)) {
        std::fprintf(stderr, "ERROR: %s\n", err.c_str());
        return 1;
    }
    const size_t total = data.size();
    wolf::Reader r(std::move(data));
    wolf::TileSetDataDat tsd;
    wolf::TileSetDataDat::parse(r, tsd, err);
    if (!err.empty()) { std::fprintf(stderr, "ERROR: %s\n", err.c_str()); return 1; }

    std::printf("%s\n", path);
    std::printf("  head      : 10 bytes magic (WOLF / class 'M') verified\n");
    std::printf("  tag       : 0x%02X (mode %d)\n", tsd.tag, tsd.mode());
    std::printf("  records   : %d (file %zu bytes fully consumed)\n",
                static_cast<int>(tsd.records.size()), total);
    std::printf("  terminate : 0x%02X\n", tsd.terminator);
    unsigned i = 0;
    for (const auto& rec : tsd.records) {
        size_t str_bytes = 0;
        for (const auto& s : rec.strings) str_bytes += s.size();
        std::printf("    record %u (mode %d): strings %zu (+m0 %zu B), string bytes %zu, "
                    "flag bytes %4zu, flag ints %4zu / %zu B\n",
                    i, tsd.mode(), rec.strings.size(), rec.name.size(), str_bytes,
                    rec.flag_bytes.size(), rec.flag_ints.size(), rec.flag_ints.size() * 4);
        if (verbose) {
            std::printf("      m0 len %zu (incl NUL)\n", rec.name.size() + 1);
            for (size_t k = 0; k < rec.strings.size(); ++k)
                std::printf("      m%zu len %zu\n", k + 1, rec.strings[k].size() + 1);
        }
        ++i;
    }
    return 0;
}

// Engine reading of a page's start condition (0x482240 layout).
static std::string fmt_page_cond(const wolf::MapEventPage& pg) {
    const wolf::MapPageCond c = wolf::page_conditions(pg);
    std::string s = "trig=" + std::to_string(c.trigger);
    size_t n = 0;
    for (const auto& t : c.terms) {
        if (!t.enabled()) break;          // engine stops at the first disabled term
        s += n++ ? " && " : " if ";
        s += "[" + std::to_string(t.lhs) + "] " + wolf::comparator_name(t.comparator()) +
             " " + std::to_string(t.rhs);
    }
    if (!n) s += " always";
    return s;
}

static int cmd_mpsinfo(const char* path, bool verbose, bool census) {
    // .mps maps are plain records (10 zero bytes lead), so the game-dat crypt
    // probe is not applied — verified against all 105 map files of the target.
    auto data = read_file(path);
    const size_t total = data.size();
    wolf::Reader r(std::move(data));
    wolf::MapProjectDat mp;
    std::string err;
    wolf::MapProjectDat::parse(r, mp, err);
    if (!err.empty()) { std::fprintf(stderr, "ERROR: %s\n", err.c_str()); return 1; }

    size_t n_pages = 0, n_cmds = 0;
    for (const auto& e : mp.events) {
        n_pages += e.pages.size();
        for (const auto& p : e.pages) n_cmds += p.commands.size();
    }
    std::printf("%s\n", path);
    std::printf("  size      : %zu bytes fully consumed\n", total);
    std::printf("  magic     : WOLFM (fields 100 / 1381 verified)\n");
    std::printf("  copyright : \"%s\"\n", escape_bytes(mp.copyright).c_str());
    std::printf("  map       : %u x %u chips, layer-field %u, extra %u\n",
                mp.width, mp.height, mp.layer_count, mp.extra);
    std::printf("  planes    : 3 x %zu B\n", mp.planes.empty() ? 0 : mp.planes[0].size());
    std::printf("  events    : %zu records, %zu pages, %zu commands\n",
                mp.events.size(), n_pages, n_cmds);
    if (census) {
        // Stage-2 decode-priorities input: per-cid totals across every page.
        // Stage-3 addition: cid 210 arg[0] band tally — the Game.exe decode
        // (ENGINE_ANALYSIS §12.7) implies >=1e6 args are MAP-EV PAGE CALLS
        // (self-indexed keys), not common-event ids; this counts them.
        std::map<uint32_t, unsigned> h;
        unsigned n210 = 0;
        std::map<int, unsigned> band210;   // key = arg[0] band edge, see names[]
        for (const auto& e : mp.events)
            for (const auto& p : e.pages)
                for (const auto& c : p.commands) {
                    ++h[c.cid];
                    if (c.cid == 210 && !c.args.empty()) {
                        ++n210;
                        const int32_t v = c.args[0];
                        const int band =
                            v < 0 ? -1 :
                            v < 1000000 ? 0 :
                            v < 1100000 ? 1000000 :
                            v < 1200000 ? 1100000 :
                            v < 1600000 ? 1200000 : 1600000;
                        ++band210[band];
                    }
                }
        for (const auto& p : h) {
            wolf::Command probe;
            probe.cid = p.first;
            std::printf("  cid %6u x %6u  (%s)\n", p.first, p.second,
                        probe.name() ? probe.name() : "?");
        }
        if (n210) {
            std::printf("  cid 210 arg0 bands:");
            const int keys[] = { -1, 0, 1000000, 1100000, 1200000, 1600000 };
            const char* names[] = { "neg", "<1e6", "1e6-1.1e6", "1.1e6-1.2e6", "1.2e6-1.6e6", ">=1.6e6" };
            for (int i = 0; i < 6; ++i)
                if (band210.count(keys[i])) std::printf(" %s:%u", names[i], band210[keys[i]]);
            std::printf("\n");
        }
        return 0;
    }
    if (verbose) {
        for (const auto& e : mp.events) {
            std::printf("  event %d @(%d,%d) pages=%d name \"%s\"\n",
                        e.id, e.x, e.y, e.page_count, escape_bytes(e.name).c_str());
            for (size_t k = 0; k < e.pages.size(); ++k) {
                const auto& pg = e.pages[k];
                std::printf("    page %zu: cond %d gfx \"%s\" cmds %zu chips [%d %d %d %d]\n",
                            k, pg.condition, escape_bytes(pg.graphic_file).c_str(),
                            pg.commands.size(), pg.chips[0], pg.chips[1],
                            pg.chips[2], pg.chips[3]);
                // Stage-3 condition-location hunt: dump the still-open raw page
                // fields so a data census can diff anomalous (-0/52) conditions.
                auto hexes = [](const std::vector<uint8_t>& b) {
                    std::string s;
                    char buf[4];
                    for (uint8_t v : b) { std::snprintf(buf, sizeof(buf), "%02X ", v); s += buf; }
                    if (!s.empty()) s.pop_back();
                    return s;
                };
                std::printf("      head %s | flags %s | v16 %s | b10 %s\n",
                            hexes(pg.graphic_head).c_str(), hexes(pg.graphic_flags).c_str(),
                            hexes(pg.unknown16).c_str(), hexes(pg.block10).c_str());
                std::printf("      start %s\n", fmt_page_cond(pg).c_str());
            }
        }
    }
    return 0;
}

// ---------------------------------------------------------------------------
// selftest — in-memory VM regression net (no game data needed).
// Synthesizes CommonEventDat records and pins the semantics the sweep harness
// relies on, plus both classifier fault classes (step cap / recursion cap).
// Also PINS the current 600xxx early-terminate behavior (the v5-sweep-measured
// binary) — flip that test together with the vm.cpp TODO(verify) note.
// ---------------------------------------------------------------------------
static unsigned g_st_pass = 0, g_st_fail = 0;

static void st_expect(bool ok, const char* what) {
    if (ok) {
        ++g_st_pass;
        std::printf("  ok   %s\n", what);
    } else {
        ++g_st_fail;
        std::printf("  FAIL %s\n", what);
    }
}

static wolf::Command mk(uint32_t cid, std::vector<int32_t> args, uint8_t ind = 0) {
    wolf::Command c;
    c.cid = cid;
    c.args = std::move(args);
    c.indent = ind;
    return c;
}

static void adde(wolf::CommonEventDat& ce, int32_t id,
                 std::vector<wolf::Command> cmds, int32_t tail92 = 0) {
    wolf::CommonEventRecord rec;
    rec.id = id;
    rec.commands = std::move(cmds);
    rec.tail_int_92 = tail92;
    ce.events.push_back(std::move(rec));
}

static bool st_run(wolf::CommonEventDat& ce, int32_t id, wolf::GameState& st,
                   wolf::RunResult& out, size_t cap = 0) {
    wolf::Vm vm(st);
    vm.load(ce);
    if (cap) vm.set_step_cap(cap);
    return vm.run(id, out);
}

static const wolf::Cell& gc(const wolf::GameState& st, long long addr) {
    static const wolf::Cell zero;
    auto it = st.gvar.find(addr);
    return it == st.gvar.end() ? zero : it->second;
}

static int cmd_selftest() {
    std::printf("wolfvm selftest\n");

    {   // 121 assign: g(10) = v1 + v2
        wolf::CommonEventDat ce;
        adde(ce, 0, {mk(121, {10, 3, 5, 0})});
        wolf::GameState st;
        wolf::RunResult res;
        st_expect(st_run(ce, 0, st, res), "121 assign runs ok");
        st_expect(gc(st, 10).i == 8, "121 assign value 3+5=8");
    }

    {   // 121 accumulate — sys-band cell (2M..4M is the fully modeled read/write band;
        // plain <1.6M reads are literal-mode in the census'd semantics, so avoid). 
        wolf::CommonEventDat ce;
        adde(ce, 0, {mk(121, {2000005, 3, 5, 0}), mk(121, {2000005, 0, 7, 0x100})});
        wolf::GameState st;
        wolf::RunResult res;
        const bool ok = st_run(ce, 0, st, res);
        st_expect(ok, "121 assign+accum runs ok");
        st_expect(gc(st, 2000005).i == 15, "121 accumulate 8+(0+7)=15 (sys-band)");
    }

    {   // select TRUE: case1 body runs, else (420) body skipped
        wolf::CommonEventDat ce;
        adde(ce, 0, {
            mk(111, {0x11, 5, 5, 2}),        // 5 == 5 -> case_target 1
            mk(401, {1}),
            mk(121, {30, 7, 0, 0}, 1),       // case1 body: g30=7
            mk(420, {0}),
            mk(121, {30, 9, 0, 0}, 1),       // else body: g30=9 (must not run)
            mk(499, {}),
        });
        wolf::GameState st;
        wolf::RunResult res;
        st_expect(st_run(ce, 0, st, res), "select true runs ok");
        st_expect(gc(st, 30).i == 7, "select true takes case1 body only (g30=7)");
    }

    {   // select FALSE: register=2 matches the 420 else arm only
        wolf::CommonEventDat ce;
        adde(ce, 0, {
            mk(111, {0x11, 4, 5, 2}),        // 4 == 5 false -> case_target 2
            mk(401, {1}),
            mk(121, {30, 7, 0, 0}, 1),       // case1 body (must not run)
            mk(420, {0}),
            mk(121, {30, 9, 0, 0}, 1),       // else body: g30=9
            mk(499, {}),
        });
        wolf::GameState st;
        wolf::RunResult res;
        st_expect(st_run(ce, 0, st, res), "select false runs ok");
        st_expect(gc(st, 30).i == 9, "select false takes 420 else body only (g30=9)");
    }

    {   // 179 LoopTimes x3 — body accumulates cself[0] (census-faithful target),
        // then an assign copies it into a sys-band cell for post-run assertion.
        wolf::CommonEventDat ce;
        adde(ce, 0, {
            mk(121, {2000005, 0, 0, 0}),              // sys cell = 0
            mk(179, {3}),
            mk(121, {1600000, 0, 1, 0x100}, 1),       // cself[0] += 1
            mk(498, {}),
            mk(121, {2000005, 1600000, 0, 0}),        // sys = cself[0]
        });
        wolf::GameState st;
        wolf::RunResult res;
        st_expect(st_run(ce, 0, st, res), "179 loop runs ok");
        st_expect(gc(st, 2000005).i == 3, "179 loop runs body 3 times (cself[0]=3)");
    }

    {   // 179 loop with conditional 171 BreakLoop on iteration 3
        wolf::CommonEventDat ce;
        adde(ce, 0, {
            mk(179, {10}),
            mk(121, {1600000, 0, 1, 0x100}, 1),       // cself[0] += 1
            mk(111, {0x11, 1600000, 3, 1}, 1),        // cself[0] >= 3 (engine op 1) -> case_target 1
            mk(401, {1}, 1),
            mk(171, {}, 2),                           // break
            mk(420, {0}, 1),
            mk(499, {}, 1),
            mk(498, {}),
            mk(121, {2000005, 1600000, 0, 0}),        // sys = cself[0]
        });
        wolf::GameState st;
        wolf::RunResult res;
        st_expect(st_run(ce, 0, st, res), "179+171 break runs ok");
        st_expect(gc(st, 2000005).i == 3, "171 breaks on 3rd iteration (cself[0]=3, not 10)");
    }

    {   // 210 call with 2 int locals
        wolf::CommonEventDat ce;
        adde(ce, 0, {mk(210, {500002, 2, 3, 6})});
        adde(ce, 2, {mk(121, {50, 1600000, 1600001, 0})});   // g50 = cself0+cself1
        wolf::GameState st;
        wolf::RunResult res;
        st_expect(st_run(ce, 0, st, res), "210 int-call runs ok");
        st_expect(gc(st, 50).i == 9, "210 passes int locals (3+6=9)");
    }

    {   // 210 call with string-arg slotted to cself[5] (sys-band strings — the
        // fully modeled bank)
        wolf::CommonEventDat ce;
        std::vector<wolf::Command> driver = {
            mk(122, {3000080, 0}),            // sys string cell = "hello"
            mk(210, {500002, 0x12, 70, 71, 3000080}),
        };
        driver[0].string_args = {"hello"};
        adde(ce, 0, std::move(driver));
        adde(ce, 2, {mk(122, {3000051, 0, 1600005})});   // sys1 = cself[5] text
        wolf::GameState st;
        wolf::RunResult res;
        st_expect(st_run(ce, 0, st, res), "210 string-call runs ok");
        st_expect(gc(st, 3000051).as_str() == "hello", "210 binds string arg into cself[5]");
    }

    {   // 210 bit24 ret-bind chain: callee1 returns cself[5] -> driver cself[0];
        // callee2 then reads driver cself[0] as an int-valued string cell.
        wolf::CommonEventDat ce;
        adde(ce, 0, {
            mk(210, {500001, 0x1000000, 1600000}),          // ref-bind cself[0]
            mk(210, {500002, 1, 1600000}),                  // pass cself[0] back
        });
        adde(ce, 1, {mk(121, {1600005, 77, 0, 0})}, 5);     // cself[5]=77; tail_92=5
        adde(ce, 2, {mk(121, {60, 1600000, 0, 0})});        // g60 = cself[0]
        wolf::GameState st;
        wolf::RunResult res;
        st_expect(st_run(ce, 0, st, res), "210 ret-bind chain runs ok");
        st_expect(gc(st, 60).i == 77, "210 ret-bind round-trips (77)");
    }

    {   // PIN: 600xxx band terminates the run early (v5-measured semantics;
        // the marker at pc1 must stay untouched to prove no continue-past).
        wolf::CommonEventDat ce;
        adde(ce, 0, {
            mk(210, {600101, 2, 3, 6}),
            mk(121, {70, 1, 0, 0}),          // must NOT execute
        });
        wolf::GameState st;
        wolf::RunResult res;
        const bool ok = st_run(ce, 0, st, res);
        st_expect(ok && !res.ok && res.err.empty() && !res.warnings.empty() &&
                  res.warnings[0].find("600xxx") != std::string::npos,
                  "600xxx pins: note + early terminate (v5-measured)");
        st_expect(gc(st, 70).touched_i == false && gc(st, 70).touched_s == false,
                  "600xxx pins: nothing executes after the early return");
    }

    {   // 213 JumpLabel forward-scan to a 212 defined later
        wolf::CommonEventDat ce;
        adde(ce, 0, {
            mk(121, {10, 0, 1, 0}),          // g10 = 1
            mk(213, {}),                     // jump "fwd" (label below)
            mk(121, {20, 9, 9, 0}),          // skipped
            mk(212, {}),                     // "fwd" -> pc4
        });
        ce.events[0].commands[1].string_args = {"fwd"};
        ce.events[0].commands[3].string_args = {"fwd"};
        wolf::GameState st;
        wolf::RunResult res;
        st_expect(st_run(ce, 0, st, res), "213 fwd-label run ok");
        st_expect(gc(st, 10).i == 1 && gc(st, 20).touched_i == false,
                  "213 jumps forward past pc2 (g10=1, g20 unset)");
    }

    {   // RE-CAP classifier class: self-recursive 210 hits kMaxDepth
        wolf::CommonEventDat ce;
        adde(ce, 0, {mk(210, {500000, 0})});   // calls itself
        wolf::GameState st;
        wolf::RunResult res;
        st_expect(!st_run(ce, 0, st, res) && res.err == "210 recursion cap",
                  "reccap: 210 self-recursion stops at depth cap");
    }

    {   // STEP-CAP classifier class: 212/213 self-loop spins out
        wolf::CommonEventDat ce;
        adde(ce, 0, {mk(212, {}), mk(213, {})});
        ce.events[0].commands[0].string_args = {"top"};
        ce.events[0].commands[1].string_args = {"top"};
        wolf::GameState st;
        wolf::RunResult res;
        const bool ran = st_run(ce, 0, st, res, 10);
        st_expect(!ran && res.err.find("step cap") != std::string::npos,
                  "stepcap: runaway 212/213 loop hits step cap");
    }

    // --- map_runtime MODEL pins (Stage-3 skeleton; semantics are MODEL,
    // NOT engine claims — real page selection is TODO verify Game.exe).
    {
        wolf::MapProjectDat mp;
        wolf::MapEventRecord rec;
        rec.id = 7; rec.page_count = 2;
        wolf::MapEventPage p0; p0.condition = -1; rec.pages.push_back(p0);
        wolf::MapEventPage p1; p1.condition = -1; rec.pages.push_back(p1);
        mp.events.push_back(rec);
        wolf::MapRuntime rt(mp);
        wolf::CondDefaultPred pred;
        rt.tick(0, pred);
        st_expect(rt.objects()[0].active_page == 1,
                  "model: all(-1) 2-page record picks top-down last page");
        rt.erase(7);
        st_expect(rt.objects()[0].erased && rt.objects()[0].active_page == -1,
                  "model: 173-style erase clears active page");
        rt.tick(0, pred);
        st_expect(rt.objects()[0].erased && rt.objects()[0].active_page == -1,
                  "model: erase is sticky across tick");
        wolf::MapProjectDat mp2;
        wolf::MapEventRecord rec2;
        rec2.id = 1; rec2.page_count = 1;
        wolf::MapEventPage q0; q0.condition = 0; rec2.pages.push_back(q0);
        mp2.events.push_back(rec2);
        wolf::MapRuntime rt2(mp2);
        rt2.tick(0, pred);
        st_expect(rt2.objects()[0].active_page == -1,
                  "model: cond!=(-1) page inert under default pred (0 x12/52 x1 band open)");
    }

    // --- ENGINE pins: page condition evaluator 0x482240 + pick 0x44B530
    // (byte-read 2026-10-22, ENGINE_ANALYSIS §14.3).
    {
        auto mkpage = [](std::vector<std::tuple<uint8_t, int32_t, int32_t>> terms) {
            wolf::MapEventPage p;
            p.condition = -1;
            p.graphic_head = {2, 0, 0xFF, 0, 0};
            p.graphic_flags = {0x20, 0x20, 0x20, 0x20};
            p.chips = {1000000, 1000000, 1000000, 1000000};
            p.unknown16.assign(16, 0);
            for (size_t i = 0; i < terms.size() && i < 4; ++i) {
                p.graphic_flags[i] = std::get<0>(terms[i]);
                p.chips[i] = std::get<1>(terms[i]);
                const uint32_t v = (uint32_t)std::get<2>(terms[i]);
                for (int b = 0; b < 4; ++b) p.unknown16[i * 4 + b] = (uint8_t)(v >> (8 * b));
            }
            return p;
        };
        wolf::MapVarStore vs;
        wolf::EnginePagePred ep(vs);
        // comparator table: (op<<4)|1 against V=5
        vs.set(2000035, 5);
        const int32_t want[7][3] = { // rhs 4,5,6 -> expected for each op
            {1,0,0}, {1,1,0}, {0,1,0}, {0,1,1}, {0,0,1}, {1,0,1}, {1,1,0} };
        bool tbl = true;
        for (int op = 0; op < 7; ++op)
            for (int r = 0; r < 3; ++r) {
                auto pg = mkpage({{(uint8_t)((op << 4) | 1), 2000035, 4 + r}});
                if ((int)ep.page_ok(pg, 0, 1, 0, 0) != want[op][r]) tbl = false;
            }
        st_expect(tbl, "engine: page comparator table 0..6 (> >= == <= < != &=)");
        auto pgbad = mkpage({{0x71, 2000035, 5}});
        st_expect(!ep.page_ok(pgbad, 0, 1, 0, 0), "engine: comparator >6 fails the page");
        // prefix semantics: term0 disabled -> true even if term1 would fail
        auto pgpre = mkpage({{0x20, 2000035, 0}, {0x21, 2000035, 999}});
        st_expect(ep.page_ok(pgpre, 0, 1, 0, 0), "engine: first disabled term ends the list (true)");
        // self-var rebasing: 1100002 -> 1e6 + id*10 + 2
        wolf::MapProjectDat mp;
        wolf::MapEventRecord door;
        door.id = 9; door.page_count = 2;
        door.pages.push_back(mkpage({{0x21, 1100002, 0}}));
        door.pages.push_back(mkpage({{0x21, 1100002, 1}}));
        mp.events.push_back(door);
        wolf::MapRuntime rt(mp);
        rt.tick(0, ep);
        st_expect(rt.objects()[0].active_page == 0, "engine: door SelfVar2==0 -> page 0");
        vs.set_self(9, 2, 1);
        rt.tick(1, ep);
        st_expect(rt.objects()[0].active_page == 1, "engine: door SelfVar2==1 -> page 1 (top-down)");
        vs.set_self(9, 2, 7);
        rt.tick(2, ep);
        st_expect(rt.objects()[0].active_page == -1, "engine: no page holds -> -1");
        // rhs band asymmetry: rhs 1000003 is NOT rebased (literal key read)
        vs.set(1000003, 42);
        auto pgr = mkpage({{0x21, 2000035, 1000003}});
        vs.set(2000035, 42);
        st_expect(ep.page_ok(pgr, 0, 1, 9, 0), "engine: rhs 1.0e6 band read unrebased");
        // op 6 compares (a&b) with the RAW rhs field
        vs.set(2000035, 0x0F);
        auto pg6 = mkpage({{0x61, 2000035, 6}});
        st_expect(ep.page_ok(pg6, 0, 1, 0, 0), "engine: op6 (a&b)==rhs");
    }

    std::printf("selftest %s  (pass %u, fail %u)\n",
                g_st_fail ? "FAILED" : "PASS", g_st_pass, g_st_fail);
    return g_st_fail ? 1 : 0;
}

// ---------------------------------------------------------------------------
// sweep — native per-id CommonEvent sweep, mirroring the external PowerShell
// harness + classifier (ENGINE_ANALYSIS §12.5): fresh GameState per id, same
// step-cap default (2e6), classes OK / stePCAP / reccap / other. This replaces
// the machine-local harness so a sweep can run from wolfvm alone once the
// game data directory is present.
//   wolfvm sweep <CommonEvent.dat> [--db P D] [--cdb P D] [--from A] [--to B] [--cap N]
// Classifier: run() true -> OK (includes the 600xxx early-terminate, which the
// external harness also scored OK on process exit); false -> step cap =>
// stepcap, "210 recursion cap" => reccap, anything else => other.
// ---------------------------------------------------------------------------
static int cmd_sweep(const char* cepath, const char* dbproj, const char* dbdat,
                     const char* cdbproj, const char* cdbdat,
                     int32_t from, int32_t to, size_t cap) {
    auto cedata = read_file(cepath);
    std::string err;
    if (!wolf::is_plain_or_decrypt(cedata, err)) { std::fprintf(stderr, "ERROR: %s\n", err.c_str()); return 1; }
    wolf::Reader r(std::move(cedata));
    wolf::CommonEventDat ce;
    wolf::CommonEventDat::parse(r, ce, err);
    if (!err.empty()) { std::fprintf(stderr, "ERROR: %s\n", err.c_str()); return 1; }

    // Pristine DB templates; restored whenever a sweep step wrote DB cells
    // (the external harness ran one process per id = fresh DB per id).
    wolf::DatabasePair ldb_pris, cdb_pris;
    bool have_ldb = false, have_cdb = false;
    if (dbproj && *dbproj) {
        std::string e2;
        have_ldb = wolf::DatabasePair::parse_pair(read_file(dbproj), read_file(dbdat), ldb_pris, e2);
        if (!have_ldb) { std::fprintf(stderr, "ERROR: db pair: %s\n", e2.c_str()); return 1; }
    }
    if (cdbproj && *cdbproj) {
        std::string e2;
        have_cdb = wolf::DatabasePair::parse_pair(read_file(cdbproj), read_file(cdbdat), cdb_pris, e2);
        if (!have_cdb) { std::fprintf(stderr, "ERROR: cdb pair: %s\n", e2.c_str()); return 1; }
    }

    int32_t max_id = 0;
    for (const auto& e : ce.events) if (e.id > max_id) max_id = e.id;
    if (to < 0) to = max_id;
    std::fprintf(stderr, "[sweep] %s ids [%d..%d] db=%d cdb=%d cap=%zu\n",
                 cepath, from, to, (int)have_ldb, (int)have_cdb, cap ? cap : (size_t)2000000);

    size_t n_ok = 0, n_stepcap = 0, n_reccap = 0, n_other = 0, n_gap = 0;
    std::string l_other, l_stepcap, l_reccap;
    wolf::DatabasePair ldb_val, cdb_val;
    bool dirty = true;

    for (int32_t id = from; id <= to; ++id) {
        const wolf::CommonEventRecord* found = nullptr;
        for (const auto& e : ce.events) if (e.id == id) { found = &e; break; }
        if (!found) { ++n_gap; std::printf("%5d GAP (id not in file)\n", id); continue; }
        if (dirty) {
            if (have_ldb) ldb_val = ldb_pris;
            if (have_cdb) cdb_val = cdb_pris;
            dirty = false;
        }
        wolf::GameState st;
        if (have_ldb) st.ldb = &ldb_val;
        if (have_cdb) st.cdb = &cdb_val;
        wolf::Vm vm(st);
        vm.load(ce);
        if (cap) vm.set_step_cap(cap);
        wolf::RunResult res;
        const bool ok = vm.run(id, res);
        const char* cls = "OK";
        if (ok) {
            // run() true = exit-code-0 equivalence with the external harness.
            // (res.ok stays false on the 600xxx early-terminate; still OK.)
            ++n_ok;
        } else if (res.err.find("step cap") != std::string::npos) {
            cls = "STEPCAP"; ++n_stepcap; l_stepcap += std::to_string(id) + " ";
        } else if (res.err.find("recursion cap") != std::string::npos) {
            cls = "RECCAP"; ++n_reccap; l_reccap += std::to_string(id) + " ";
        } else {
            cls = "OTHER"; ++n_other; l_other += std::to_string(id) + " ";
        }
        std::printf("%5d %-7s exec=%-6zu warns=%-4zu%s\n", id, cls, res.executed,
                    res.warnings.size(), ok ? "" : res.err.c_str());
        if (res.db_writes) dirty = true;
    }

    std::printf("[sweep] ids %d..%d  OK=%zu  stepcap=%zu  reccap=%zu  other=%zu  gap=%zu\n",
                from, to, n_ok, n_stepcap, n_reccap, n_other, n_gap);
    std::printf("[sweep] other   : %s\n", l_other.c_str());
    std::printf("[sweep] stepcap : %s\n", l_stepcap.c_str());
    std::printf("[sweep] reccap  : %s\n", l_reccap.c_str());
    return 0;
}

// ---------------------------------------------------------------------------
// mpssweep — run every map-event page of one .mps through the CommonEvent VM
// kernel (Stage-2 headless runner). Synthesizes a CommonEventDat with one
// record per page (synthetic id = record_index*1000 + page_index; the shipped
// rec id is printed alongside). (approx) semantics documented here:
//   * page activation conditions are NOT modeled — the engine's MapX object
//     decides which page is active (traits/flags/graphics/state), Stage 3.
//   * fresh GameState per page; DB pairs restored to pristine on writes
//     (mirrors `sweep` isolation); cself = the same 100-slot template.
// Purpose: classification over map-shaped command data + decode backlog
// (map-side census: 173 EraseEvent x106, 126 BanInput x2, 125 AutoInput x1).
//   wolfvm mpssweep <X.mps> [--db P D] [--cdb P D] [--from A] [--to B] [--cap N]
// ---------------------------------------------------------------------------
static int cmd_mpssweep(const char* mpspath, const char* cepath, const char* dbproj, const char* dbdat,
                        const char* cdbproj, const char* cdbdat,
                        int32_t from, int32_t to, size_t cap) {
    auto mpsdata = read_file(mpspath);
    std::string err;
    wolf::Reader r(std::move(mpsdata));
    wolf::MapProjectDat mp;
    wolf::MapProjectDat::parse(r, mp, err);
    if (!err.empty()) { std::fprintf(stderr, "ERROR: %s\n", err.c_str()); return 1; }

    // Synthesize one CommonEventDat record per map page. Synthetic ids live in
    // their own band (1000000 + sequential index) so real common-event ids can
    // coexist in the same table (map pages 210-call real common ids — engine
    // resolves callees from the game's loaded common table).
    const int32_t base_id = 1000000;   // first synthetic id
    wolf::CommonEventDat ce;
    std::vector<std::pair<int32_t, int32_t>> synth_meta;   // (rec id, page idx)
    if (cepath && *cepath) {
        wolf::Reader rc(read_file(cepath));
        wolf::CommonEventDat::parse(rc, ce, err);
        if (!err.empty()) { std::fprintf(stderr, "ERROR: %s\n", err.c_str()); return 1; }
        std::fprintf(stderr, "[mpssweep] merged %zu common events\n", ce.events.size());
    }
    for (size_t ri = 0; ri < mp.events.size(); ++ri) {
        const auto& e = mp.events[ri];
        for (size_t pi = 0; pi < e.pages.size(); ++pi) {
            wolf::CommonEventRecord ev;
            ev.id = base_id + (int32_t)synth_meta.size();
            ev.name = "map rec " + std::to_string(e.id) + " page " + std::to_string(pi);
            ev.commands = e.pages[pi].commands;
            ev.tail_strings_100.resize(100);   // cself template parity with real records
            synth_meta.push_back({ e.id, (int32_t)pi });
            ce.events.push_back(std::move(ev));
        }
    }
    const int32_t n_pages = (int32_t)synth_meta.size();

    wolf::DatabasePair ldb_pris, cdb_pris;
    bool have_ldb = false, have_cdb = false;
    if (dbproj && *dbproj) {
        std::string e2;
        have_ldb = wolf::DatabasePair::parse_pair(read_file(dbproj), read_file(dbdat), ldb_pris, e2);
        if (!have_ldb) { std::fprintf(stderr, "ERROR: db pair: %s\n", e2.c_str()); return 1; }
    }
    if (cdbproj && *cdbproj) {
        std::string e2;
        have_cdb = wolf::DatabasePair::parse_pair(read_file(cdbproj), read_file(cdbdat), cdb_pris, e2);
        if (!have_cdb) { std::fprintf(stderr, "ERROR: cdb pair: %s\n", e2.c_str()); return 1; }
    }

    const int32_t max_id = base_id + n_pages - 1;
    if (to < 0) to = max_id;
    if (to > max_id) to = max_id;
    if (from < base_id) from = base_id;
    std::fprintf(stderr, "[mpssweep] %s IDs %d..%d db=%d cdb=%d cap=%zu\n",
                 mpspath, from, to, (int)have_ldb, (int)have_cdb, cap ? cap : (size_t)2000000);

    size_t n_ok = 0, n_stepcap = 0, n_reccap = 0, n_other = 0, n_gap = 0;
    std::map<uint32_t, size_t> total_cids;
    wolf::DatabasePair ldb_val, cdb_val;
    bool dirty = true;

    for (int32_t u = from; u <= to; ++u) {
        if (u - base_id >= n_pages) { ++n_gap; continue; }
        if (dirty) {
            if (have_ldb) ldb_val = ldb_pris;
            if (have_cdb) cdb_val = cdb_pris;
            dirty = false;
        }
        wolf::GameState st;
        if (have_ldb) st.ldb = &ldb_val;
        if (have_cdb) st.cdb = &cdb_val;
        wolf::Vm vm(st);
        vm.load(ce);
        if (cap) vm.set_step_cap(cap);
        wolf::RunResult res;
        const bool ok = vm.run(u, res);
        const char* cls = "OK";
        if (ok) {
            ++n_ok;
        } else if (res.err.find("step cap") != std::string::npos) {
            cls = "STEPCAP"; ++n_stepcap;
        } else if (res.err.find("recursion cap") != std::string::npos) {
            cls = "RECCAP"; ++n_reccap;
        } else {
            cls = "OTHER"; ++n_other;
        }
        for (const auto& p : res.cid_counts) total_cids[p.first] += p.second;
        std::printf("id=%7d rec=%3d p%2d %-7s exec=%-6zu warns=%-4zu%s\n",
                    u, synth_meta[(size_t)(u - base_id)].first, synth_meta[(size_t)(u - base_id)].second, cls,
                    res.executed, res.warnings.size(), ok ? "" : res.err.c_str());
        if (res.db_writes) dirty = true;
    }

    std::printf("[mpssweep] IDs %d..%d  OK=%zu  stepcap=%zu  reccap=%zu  other=%zu  gap=%zu\n",
                from, to, n_ok, n_stepcap, n_reccap, n_other, n_gap);
    std::printf("[mpssweep] pages census (cid x count):\n");
    for (const auto& p : total_cids) {
        wolf::Command probe;
        probe.cid = p.first;
        std::printf("  cid %6u x %6zu  (%s)\n", p.first, p.second,
                    probe.name() ? probe.name() : "?");
    }
    return 0;
}

// mpspagewalk — Stage-3 page-field census over every .mps in a directory.
// One machine-parseable line per page + aggregates, printed from native code
// (a shell-side census was noisy to the point of unusable). Purpose: pin the
// still-open per-page fields (v16 / block10 / graphic flags / head) against
// page position and condition value as data facts for the MapX object work
// (their ENGINE reading stays open — TODO verify against Game.exe loader).
static int cmd_mpspagewalk(const char* dirpath) {
    namespace fs = std::filesystem;
    std::vector<fs::path> files;
    for (const auto& it : fs::directory_iterator(dirpath))
        if (it.is_regular_file() && it.path().extension() == ".mps") files.push_back(it.path());
    std::sort(files.begin(), files.end());
    if (files.empty()) { std::fprintf(stderr, "no .mps under %s\n", dirpath); return 1; }

    size_t n_rec = 0, n_pg = 0, n_idx_match = 0, n_idx_mismatch = 0;
    std::map<int32_t, size_t> h_cond, h_v0, h_b4, h_f0, h_h0, h_h1, h_pgc;
    std::map<std::string, size_t> h_combo;                    // b4:idx:i0 triple
    std::map<int32_t, size_t> h_trig, h_cmp, h_nterms;        // engine-read condition block
    std::map<std::string, size_t> h_lhsband;
    size_t n_tail_enabled = 0, n_disabled_nondefault = 0;

    for (const auto& f : files) {
        auto data = read_file(f.string().c_str());
        std::string err;
        wolf::Reader r(std::move(data));
        wolf::MapProjectDat mp;
        wolf::MapProjectDat::parse(r, mp, err);
        if (!err.empty()) { std::fprintf(stderr, "ERROR %s: %s\n", f.filename().string().c_str(), err.c_str()); continue; }
        const std::string base = f.stem().string();
        for (const auto& e : mp.events) {
            ++n_rec;
            ++h_pgc[e.pages.size()];
            for (size_t k = 0; k < e.pages.size(); ++k) {
                const auto& pg = e.pages[k];
                ++n_pg;
                const int32_t i0 = pg.unknown16.size() >= 4
                    ? (int32_t)((uint32_t)pg.unknown16[0] | ((uint32_t)pg.unknown16[1] << 8) |
                                ((uint32_t)pg.unknown16[2] << 16) | ((uint32_t)pg.unknown16[3] << 24))
                    : -999;
                const int32_t b4 = pg.block10.size() >= 5 ? pg.block10[4] : -999;
                const int32_t f0 = pg.graphic_flags.size() >= 1 ? pg.graphic_flags[0] : -999;
                const int32_t h0 = pg.graphic_head.size() >= 1 ? pg.graphic_head[0] : -999;
                const int32_t h1 = pg.graphic_head.size() >= 2 ? pg.graphic_head[1] : -999;
                std::printf("pg file=%s rec=%d x=%d y=%d n=%d k=%zu cond=%d i0=%d b4=%02X f0=%02X h0=%02X h1=%02X\n",
                            base.c_str(), e.id, e.x, e.y, e.page_count, k,
                            pg.condition, i0, b4 & 0xFF, f0 & 0xFF, h0 & 0xFF, h1 & 0xFF);
                ++h_cond[pg.condition]; ++h_v0[i0]; ++h_b4[b4]; ++h_f0[f0]; ++h_h0[h0]; ++h_h1[h1];
                {
                    const wolf::MapPageCond c = wolf::page_conditions(pg);
                    ++h_trig[c.trigger];
                    size_t nt = 0; bool tail_enabled = false, stopped = false;
                    for (const auto& t : c.terms) {
                        if (!t.enabled()) { stopped = true; continue; }
                        if (stopped) tail_enabled = true;      // enabled after a disabled term
                        else { ++nt; ++h_cmp[t.comparator()];
                               const long long L = t.lhs;
                               const char* band = L < 1000000 ? "<1e6" : L < 1100000 ? "1.0e6(map self)" :
                                   L < 1200000 ? "1.1e6(this self)" : L < 2000000 ? "1.2e6..2e6" :
                                   L < 3000000 ? "2e6" : L < 4000000 ? "3e6" : L < 1000000000 ? "4e6..1e9" : ">=1e9(db)";
                               ++h_lhsband[band]; }
                    }
                    ++h_nterms[(int32_t)nt];
                    if (tail_enabled) ++n_tail_enabled;
                    for (const auto& t : c.terms)
                        if (!t.enabled() && t.lhs != 1000000) ++n_disabled_nondefault;
                }
                if (i0 == (int32_t)k) ++n_idx_match; else ++n_idx_mismatch;
                char b4hex[8]; std::snprintf(b4hex, sizeof(b4hex), "%02X", (unsigned)(b4 & 0xFF));
                char i0hex[16]; std::snprintf(i0hex, sizeof(i0hex), "%08X", (unsigned)i0);
                std::string tri = std::string("b4") + b4hex + " idx" + std::to_string(k) + " i0" + i0hex;
                ++h_combo[tri];
            }
        }
    }
    std::printf("== mpspagewalk: %zu files, %zu records, %zu pages\n", files.size(), n_rec, n_pg);
    auto dump = [](const char* nm, const auto& m) {
        std::printf("  %s:", nm);
        for (const auto& p : m) std::printf(" %d/%zu", p.first, p.second);
        std::printf("\n");
    };
    dump("cond", h_cond);
    dump("v0", h_v0);
    dump("b4", h_b4);
    dump("f0", h_f0);
    dump("h0", h_h0);
    dump("h1", h_h1);
    dump("pagesPerRecord", h_pgc);
    std::printf("  i0==idx: %zu   i0!=idx: %zu\n", n_idx_match, n_idx_mismatch);
    dump("trigger(head4)", h_trig);
    dump("enabledTermsPerPage", h_nterms);
    dump("comparator", h_cmp);
    std::printf("  lhsBand:");
    for (const auto& p : h_lhsband) std::printf(" %s/%zu", p.first.c_str(), p.second);
    std::printf("\n  enabled-after-disabled terms: %zu   disabled terms with lhs!=1000000: %zu\n",
                n_tail_enabled, n_disabled_nondefault);
    std::printf("  (b4, idx, i0) top combos:\n");
    size_t shown = 0;
    for (const auto& p : h_combo) {
        if (++shown > 16) break;
        std::printf("    %-14s x %zu\n", p.first.c_str(), p.second);
    }
    return 0;
}

// mapobj — Stage-3 headless map-object demo (MODEL, approx).
// Loads one .mps, builds the MapRuntime object layer with the default
// (cond==-1) predicate, ticks N times, prints per-object model state.
// Purpose: exercise the object-layer API shape the verified engine work
// will plug into; output is explicitly NOT engine truth (page selection
// TODO verify — see ENGINE_ANALYSIS §14.2).
static int cmd_mapobj(const char* path, uint64_t ticks,
                      const std::vector<std::pair<long long, long long>>& sets) {
    auto data = read_file(path);
    wolf::Reader r(std::move(data));
    wolf::MapProjectDat mp;
    std::string err;
    wolf::MapProjectDat::parse(r, mp, err);
    if (!err.empty()) { std::fprintf(stderr, "ERROR: %s\n", err.c_str()); return 1; }

    wolf::MapVarStore vs;
    for (const auto& kv : sets) vs.set(kv.first, kv.second);
    wolf::EnginePagePred pred(vs);
    wolf::MapRuntime rt(mp);
    for (uint64_t t = 0; t < ticks; ++t) rt.tick(t, pred);

    std::printf("%s: %zu records, %zu pages\n", path, mp.events.size(), [&] {
        size_t n = 0;
        for (const auto& e : mp.events) n += e.pages.size();
        return n;
    }());
    size_t act = 0, inactive = 0, erased = 0;
    for (size_t i = 0; i < rt.objects().size(); ++i) {
        const auto& o = rt.objects()[i];
        const auto& e = mp.events[i];
        if (o.erased) ++erased; else if (o.active_page >= 0) ++act; else ++inactive;
        std::printf("obj %d \"%.32s\" @(%d,%d) pages %d active %d%s",
                    e.id, escape_bytes(e.name).c_str(), e.x, e.y, e.page_count,
                    o.active_page, o.erased ? " [erased]" : "");
        if (o.active_page >= 0)
            std::printf("  {%s}", fmt_page_cond(e.pages[(size_t)o.active_page]).c_str());
        std::printf("\n");
    }
    std::printf("[mapobj] active=%zu inactive=%zu erased=%zu  (page pick ENGINE-TRUE"
                " 0x44B530/0x482240; vars default 0 unless --set; self key = record id)\n",
                act, inactive, erased);
    return 0;
}

// run_cli — the whole CLI dispatch, reusable as a library entry point (the
// Android shell calls it via jni_bridge with an argv it builds itself).
int run_cli(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);   // survive crashes: unbuffered output
    if (argc >= 3 && std::strcmp(argv[1], "datinfo") == 0)    return cmd_datinfo(argv[2]);
    if (argc >= 3 && std::strcmp(argv[1], "gameinfo") == 0)   return cmd_gameinfo(argv[2]);
    if (argc >= 3 && std::strcmp(argv[1], "commoninfo") == 0) {
        int show = (argc >= 5 && !std::strcmp(argv[3], "-n")) ? std::atoi(argv[4]) : 0;
        return cmd_commoninfo(argv[2], show);
    }
    if (argc >= 3 && std::strcmp(argv[1], "commondump") == 0) {
        if (argc >= 4 && !std::strcmp(argv[3], "-h"))
            return cmd_commondump(argv[2], 0, true);
        int id = (argc >= 4) ? std::atoi(argv[3]) : 0;
        return cmd_commondump(argv[2], id, false);
    }
    if (argc >= 3 && std::strcmp(argv[1], "commonstat") == 0) {
        return cmd_commonstat(argv[2]);
    }
    if (argc >= 3 && std::strcmp(argv[1], "commonrun") == 0) {
        if (argc < 4) { std::fprintf(stderr, "commonrun requires an event id\n"); return 1; }
        int32_t id = (int32_t)std::atoi(argv[3]);
        const char* dbproj = nullptr;
        const char* dbdat = nullptr;
        const char* cdbproj = nullptr;
        const char* cdbdat = nullptr;
        bool verbose = false, verbose_trace = false;
        size_t cap = 0;
        for (int k = 4; k < argc; ++k) {
            if (!std::strcmp(argv[k], "-v")) verbose = true;
            else if (!std::strcmp(argv[k], "-t")) verbose_trace = true;
            else if (!std::strcmp(argv[k], "--cap")) {
                if (k + 1 >= argc) { std::fprintf(stderr, "--cap needs a number\n"); return 1; }
                cap = (size_t)std::strtoull(argv[k + 1], nullptr, 10);
                ++k;
            } else if (!std::strcmp(argv[k], "--db")) {
                if (k + 2 >= argc) { std::fprintf(stderr, "--db needs project dat\n"); return 1; }
                dbproj = argv[k + 1];
                dbdat = argv[k + 2];
                k += 2;
            } else if (!std::strcmp(argv[k], "--cdb")) {
                if (k + 2 >= argc) { std::fprintf(stderr, "--cdb needs project dat\n"); return 1; }
                cdbproj = argv[k + 1];
                cdbdat = argv[k + 2];
                k += 2;
            } else {
                std::fprintf(stderr, "unknown option %s\n", argv[k]);
                return 1;
            }
        }
        return cmd_commonrun(argv[2], id, dbproj, dbdat, cdbproj, cdbdat, verbose || verbose_trace, cap);
    }
    if (argc >= 3 && std::strcmp(argv[1], "maptree") == 0) {
        const bool tree = (argc >= 4 && !std::strcmp(argv[3], "-t"));
        return cmd_maptree(argv[2], tree);
    }
    if (argc >= 3 && std::strcmp(argv[1], "openstatus") == 0)
        return cmd_openstatus(argv[2]);
    if (argc >= 4 && std::strcmp(argv[1], "dbinfo") == 0)
        return cmd_dbinfo(argv[2], argv[3],
                          argc >= 5 ? std::atoi(argv[4]) : -1,
                          argc >= 6 ? std::atoi(argv[5]) : 0);
    if (argc >= 3 && std::strcmp(argv[1], "sysbasic") == 0)
        return cmd_sysbasic(argv[2]);
    if (argc >= 3 && std::strcmp(argv[1], "tileset") == 0) {
        const bool verbose = (argc >= 4 && !std::strcmp(argv[3], "-v"));
        return cmd_tileset(argv[2], verbose);
    }
    if (argc >= 3 && std::strcmp(argv[1], "mpsinfo") == 0) {
        const bool verbose = (argc >= 4 && !std::strcmp(argv[3], "-v"));
        const bool census  = (argc >= 4 &&  std::strcmp(argv[3], "-c") == 0);
        return cmd_mpsinfo(argv[2], verbose, census);
    }
    if (argc >= 2 && std::strcmp(argv[1], "selftest") == 0)
        return cmd_selftest();
    if (argc >= 3 && std::strcmp(argv[1], "sweep") == 0) {
        const char* dbproj = nullptr;
        const char* dbdat = nullptr;
        const char* cdbproj = nullptr;
        const char* cdbdat = nullptr;
        int32_t from = 0, to = -1;
        size_t cap = 0;
        for (int k = 3; k < argc; ++k) {
            if (!std::strcmp(argv[k], "--db")) {
                if (k + 2 >= argc) { std::fprintf(stderr, "--db needs project dat\n"); return 1; }
                dbproj = argv[k + 1]; dbdat = argv[k + 2]; k += 2;
            } else if (!std::strcmp(argv[k], "--cdb")) {
                if (k + 2 >= argc) { std::fprintf(stderr, "--cdb needs project dat\n"); return 1; }
                cdbproj = argv[k + 1]; cdbdat = argv[k + 2]; k += 2;
            } else if (!std::strcmp(argv[k], "--from")) {
                if (k + 1 >= argc) { std::fprintf(stderr, "--from needs a number\n"); return 1; }
                from = (int32_t)std::atoi(argv[k + 1]); ++k;
            } else if (!std::strcmp(argv[k], "--to")) {
                if (k + 1 >= argc) { std::fprintf(stderr, "--to needs a number\n"); return 1; }
                to = (int32_t)std::atoi(argv[k + 1]); ++k;
            } else if (!std::strcmp(argv[k], "--cap")) {
                if (k + 1 >= argc) { std::fprintf(stderr, "--cap needs a number\n"); return 1; }
                cap = (size_t)std::strtoull(argv[k + 1], nullptr, 10); ++k;
            } else {
                std::fprintf(stderr, "unknown option %s\n", argv[k]);
                return 1;
            }
        }
        return cmd_sweep(argv[2], dbproj, dbdat, cdbproj, cdbdat, from, to, cap);
    }
    if (argc >= 3 && std::strcmp(argv[1], "mpssweep") == 0) {
        const char* cepath = nullptr;
        const char* dbproj = nullptr;
        const char* dbdat = nullptr;
        const char* cdbproj = nullptr;
        const char* cdbdat = nullptr;
        int32_t from = 0, to = -1;
        size_t cap = 0;
        for (int k = 3; k < argc; ++k) {
            if (!std::strcmp(argv[k], "--ce")) {
                if (k + 1 >= argc) { std::fprintf(stderr, "--ce needs CommonEvent.dat\n"); return 1; }
                cepath = argv[k + 1]; ++k;
            } else if (!std::strcmp(argv[k], "--db")) {
                if (k + 2 >= argc) { std::fprintf(stderr, "--db needs project dat\n"); return 1; }
                dbproj = argv[k + 1]; dbdat = argv[k + 2]; k += 2;
            } else if (!std::strcmp(argv[k], "--cdb")) {
                if (k + 2 >= argc) { std::fprintf(stderr, "--cdb needs project dat\n"); return 1; }
                cdbproj = argv[k + 1]; cdbdat = argv[k + 2]; k += 2;
            } else if (!std::strcmp(argv[k], "--from")) {
                if (k + 1 >= argc) { std::fprintf(stderr, "--from needs a number\n"); return 1; }
                from = (int32_t)std::atoi(argv[k + 1]); ++k;
            } else if (!std::strcmp(argv[k], "--to")) {
                if (k + 1 >= argc) { std::fprintf(stderr, "--to needs a number\n"); return 1; }
                to = (int32_t)std::atoi(argv[k + 1]); ++k;
            } else if (!std::strcmp(argv[k], "--cap")) {
                if (k + 1 >= argc) { std::fprintf(stderr, "--cap needs a number\n"); return 1; }
                cap = (size_t)std::strtoull(argv[k + 1], nullptr, 10); ++k;
            } else {
                std::fprintf(stderr, "unknown option %s\n", argv[k]);
                return 1;
            }
        }
        return cmd_mpssweep(argv[2], cepath, dbproj, dbdat, cdbproj, cdbdat, from, to, cap);
    }
    if (argc >= 3 && std::strcmp(argv[1], "mpspagewalk") == 0) {
        return cmd_mpspagewalk(argv[2]);
    }
    if (argc >= 3 && std::strcmp(argv[1], "mapobj") == 0) {
        uint64_t ticks = 1;
        std::vector<std::pair<long long, long long>> sets;
        for (int k = 3; k < argc; ++k) {
            if (!std::strcmp(argv[k], "--ticks")) {
                if (k + 1 >= argc) { std::fprintf(stderr, "--ticks needs a number\n"); return 1; }
                ticks = std::strtoull(argv[k + 1], nullptr, 10); ++k;
            } else if (!std::strcmp(argv[k], "--set")) {
                if (k + 1 >= argc || !std::strchr(argv[k + 1], '=')) {
                    std::fprintf(stderr, "--set needs KEY=VALUE\n"); return 1; }
                sets.emplace_back(std::strtoll(argv[k + 1], nullptr, 10),
                                  std::strtoll(std::strchr(argv[k + 1], '=') + 1, nullptr, 10));
                ++k;
            } else {
                std::fprintf(stderr, "unknown option %s\n", argv[k]);
                return 1;
            }
        }
        return cmd_mapobj(argv[2], ticks, sets);
    }
    std::fprintf(stderr, "usage: wolfvm datinfo <file.dat> | gameinfo <Game.dat> | "
                         "commoninfo <CommonEvent.dat> [-n N] | commondump <CommonEvent.dat> "
                         "[-h | <eventId>] | commonstat <CommonEvent.dat> | "
                         "commonrun <CommonEvent.dat> <id> [-t] [--db <X.project> <X.dat>] [--cdb <P.project> <P.dat>] | "
                         "selftest | "
                         "sweep <CommonEvent.dat> [--db <X.project> <X.dat>] [--cdb <P.project> <P.dat>] [--from A] [--to B] [--cap N] | "
                         "mpssweep <X.mps> [--ce CommonEvent.dat] [--db P D] [--cdb P D] [--from A] [--to B] [--cap N] | "
                         "maptree <MapTree.dat> [-t] | openstatus <MapTreeOpenStatus.dat> | "
                         "dbinfo <X.project> <X.dat> [type [row]] | sysbasic <SysDataBaseBasic.dat> | "
                         "tileset <TileSetData.dat> [-v] | mpsinfo <Data/MapData/X.mps> [-v] [-c] | "
                         "mpspagewalk <Data/MapData-dir> | "
                         "mapobj <X.mps> [--ticks N] [--set KEY=VALUE ...]\n");
    return 1;
}

#ifndef WOLFVM_AS_LIBRARY
int main(int argc, char** argv) {
    return run_cli(argc, argv);
}
#endif

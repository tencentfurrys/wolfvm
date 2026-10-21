// wolfvm v0.3 — Stage 1: dat primitives Game.dat + CommonEvent.dat decoders verified on real data.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#include <algorithm>

#include "common_event.h"
#include "database.h"
#include "filecoder.h"
#include "game_dat.h"
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
                         const char* cdbProjPath, const char* cdbDatPath, bool verbose) {
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

static int cmd_dbinfo(const char* project_path, const char* dat_path) {
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

int main(int argc, char** argv) {
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
        for (int k = 4; k < argc; ++k) {
            if (!std::strcmp(argv[k], "-v")) verbose = true;
            else if (!std::strcmp(argv[k], "-t")) verbose_trace = true;
            else if (!std::strcmp(argv[k], "--db")) {
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
        return cmd_commonrun(argv[2], id, dbproj, dbdat, cdbproj, cdbdat, verbose || verbose_trace);
    }
    if (argc >= 3 && std::strcmp(argv[1], "maptree") == 0) {
        const bool tree = (argc >= 4 && !std::strcmp(argv[3], "-t"));
        return cmd_maptree(argv[2], tree);
    }
    if (argc >= 3 && std::strcmp(argv[1], "openstatus") == 0)
        return cmd_openstatus(argv[2]);
    if (argc >= 4 && std::strcmp(argv[1], "dbinfo") == 0)
        return cmd_dbinfo(argv[2], argv[3]);
    if (argc >= 3 && std::strcmp(argv[1], "sysbasic") == 0)
        return cmd_sysbasic(argv[2]);
    if (argc >= 3 && std::strcmp(argv[1], "tileset") == 0) {
        const bool verbose = (argc >= 4 && !std::strcmp(argv[3], "-v"));
        return cmd_tileset(argv[2], verbose);
    }
    std::fprintf(stderr, "usage: wolfvm datinfo <file.dat> | gameinfo <Game.dat> | "
                         "commoninfo <CommonEvent.dat> [-n N] | commondump <CommonEvent.dat> "
                         "[-h | <eventId>] | commonstat <CommonEvent.dat> | "
                         "commonrun <CommonEvent.dat> <id> [-t] [--db <X.project> <X.dat>] [--cdb <P.project> <P.dat>] | "
                         "maptree <MapTree.dat> [-t] | openstatus <MapTreeOpenStatus.dat> | "
                         "dbinfo <X.project> <X.dat> | sysbasic <SysDataBaseBasic.dat> | "
                         "tileset <TileSetData.dat> [-v]\n");
    return 1;
}

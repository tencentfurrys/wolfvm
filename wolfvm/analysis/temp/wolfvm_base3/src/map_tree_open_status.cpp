#include "map_tree_open_status.h"
#include <cstdio>
#include <stdexcept>

namespace wolf {

void MapTreeOpenStatusDat::parse(Reader& r, MapTreeOpenStatusDat& out, std::string& err) {
    try {
        out.head = r.raw(20);
        for (size_t i = 0; i < out.head.size(); ++i)
            if (out.head[i] != 0x00) {
                char m[5];
                std::snprintf(m, 5, "%02X", out.head[i]);
                throw std::runtime_error(std::string("head padding byte ") +
                                         std::to_string(i) + " = 0x" + m + ", expected 0x00");
            }
        out.count = r.i32();
        if (out.count < 0) throw std::runtime_error("negative flag count");
        if (out.count > 100000) throw std::runtime_error("absurd flag count");
        out.flags.resize(static_cast<size_t>(out.count));
        for (size_t i = 0; i < out.flags.size(); ++i) {
            out.flags[i] = r.u8();
            if (out.flags[i] > 1)
                throw std::runtime_error("flag[" + std::to_string(i) + "] = 0x" +
                                         std::to_string(out.flags[i]) + ", expected 0 or 1");
        }
        out.terminator = r.u8();
        if (!r.eof())
            throw std::runtime_error("unconsumed trailing bytes: " + std::to_string(r.remaining()));
    } catch (const std::exception& e) {
        err = std::string("MapTreeOpenStatus.parse: ") + e.what();
    }
}

} // namespace wolf

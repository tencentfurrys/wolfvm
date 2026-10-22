#include "map_tree.h"
#include <cstdio>
#include <stdexcept>

namespace wolf {

void MapTreeDat::parse(Reader& r, MapTreeDat& out, std::string& err) {
    try {
        out.head = r.raw(10);
        for (size_t i = 0; i < out.head.size(); ++i)
            if (out.head[i] != 0x00) {
                char m[5];
                std::snprintf(m, 5, "%02X", out.head[i]);
                throw std::runtime_error(std::string("head padding byte ") +
                                         std::to_string(i) + " = 0x" + m + ", expected 0x00");
            }
        out.marker = r.u8();
        if (out.marker != 0x90) {
            char m[5];
            std::snprintf(m, 5, "%02X", out.marker);
            throw std::runtime_error(std::string("marker byte 0x") + m +
                                     " at offset " + std::to_string(r.pos() - 1) +
                                     ", expected 0x90");
        }
        out.count = r.i32();
        if (out.count < 0) throw std::runtime_error("negative node count");
        if (out.count > 100000) throw std::runtime_error("absurd node count");
        out.nodes.resize(static_cast<size_t>(out.count));
        for (auto& n : out.nodes) {
            n.parent = r.i32();
            n.id = r.i32();
        }
        out.terminator = r.u8();
        if (!r.eof())
            throw std::runtime_error("unconsumed trailing bytes: " + std::to_string(r.remaining()));
    } catch (const std::exception& e) {
        err = std::string("MapTree.parse: ") + e.what();
    }
}

} // namespace wolf

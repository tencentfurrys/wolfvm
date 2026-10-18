#include "sys_db_basic.h"
#include <cstdio>
#include <stdexcept>

namespace wolf {

void SysDbBasicDat::parse(Reader& r, SysDbBasicDat& out, std::string& err) {
    try {
        const size_t begin = r.pos();
        (void)begin;
        out.head = r.raw(10);
        for (size_t i = 0; i < out.head.size(); ++i)
            if (out.head[i] != 0x00) {
                char m[5];
                std::snprintf(m, 5, "%02X", out.head[i]);
                throw std::runtime_error(std::string("head byte ") + std::to_string(i) +
                                         " = 0x" + m + ", expected 0x00");
            }
        out.head_marker = r.u8();
        if (out.head_marker != 0xC0) {
            char m[5];
            std::snprintf(m, 5, "%02X", out.head_marker);
            throw std::runtime_error(std::string("head marker 0x") + m + ", expected 0xC0");
        }
        out.type_count = r.i32();
        if (out.type_count < 0 || out.type_count > 1000)
            throw std::runtime_error("absurd type count " + std::to_string(out.type_count));
        out.types.resize(static_cast<size_t>(out.type_count));
        for (size_t t = 0; t < out.types.size(); ++t) {
            SysDbBasicType& ty = out.types[t];
            const int32_t f = r.i32();
            if (f < 0 || f > 10000)
                throw std::runtime_error("type " + std::to_string(t) + ": absurd field count " +
                                         std::to_string(f));
            ty.indexinfos.resize(static_cast<size_t>(f));
            for (int32_t i = 0; i < f; ++i) {
                const int32_t ix = r.i32();
                if (ix < 0 || ix > 100000)
                    throw std::runtime_error("type " + std::to_string(t) + " field " +
                                             std::to_string(i) + ": absurd indexinfo " +
                                             std::to_string(ix));
                ty.indexinfos[static_cast<size_t>(i)] = ix;
            }
            ty.row_capacity = r.i32();
            if (ty.row_capacity < 0 || ty.row_capacity > 1000000)
                throw std::runtime_error("type " + std::to_string(t) + ": absurd row capacity " +
                                         std::to_string(ty.row_capacity));
            size_t n_int = 0, n_str = 0;
            for (const int32_t ix : ty.indexinfos) (ix >= 0x07D0 ? ++n_str : ++n_int);
            ty.int_rows.resize(static_cast<size_t>(ty.row_capacity));
            ty.str_rows.resize(static_cast<size_t>(ty.row_capacity));
            for (int32_t row = 0; row < ty.row_capacity; ++row) {
                ty.int_rows[static_cast<size_t>(row)].reserve(n_int);
                for (size_t k = 0; k < n_int; ++k)
                    ty.int_rows[static_cast<size_t>(row)].push_back(r.i32());
                ty.str_rows[static_cast<size_t>(row)].reserve(n_str);
                for (size_t k = 0; k < n_str; ++k)
                    ty.str_rows[static_cast<size_t>(row)].push_back(r.str());
            }
        }
        out.terminator = r.u8();
        if (out.terminator != 0xC0) {
            char m[5];
            std::snprintf(m, 5, "%02X", out.terminator);
            throw std::runtime_error(std::string("terminator 0x") + m + ", expected 0xC0");
        }
        out.size_consumed = r.pos();
        if (!r.eof())
            throw std::runtime_error("unconsumed trailing bytes: " + std::to_string(r.remaining()));
    } catch (const std::exception& e) {
        err = std::string("SysDbBasic.parse: ") + e.what();
    }
}

} // namespace wolf

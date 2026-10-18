#include "route.h"
#include <stdexcept>

namespace wolf {

RouteCommand RouteCommand::read(Reader& r) {
    RouteCommand rc;
    rc.id = r.u8();
    const uint32_t n = r.u8();
    rc.args.resize(n);
    for (auto& a : rc.args) a = r.i32();
    if (r.u8() != 0x01) throw std::runtime_error("route terminator-1 mismatch");
    if (r.u8() != 0x00) throw std::runtime_error("route terminator-2 mismatch");
    return rc;
}

} // namespace wolf

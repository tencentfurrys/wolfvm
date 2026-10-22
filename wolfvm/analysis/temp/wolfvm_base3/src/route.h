// Wolf movement-route sub-records — factual layout, clean-room implementation.
#pragma once
#include <cstdint>
#include <vector>
#include "filecoder.h"

namespace wolf {

struct RouteCommand {
    uint8_t id = 0;
    std::vector<int32_t> args;
    static RouteCommand read(Reader& r);   // consumes the trailing 01 00 marker
};

} // namespace wolf

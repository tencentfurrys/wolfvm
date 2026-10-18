// Wolf command stream — factual 2.2x layout, clean-room implementation.
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "filecoder.h"
#include "route.h"

namespace wolf {

struct Command {
    uint32_t cid = 0;
    std::vector<int32_t> args;
    uint8_t indent = 0;
    std::vector<std::string> string_args;
    bool has_move_tail = false;

    // Move-command tail (cid 201) when has_move_tail:
    std::vector<uint8_t> move_unknown;   // 5 bytes
    uint8_t move_flags = 0;
    std::vector<RouteCommand> route;

    static Command read(Reader& r, std::string& err);

    const char* name() const;            // descriptive name for known cids, nullptr otherwise
};

} // namespace wolf

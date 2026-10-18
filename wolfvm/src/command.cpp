#include "command.h"
#include <cstring>
#include <stdexcept>

namespace wolf {

// Factual command-ID table for 2.2x-era events (diagnostic display only).
// Returns nullptr for unknown/extension cids — Stage 2 dispatch adds these.
const char* Command::name() const {
    switch (cid) {
        case 0:    return "Blank";
        case 99:   return "Checkpoint";
        case 101:  return "Message";
        case 102:  return "Choices";
        case 103:  return "Comment";
        case 105:  return "ForceStopMessage";
        case 106:  return "DebugMessage";
        case 107:  return "ClearDebugText";
        case 111:  return "VariableCondition";
        case 112:  return "StringCondition";
        case 121:  return "SetVariable";
        case 122:  return "SetString";
        case 123:  return "InputKey";
        case 124:  return "SetVariableEx";
        case 125:  return "AutoInput";
        case 126:  return "BanInput";
        case 130:  return "Teleport";
        case 140:  return "Sound";
        case 150:  return "Picture";
        case 151:  return "ChangeColor";
        case 160:  return "SetTransition";
        case 161:  return "PrepareTransition";
        case 162:  return "ExecuteTransition";
        case 170:  return "StartLoop";
        case 171:  return "BreakLoop";
        case 172:  return "BreakEvent";
        case 173:  return "EraseEvent";
        case 174:  return "ReturnToTitle";
        case 175:  return "EndGame";
        case 176:  return "StartLoop";
        case 177:  return "StopNonPic";
        case 178:  return "ResumeNonPic";
        case 179:  return "LoopTimes";
        case 180:  return "Wait";
        case 201:  return "Move";
        case 202:  return "WaitForMove";
        case 210:  return "CommonEvent";
        case 211:  return "CommonEventReserve";
        case 212:  return "SetLabel";
        case 213:  return "JumpLabel";
        case 220:  return "SaveLoad";
        case 221:  return "LoadGame";
        case 222:  return "SaveGame";
        case 230:  return "MoveDuringEventOn";
        case 231:  return "MoveDuringEventOff";
        case 240:  return "Chip";
        case 241:  return "ChipSet";
        case 242:  return "ChipOverwrite";
        case 250:  return "Database";
        case 251:  return "ImportDatabase";
        case 270:  return "Party";
        case 280:  return "MapEffect";
        case 281:  return "ScrollScreen";
        case 290:  return "Effect";
        case 300:  return "CommonEventByName";
        case 401:  return "ChoiceCase";
        case 402:  return "SpecialChoiceCase";
        case 420:  return "ElseCase";
        case 421:  return "CancelCase";
        case 498:  return "LoopEnd";
        case 499:  return "BranchEnd";
        default:   return nullptr;
    }
}

Command Command::read(Reader& r, std::string& err) {
    Command c;
    try {
        const uint32_t n_args = static_cast<uint32_t>(r.u8()) - 1;
        if (n_args > 0xFF) throw std::logic_error("argument count underflow");
        c.cid = static_cast<uint32_t>(r.i32());
        c.args.resize(n_args);
        for (auto& a : c.args) a = r.i32();
        c.indent = r.u8();
        const uint32_t n_str = r.u8();
        c.string_args.resize(n_str);
        for (auto& s : c.string_args) s = r.str();
        const uint8_t terminator = r.u8();
        if (terminator == 0x01) {
            c.has_move_tail = true;
            c.move_unknown = r.raw(5);
            c.move_flags = r.u8();
            const int32_t route_n = r.i32();
            if (route_n < 0) throw std::logic_error("negative route count");
            c.route.resize(static_cast<size_t>(route_n));
            for (auto& rc : c.route) rc = RouteCommand::read(r);
        } else if (terminator != 0x00) {
            err = "unexpected command terminator 0x" + ([](uint8_t v){ static char b[3]; std::snprintf(b, 3, "%02X", v); return std::string(b); })(terminator);
            return c;
        }
    } catch (const std::exception& e) {
        err = std::string("Command.read: ") + e.what();
    }
    return c;
}

} // namespace wolf

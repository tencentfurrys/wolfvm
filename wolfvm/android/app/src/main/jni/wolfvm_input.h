// wolfvm_input.h — on-device gesture floor (Stage 4 round 2, approx/MODEL).
// Virtual controls for the map session until the engine's real input
// namespace is decoded from Game.exe (then this maps onto it — TODO verify).
#pragma once
#include <cstdint>
#include <mutex>

namespace wolf {

enum GestureKind {
    KIND_TAP = 0,     // confirm / advance (engine A-key slot)
    KIND_DRAG = 1,    // camera pan (dx,dy accumulated since last consume)
    KIND_LONG = 2,    // cancel / open (engine B-key slot)
    KIND_DOUBLE = 3   // UI: toggle console overlay (shell-owned, not game input)
};

struct InputState {
    std::mutex m;
    float cam_dx = 0, cam_dy = 0;
    uint32_t tap_count = 0, long_count = 0, double_count = 0;

    void gesture(int kind, float /*x*/, float /*y*/, float dx, float dy) {
        std::lock_guard<std::mutex> g(m);
        switch (kind) {
            case KIND_TAP: ++tap_count; break;
            case KIND_LONG: ++long_count; break;
            case KIND_DOUBLE: ++double_count; break;
            case KIND_DRAG: cam_dx += dx; cam_dy += dy; break;
            default: break;
        }
    }
    void consume_frame(float& dx, float& dy, uint32_t& taps, uint32_t& longs, uint32_t& dbls) {
        std::lock_guard<std::mutex> g(m);
        dx = cam_dx; dy = cam_dy;
        taps = tap_count; longs = long_count; dbls = double_count;
        cam_dx = cam_dy = 0;
        tap_count = long_count = double_count = 0;
    }
};

} // namespace wolf

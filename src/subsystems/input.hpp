#pragma once

#include <math/sr_math.hpp>

// =============================================================================
// Input as intent, not as devices.
//
// The host maps whatever it has (keyboard, mouse, later a gamepad) into this
// once per frame; game logic reads it and never sees SDL. Two players means two
// of these, not two sets of scancode checks.
// =============================================================================

// UI-oriented intent (menus, buttons), kept separate from gameplay intent.
// `pointer_x/y` are in window pixels, or negative when there is no pointer
// (relative mouse mode). `*_pressed` is true only on the frame of the press.
// The navigation fields let a game UI work without a mouse.
struct UiInput {
    float pointer_x = -1.0f, pointer_y = -1.0f;
    bool  pointer_down    = false;
    bool  pointer_pressed = false;

    bool  nav_up = false, nav_down = false;
    bool  accept = false, back = false;
};

struct InputState {
    // Movement intent in camera-local axes, each component in [-1, 1]:
    // +x right, +y up, -z forward.
    vec3 move = vec3(0.0f, 0.0f, 0.0f);

    // Look delta accumulated this frame, in raw device units (mouse pixels).
    // Sensitivity is the consumer's business, not the host's.
    float look_x = 0.0f;
    float look_y = 0.0f;

    // Wheel notches accumulated this frame (+ up / - down).
    float wheel = 0.0f;

    // UI intent for the game's own menus. Filled by app/, consumed by game_ui.
    UiInput ui;
};

#pragma once

#include <math/sr_math.hpp>

// =============================================================================
// Input as intent, not as devices.
//
// The host maps whatever it has (keyboard, mouse, later a gamepad) into this
// once per frame; game logic reads it and never sees SDL. Two players means two
// of these, not two sets of scancode checks.
// =============================================================================
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
};

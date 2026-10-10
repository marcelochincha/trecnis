#pragma once

#include <cstddef>
#include <vector>

// =============================================================================
// DebugVars — a flat registry of tunable variables game/ exposes to the
// debugger, without game/ knowing ImGui exists.
//
// A DebugVar is just a name, a group, a kind and a raw pointer to the live
// value. The debugger walks the list and renders a widget per entry. The
// pointers must stay valid for as long as the registry is queried: game/
// registers members of its own Game, so the registry is emptied in app_shutdown
// right before the Game is destroyed.
//
// C++ only: no SDL, no ImGui, no render/. It is a subsystem so app/ and game/
// can share it.
// =============================================================================

struct DebugVar {
    const char* group;
    const char* name;
    enum Kind { Float, Int, Bool } kind;
    void*  ptr;
    float  min;
    float  max;
};

struct DebugVars {
    std::vector<DebugVar> vars;

    void add(const char* group, const char* name, float* p, float lo, float hi) {
        vars.push_back({ group, name, DebugVar::Float, p, lo, hi });
    }
    void add(const char* group, const char* name, int* p, int lo, int hi) {
        vars.push_back({ group, name, DebugVar::Int, p, (float)lo, (float)hi });
    }
    void add(const char* group, const char* name, bool* p) {
        vars.push_back({ group, name, DebugVar::Bool, p, 0.0f, 0.0f });
    }
};

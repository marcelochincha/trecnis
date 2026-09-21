#include <core/sr_profiler.hpp>

Profiler g_prof;

void Profiler::begin_frame() {
    for (int i = 0; i < PROF_COUNT; ++i) cur_[i] = 0.0;
}

void Profiler::add(ProfSection s, double ms) {
    cur_[s] += ms;
}

void Profiler::end_frame() {
    // The first frame seeds the average directly; smoothing from zero would
    // otherwise take dozens of frames to climb to the real value.
    if (!primed_) {
        for (int i = 0; i < PROF_COUNT; ++i) avg_[i] = cur_[i];
        primed_ = true;
        return;
    }
    for (int i = 0; i < PROF_COUNT; ++i)
        avg_[i] += (cur_[i] - avg_[i]) * kSmoothing;
}

double Profiler::total_ms() const {
    double t = 0.0;
    for (int i = 0; i < PROF_COUNT; ++i) t += avg_[i];
    return t;
}

const char* Profiler::name(ProfSection s) {
    switch (s) {
        // Kept to 6 characters so the HUD column stays narrow at 320px wide.
        case PROF_SKIN:    return "skin";
        case PROF_BVH_DYN: return "bvhdyn";
        case PROF_CLEAR:   return "clear";
        case PROF_TRACE:   return "trace";
        case PROF_GIZMO:   return "gizmo";
        case PROF_DEBUG:   return "dbgovl";
        case PROF_HUD:     return "hud";
        case PROF_UPLOAD:  return "upload";
        case PROF_PRESENT: return "presnt";
        default:           return "?";
    }
}

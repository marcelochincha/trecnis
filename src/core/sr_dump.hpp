#pragma once

#include <cstdint>
#include <vector>

struct framebuffer;

// =============================================================================
// Frame dumping — the renderer's only way to be checked without a human looking
// at the window.
//
// Two uses, one implementation:
//   * [F12] in the running app writes the frame you are looking at to disk.
//   * `--dump <dir>` renders the SAME frame with every available backend and
//     writes them side by side, so CPU / Embree / OpenCL can be compared as
//     images and as numbers instead of by eye.
//
// The capture point is deliberately BEFORE the gizmo, the debug overlays and
// the HUD: those draw frame counters and timings that change every frame, so
// including them would make every hash unique and every comparison useless.
// =============================================================================
namespace dump {

// Copy the colour buffer out, for comparing two frames later.
std::vector<uint32_t> capture(const framebuffer& fb);

// FNV-1a over the RGB channels. The identity of an image in one number: equal
// hashes mean pixel-identical frames. Alpha is masked out because backends do
// not agree on what they leave there and it carries no image information.
uint64_t frame_hash(const framebuffer& fb);

// Write a 32-bit BMP (no dependencies, opens natively on Windows).
bool save_bmp(const framebuffer& fb, const char* path);

// Write `dir`/`tag`.bmp, creating `dir` if needed, and log the path + hash.
// `tag` is sanitised, so a backend name with spaces in it is a valid argument.
// Returns the frame hash.
uint64_t write(const framebuffer& fb, const char* dir, const char* tag);

// How far apart two frames are. `pixels` counts pixels differing by more than
// `tol` on any channel; `max_delta` is the largest single-channel difference.
//
// A hash comparison alone is too strict to be useful here: two tracers can
// disagree on a handful of edge pixels from float tie-breaking and still be the
// same shading model, which a boolean cannot express but these two numbers can.
struct DiffStats {
    int pixels    = 0;
    int max_delta = 0;
};
DiffStats diff(const std::vector<uint32_t>& a, const std::vector<uint32_t>& b, int tol = 0);

} // namespace dump

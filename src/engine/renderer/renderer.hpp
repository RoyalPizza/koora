#pragma once

#include "engine/core/arena.hpp"
#include "engine/platform/platform.hpp"

// 2D renderer. One backend per OS (render_d3d11.cpp); the build picks which file is compiled.

// Creates the device and swap chain for the window. Shaders are read from shader_folder
// (with a trailing separator). Returns false on failure.
bool renderer_init(Window* window, const char* shader_folder);

// Releases every GPU object, in reverse order of creation.
void renderer_shutdown();

// Starts a frame: handles window resizes and clears to the color (0..1 per channel).
void renderer_begin_frame(float r, float g, float b);

// Saves what has been drawn so far this frame as a PNG. Call before renderer_end_frame.
// Pixel data is staged in the frame arena. Returns false on failure.
bool renderer_save_screenshot(const char* path, Arena& frame);

// Presents the frame with vsync.
void renderer_end_frame();

// Loads and compiles <shader_folder><name>.hlsl (entry points vs_main and ps_main).
// Returns a shader id, or -1 after logging the compiler's message.
int shader_load(const char* name);

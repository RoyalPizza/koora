#pragma once

#include "engine/core/arena.hpp"
#include "engine/platform/platform.hpp"

#include <cstdint>

// 2D renderer. One backend per OS (render_d3d11.cpp); the build picks which file is compiled.
// The game draws into a fixed low-res screen (NES/SNES-style, default 320x180) that is scaled
// up to the window by a whole number, centered with black bars. Everything is drawn as quads
// through one sprite batcher, in game pixels with the origin at the top-left of the game
// screen, offset by the camera position.

// Colors are 0xRRGGBBAA.
constexpr uint32_t COLOR_WHITE = 0xFFFFFFFF;

struct Rect
{
    float x;
    float y;
    float w;
    float h;
};

// Creates the device, swap chain and batcher for the window. Batch memory comes from the
// permanent arena. Loads the default "sprite" shader. Returns false on failure.
bool renderer_init(Window* window, Arena& permanent);

// Releases every GPU object, in reverse order of creation.
void renderer_shutdown();

// Starts a frame: handles window resizes and clears to the color (0..1 per channel).
void renderer_begin_frame(float r, float g, float b);

// Draws everything batched so far. Called by end_frame and before screenshots; only needed
// directly when mixing in other GPU work.
void renderer_flush();

// Saves the window as shown (game screen scaled, with bars) as a PNG. Call before
// renderer_end_frame. Pixel data is staged in the frame arena. Returns false on failure.
bool renderer_save_screenshot(const char* path, Arena& frame);

// Presents the frame with vsync.
void renderer_end_frame();

// Loads and compiles shaders/<name>.hlsl (entry points vs_main and ps_main). Loading the same
// name again returns the same id. Returns -1 after logging the compiler's message.
int shader_load(const char* name);

// Shader used for the following draws, until changed. -1 selects the default sprite shader.
void renderer_set_shader(int shader);

// Uploads RGBA8 pixels as a texture with point filtering. Returns a texture id, or -1.
int renderer_create_texture(int width, int height, const uint8_t* rgba);

// Size of a texture in pixels; 0x0 for an invalid id.
void renderer_get_texture_size(int texture, int* width, int* height);

// World position shown at the top-left corner of the game screen.
void renderer_set_camera(float x, float y);

// Size of the game screen in game pixels. Returns false if the target can't be created.
bool renderer_set_game_resolution(int width, int height);

// Current game screen size in game pixels.
void renderer_get_size(int* width, int* height);

// Solid rectangle.
void renderer_draw_rect(Rect dest, uint32_t color);

// Part of a texture (source, in texture pixels) stretched over dest, multiplied by tint.
void renderer_draw_sprite(int texture, Rect source, Rect dest, uint32_t tint);

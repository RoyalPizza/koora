#pragma once

#include "engine/core/arena.hpp"

#include <cstdint>
#include <vector>

// Loading and saving game content.

// Carves the scratch arena that image libraries decode into. Call once at startup.
// Returns false if the permanent arena is too small.
bool assets_init(Arena& permanent);

// Reads a whole file (UTF-8 path) into out. Load-time only. Logs and returns false on failure.
bool assets_read_file(const char* path, std::vector<uint8_t>& out);

// Writes tightly packed RGBA8 pixels as a PNG. Logs and returns false on failure.
bool assets_write_png(const char* path, int width, int height, const uint8_t* rgba);

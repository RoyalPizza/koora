#pragma once

#include "engine/core/arena.hpp"

#include <cstdint>
#include <vector>

// Loading and saving game content. Content is named by a path relative to the game folder,
// with forward slashes ("art/player.png", "data/player.toml"). Names starting with "shaders/"
// are engine shaders. Dev builds read loose files from the folders given to assets_init.

// Sets where content comes from, carves the decode scratch arena and the asset table.
// Folders end with a separator. Returns false if the permanent arena is too small.
bool assets_init(Arena& permanent, const char* game_folder, const char* shader_folder);

// Reads a whole content file into out. Load-time only. Logs and returns false on failure.
bool assets_read_file(const char* name, std::vector<uint8_t>& out);

// Loads a PNG as a renderer texture. Loading the same name again returns the same id.
// Logs and returns -1 on failure.
int assets_load_texture(const char* name);

// Writes tightly packed RGBA8 pixels as a PNG at a real file path (not a content name).
// Logs and returns false on failure.
bool assets_write_png(const char* path, int width, int height, const uint8_t* rgba);

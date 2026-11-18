#pragma once

#include <cstdint>
#include <vector>

// Loading game content from disk.

// Reads a whole file (UTF-8 path) into out. Load-time only. Logs and returns false on failure.
bool assets_read_file(const char* path, std::vector<uint8_t>& out);

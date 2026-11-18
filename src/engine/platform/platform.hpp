#pragma once

#include <cstddef>
#include <cstdio>
#include <string>

// OS layer. One implementation file per OS (platform_win32.cpp); main.cpp and game code never
// call the OS directly. Paths are UTF-8.

// Reserves and commits zeroed memory. Returns nullptr on failure.
void* platform_alloc(size_t bytes);

// Releases memory from platform_alloc.
void platform_free(void* memory);

// Seconds since an arbitrary fixed point, high resolution.
double platform_get_time();

// Folder holding the running exe, with a trailing separator. Empty on failure.
std::string platform_get_exe_folder();

// fopen with a UTF-8 path. Returns nullptr on failure.
FILE* platform_file_open(const char* path, const char* mode);

// Renames a file, replacing the destination. Returns false on failure.
bool platform_file_rename(const char* from, const char* to);

// Deletes a file. Returns false on failure, including when it doesn't exist.
bool platform_file_delete(const char* path);

// Returns true if a file or folder exists at path.
bool platform_file_exists(const char* path);

// Creates a folder and any missing parents. Returns true if it exists afterwards.
bool platform_create_folder(const char* path);

// Sends text to the attached debugger, if any.
void platform_debug_output(const char* text);

#pragma once

#include <cstddef>
#include <cstdint>

// Linear allocator over a fixed block. Memory is freed only by resetting the whole arena.
struct Arena
{
    const char* name = "";
    uint8_t* base = nullptr;
    size_t size = 0;
    size_t used = 0;
};

// Points the arena at memory it does not own. The name appears in out-of-memory errors.
void arena_init(Arena& arena, const char* name, void* memory, size_t size);

// Returns zeroed memory aligned to align (a power of two). Logs an error and returns nullptr
// when the arena is full.
void* arena_push(Arena& arena, size_t bytes, size_t align);

// Frees everything in the arena at once.
void arena_reset(Arena& arena);

#define ARENA_PUSH_STRUCT(arena, T) (T*)arena_push(arena, sizeof(T), alignof(T))
#define ARENA_PUSH_ARRAY(arena, T, n) (T*)arena_push(arena, sizeof(T) * (n), alignof(T))

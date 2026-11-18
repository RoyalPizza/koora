#include "engine/core/arena.hpp"

#include "engine/core/log.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>

void arena_init(Arena& arena, const char* name, void* memory, size_t size)
{
    arena.name = name;
    arena.base = (uint8_t*)memory;
    arena.size = size;
    arena.used = 0;
}

void* arena_push(Arena& arena, size_t bytes, size_t align)
{
    assert(align != 0 && (align & (align - 1)) == 0);

    uintptr_t current = (uintptr_t)arena.base + arena.used;
    uintptr_t aligned = (current + (align - 1)) & ~(uintptr_t)(align - 1);
    size_t padding = (size_t)(aligned - current);

    if (padding > arena.size - arena.used || bytes > arena.size - arena.used - padding)
    {
        LOG_ERROR("arena '%s' out of memory: wanted %zu bytes, %zu of %zu used", arena.name,
                  bytes, arena.used, arena.size);
        return nullptr;
    }

    arena.used += padding + bytes;
    memset((void*)aligned, 0, bytes);
    return (void*)aligned;
}

void arena_reset(Arena& arena)
{
    arena.used = 0;
}

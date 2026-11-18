#include "engine/assets/assets.hpp"

#include "engine/core/arena.hpp"
#include "engine/core/log.hpp"
#include "engine/platform/platform.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

// stb allocates through these, into the scratch arena. Declared before the stb include.
static void* scratch_alloc(size_t bytes);
static void* scratch_realloc(void* old, size_t bytes);
static void scratch_free(void* memory);

#define STBIW_MALLOC(bytes) scratch_alloc(bytes)
#define STBIW_REALLOC(old, bytes) scratch_realloc(old, bytes)
#define STBIW_FREE(memory) scratch_free(memory)
#define STBI_WRITE_NO_STDIO
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb/stb_image_write.h"

constexpr size_t SCRATCH_ARENA_SIZE = 64 * 1024 * 1024;
constexpr size_t SCRATCH_ALIGN = 16;

// Each scratch block starts with a header so realloc knows how much to copy.
struct ScratchHeader
{
    size_t size;
    size_t padding;
};
static_assert(sizeof(ScratchHeader) == SCRATCH_ALIGN);

// Scratch memory for third-party decoders. Reset after each load or save, never kept.
static Arena scratch_arena;

bool assets_init(Arena& permanent)
{
    void* memory = arena_push(permanent, SCRATCH_ARENA_SIZE, SCRATCH_ALIGN);
    if (!memory)
    {
        return false;
    }
    arena_init(scratch_arena, "asset scratch", memory, SCRATCH_ARENA_SIZE);
    return true;
}

bool assets_read_file(const char* path, std::vector<uint8_t>& out)
{
    out.clear();
    FILE* file = platform_file_open(path, "rb");
    if (!file)
    {
        LOG_ERROR("could not open '%s'", path);
        return false;
    }

    bool ok = fseek(file, 0, SEEK_END) == 0;
    long size = ok ? ftell(file) : -1;
    ok = ok && size >= 0 && fseek(file, 0, SEEK_SET) == 0;
    if (ok)
    {
        out.resize((size_t)size);
        ok = size == 0 || fread(out.data(), 1, (size_t)size, file) == (size_t)size;
    }
    fclose(file);

    if (!ok)
    {
        LOG_ERROR("could not read '%s'", path);
        out.clear();
        return false;
    }
    return true;
}

bool assets_write_png(const char* path, int width, int height, const uint8_t* rgba)
{
    int png_len = 0;
    uint8_t* png = stbi_write_png_to_mem(rgba, width * 4, width, height, 4, &png_len);
    if (!png)
    {
        LOG_ERROR("could not encode PNG '%s'", path);
        arena_reset(scratch_arena);
        return false;
    }

    FILE* file = platform_file_open(path, "wb");
    bool ok = file && fwrite(png, 1, (size_t)png_len, file) == (size_t)png_len;
    if (file)
    {
        fclose(file);
    }
    arena_reset(scratch_arena);

    if (!ok)
    {
        LOG_ERROR("could not write PNG '%s'", path);
        return false;
    }
    LOG_INFO("wrote %dx%d PNG '%s'", width, height, path);
    return true;
}

static void* scratch_alloc(size_t bytes)
{
    ScratchHeader* header = (ScratchHeader*)arena_push(scratch_arena,
                                                       sizeof(ScratchHeader) + bytes,
                                                       SCRATCH_ALIGN);
    if (!header)
    {
        return nullptr;
    }
    header->size = bytes;
    return header + 1;
}

static void* scratch_realloc(void* old, size_t bytes)
{
    void* memory = scratch_alloc(bytes);
    if (memory && old)
    {
        ScratchHeader* old_header = (ScratchHeader*)old - 1;
        size_t keep = old_header->size < bytes ? old_header->size : bytes;
        memcpy(memory, old, keep);
    }
    return memory;
}

static void scratch_free(void* memory)
{
    // Freed in bulk when the scratch arena is reset.
    (void)memory;
}

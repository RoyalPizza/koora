#include "engine/assets/assets.hpp"

#include "engine/core/arena.hpp"
#include "engine/core/log.hpp"
#include "engine/platform/platform.hpp"
#include "engine/renderer/renderer.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

// stb allocates through these, into the scratch arena. Declared before the stb includes.
static void* scratch_alloc(size_t bytes);
static void* scratch_realloc(void* old, size_t bytes);
static void scratch_free(void* memory);

#define STBIW_MALLOC(bytes) scratch_alloc(bytes)
#define STBIW_REALLOC(old, bytes) scratch_realloc(old, bytes)
#define STBIW_FREE(memory) scratch_free(memory)
#define STBI_WRITE_NO_STDIO
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb/stb_image_write.h"

#define STBI_MALLOC(bytes) scratch_alloc(bytes)
#define STBI_REALLOC(old, bytes) scratch_realloc(old, bytes)
#define STBI_FREE(memory) scratch_free(memory)
#define STBI_NO_STDIO
#define STBI_ONLY_PNG
#define STB_IMAGE_IMPLEMENTATION
#include "stb/stb_image.h"

constexpr size_t SCRATCH_ARENA_SIZE = 64 * 1024 * 1024;
constexpr size_t SCRATCH_ALIGN = 16;
constexpr int MAX_TEXTURES = 256;
constexpr int MAX_ASSET_NAME = 128;
constexpr int MAX_FOLDER_LEN = 1024;
constexpr const char* SHADER_PREFIX = "shaders/";

// Each scratch block starts with a header so realloc knows how much to copy.
struct ScratchHeader
{
    size_t size;
    size_t padding;
};
static_assert(sizeof(ScratchHeader) == SCRATCH_ALIGN);

struct TextureAsset
{
    char name[MAX_ASSET_NAME];
    int texture;
};

// Lives in the permanent arena.
struct AssetTable
{
    char game_folder[MAX_FOLDER_LEN];
    char shader_folder[MAX_FOLDER_LEN];
    TextureAsset textures[MAX_TEXTURES];
    int texture_count;
};

// Scratch memory for third-party decoders. Reset after each load or save, never kept.
static Arena scratch_arena;
static AssetTable* assets = nullptr;

static std::string resolve_path(const char* name);

bool assets_init(Arena& permanent, const char* game_folder, const char* shader_folder)
{
    void* memory = arena_push(permanent, SCRATCH_ARENA_SIZE, SCRATCH_ALIGN);
    assets = ARENA_PUSH_STRUCT(permanent, AssetTable);
    if (!memory || !assets)
    {
        return false;
    }

    arena_init(scratch_arena, "asset scratch", memory, SCRATCH_ARENA_SIZE);
    snprintf(assets->game_folder, MAX_FOLDER_LEN, "%s", game_folder);
    snprintf(assets->shader_folder, MAX_FOLDER_LEN, "%s", shader_folder);
    return true;
}

bool assets_read_file(const char* name, std::vector<uint8_t>& out)
{
    out.clear();
    std::string path = resolve_path(name);
    FILE* file = platform_file_open(path.c_str(), "rb");
    if (!file)
    {
        LOG_ERROR("could not open '%s'", path.c_str());
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
        LOG_ERROR("could not read '%s'", path.c_str());
        out.clear();
        return false;
    }
    return true;
}

int assets_load_texture(const char* name)
{
    for (int i = 0; i < assets->texture_count; i += 1)
    {
        if (strcmp(assets->textures[i].name, name) == 0)
        {
            return assets->textures[i].texture;
        }
    }
    if (assets->texture_count >= MAX_TEXTURES)
    {
        LOG_ERROR("too many textures, can't load '%s'", name);
        return -1;
    }
    if (strlen(name) >= MAX_ASSET_NAME)
    {
        LOG_ERROR("texture name too long: '%s'", name);
        return -1;
    }

    std::vector<uint8_t> file;
    if (!assets_read_file(name, file))
    {
        return -1;
    }

    int width = 0;
    int height = 0;
    int channels = 0;
    uint8_t* pixels = stbi_load_from_memory(file.data(), (int)file.size(), &width, &height,
                                            &channels, 4);
    if (!pixels)
    {
        LOG_ERROR("could not decode '%s': %s", name, stbi_failure_reason());
        arena_reset(scratch_arena);
        return -1;
    }

    int texture = renderer_create_texture(width, height, pixels);
    arena_reset(scratch_arena);
    if (texture < 0)
    {
        return -1;
    }

    TextureAsset& asset = assets->textures[assets->texture_count];
    snprintf(asset.name, MAX_ASSET_NAME, "%s", name);
    asset.texture = texture;
    assets->texture_count += 1;
    LOG_INFO("texture '%s' loaded, %dx%d", name, width, height);
    return texture;
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

static std::string resolve_path(const char* name)
{
    size_t prefix_len = strlen(SHADER_PREFIX);
    if (strncmp(name, SHADER_PREFIX, prefix_len) == 0)
    {
        return std::string(assets->shader_folder) + (name + prefix_len);
    }
    return std::string(assets->game_folder) + name;
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

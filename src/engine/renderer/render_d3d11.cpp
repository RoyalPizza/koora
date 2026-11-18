#include "engine/renderer/renderer.hpp"

#include "engine/assets/assets.hpp"
#include "engine/core/arena.hpp"
#include "engine/core/log.hpp"
#include "engine/platform/platform_win32.hpp"

#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi1_2.h>
#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

// Releases a COM object and clears the pointer.
#define RELEASE_COM(object)                                                                   \
    do                                                                                        \
    {                                                                                         \
        if (object)                                                                           \
        {                                                                                     \
            (object)->Release();                                                              \
            (object) = nullptr;                                                               \
        }                                                                                     \
    } while (0)

constexpr int MAX_SHADERS = 16;
constexpr int MAX_SHADER_NAME = 64;
constexpr int MAX_TEXTURES = 256;
constexpr int MAX_QUADS = 8192;
constexpr int VERTICES_PER_QUAD = 4;
constexpr int INDICES_PER_QUAD = 6;
constexpr UINT SWAP_CHAIN_BUFFERS = 2;
constexpr DXGI_FORMAT BACK_BUFFER_FORMAT = DXGI_FORMAT_B8G8R8A8_UNORM;
constexpr UINT64 MAX_DEBUG_MESSAGES_PER_FRAME = 32;
constexpr const char* DEFAULT_SHADER_NAME = "sprite";
constexpr uint8_t WHITE_PIXEL[4] = { 255, 255, 255, 255 };

static_assert(MAX_QUADS * VERTICES_PER_QUAD <= 65536, "indices are 16-bit");

constexpr D3D11_INPUT_ELEMENT_DESC SPRITE_VERTEX_LAYOUT[] = {
    { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    { "COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0 },
};
constexpr UINT SPRITE_VERTEX_LAYOUT_COUNT =
    (UINT)(sizeof(SPRITE_VERTEX_LAYOUT) / sizeof(SPRITE_VERTEX_LAYOUT[0]));

struct SpriteVertex
{
    float x;
    float y;
    float u;
    float v;
    uint32_t color; // bytes R, G, B, A in memory
};
static_assert(sizeof(SpriteVertex) == 20);

// Matches cbuffer Frame in the shaders. Column-major, as HLSL reads it by default.
struct FrameConstants
{
    float projection[16];
};

struct Shader
{
    char name[MAX_SHADER_NAME];
    ID3D11VertexShader* vertex_shader;
    ID3D11PixelShader* pixel_shader;
    ID3D11InputLayout* input_layout;
};

struct Texture
{
    ID3D11Texture2D* texture;
    ID3D11ShaderResourceView* view;
    int width;
    int height;
};

struct Renderer
{
    Window* window;
    ID3D11Device* device;
    ID3D11DeviceContext* context;
    ID3D11InfoQueue* info_queue; // only with the debug layer
    IDXGISwapChain1* swap_chain;
    ID3D11RenderTargetView* render_target;
    ID3D11Buffer* vertex_buffer;
    ID3D11Buffer* index_buffer;
    ID3D11Buffer* constant_buffer;
    ID3D11BlendState* blend_state;
    ID3D11RasterizerState* rasterizer_state;
    ID3D11SamplerState* point_sampler;
    int width;
    int height;

    Shader shaders[MAX_SHADERS];
    int shader_count;
    Texture textures[MAX_TEXTURES];
    int texture_count;
    int default_shader;
    int white_texture;

    // Batch state. Vertices live in the permanent arena.
    SpriteVertex* vertices;
    int quad_count;
    int batch_shader;
    int batch_texture;
    float camera_x;
    float camera_y;
    bool constants_dirty;
};

// The device is genuinely single-instance, so its state is file-level.
static Renderer renderer;

static bool create_device();
static bool create_swap_chain();
static bool create_render_target();
static bool create_batch_objects(Arena& permanent);
static void resize_if_needed();
static void upload_constants();
static void push_quad(int texture, Rect dest, float u0, float v0, float u1, float v1,
                      uint32_t color);
static uint32_t color_to_vertex(uint32_t rgba);
static void log_debug_messages();
static bool compile_stage(const std::vector<uint8_t>& source, const char* path,
                          const char* entry, const char* target, ID3DBlob** out);

bool renderer_init(Window* window, Arena& permanent)
{
    renderer = {};
    renderer.window = window;
    renderer.default_shader = -1;
    renderer.white_texture = -1;
    platform_get_window_size(window, &renderer.width, &renderer.height);

    if (!create_device() || !create_swap_chain() || !create_render_target() ||
        !create_batch_objects(permanent))
    {
        renderer_shutdown();
        return false;
    }

    renderer.default_shader = shader_load(DEFAULT_SHADER_NAME);
    renderer.white_texture = renderer_create_texture(1, 1, WHITE_PIXEL);
    if (renderer.default_shader < 0 || renderer.white_texture < 0)
    {
        renderer_shutdown();
        return false;
    }

    LOG_INFO("renderer: D3D11 ready, %dx%d, debug layer %s", renderer.width, renderer.height,
             renderer.info_queue ? "on" : "off");
    return true;
}

void renderer_shutdown()
{
    if (renderer.context)
    {
        renderer.context->ClearState();
    }

    for (int i = renderer.texture_count - 1; i >= 0; i -= 1)
    {
        RELEASE_COM(renderer.textures[i].view);
        RELEASE_COM(renderer.textures[i].texture);
    }
    renderer.texture_count = 0;

    for (int i = renderer.shader_count - 1; i >= 0; i -= 1)
    {
        Shader& shader = renderer.shaders[i];
        RELEASE_COM(shader.input_layout);
        RELEASE_COM(shader.pixel_shader);
        RELEASE_COM(shader.vertex_shader);
    }
    renderer.shader_count = 0;

    RELEASE_COM(renderer.point_sampler);
    RELEASE_COM(renderer.rasterizer_state);
    RELEASE_COM(renderer.blend_state);
    RELEASE_COM(renderer.constant_buffer);
    RELEASE_COM(renderer.index_buffer);
    RELEASE_COM(renderer.vertex_buffer);
    RELEASE_COM(renderer.render_target);
    RELEASE_COM(renderer.swap_chain);
    if (renderer.info_queue)
    {
        log_debug_messages();
    }
    RELEASE_COM(renderer.info_queue);
    RELEASE_COM(renderer.context);
    RELEASE_COM(renderer.device);
}

void renderer_begin_frame(float r, float g, float b)
{
    resize_if_needed();

    D3D11_VIEWPORT viewport = {};
    viewport.Width = (float)renderer.width;
    viewport.Height = (float)renderer.height;
    viewport.MaxDepth = 1.0f;
    renderer.context->RSSetViewports(1, &viewport);
    renderer.context->OMSetRenderTargets(1, &renderer.render_target, nullptr);

    float color[4] = { r, g, b, 1.0f };
    renderer.context->ClearRenderTargetView(renderer.render_target, color);

    renderer.quad_count = 0;
    renderer.batch_shader = renderer.default_shader;
    renderer.batch_texture = renderer.white_texture;
    renderer.constants_dirty = true;
}

void renderer_flush()
{
    if (renderer.quad_count == 0)
    {
        return;
    }
    if (renderer.constants_dirty)
    {
        upload_constants();
    }

    D3D11_MAPPED_SUBRESOURCE mapped = {};
    HRESULT result = renderer.context->Map(renderer.vertex_buffer, 0, D3D11_MAP_WRITE_DISCARD, 0,
                                           &mapped);
    if (FAILED(result))
    {
        LOG_ERROR("could not map the vertex buffer: 0x%08lx", (unsigned long)result);
        renderer.quad_count = 0;
        return;
    }
    size_t bytes = sizeof(SpriteVertex) * VERTICES_PER_QUAD * (size_t)renderer.quad_count;
    memcpy(mapped.pData, renderer.vertices, bytes);
    renderer.context->Unmap(renderer.vertex_buffer, 0);

    const Shader& shader = renderer.shaders[renderer.batch_shader];
    const Texture& texture = renderer.textures[renderer.batch_texture];
    UINT stride = sizeof(SpriteVertex);
    UINT offset = 0;
    ID3D11DeviceContext* context = renderer.context;
    context->IASetInputLayout(shader.input_layout);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->IASetVertexBuffers(0, 1, &renderer.vertex_buffer, &stride, &offset);
    context->IASetIndexBuffer(renderer.index_buffer, DXGI_FORMAT_R16_UINT, 0);
    context->VSSetShader(shader.vertex_shader, nullptr, 0);
    context->VSSetConstantBuffers(0, 1, &renderer.constant_buffer);
    context->PSSetShader(shader.pixel_shader, nullptr, 0);
    context->PSSetConstantBuffers(0, 1, &renderer.constant_buffer);
    context->PSSetShaderResources(0, 1, &texture.view);
    context->PSSetSamplers(0, 1, &renderer.point_sampler);
    context->RSSetState(renderer.rasterizer_state);
    context->OMSetBlendState(renderer.blend_state, nullptr, 0xFFFFFFFF);
    context->DrawIndexed((UINT)(renderer.quad_count * INDICES_PER_QUAD), 0, 0);

    renderer.quad_count = 0;
}

bool renderer_save_screenshot(const char* path, Arena& frame)
{
    renderer_flush();

    ID3D11Texture2D* back_buffer = nullptr;
    HRESULT result = renderer.swap_chain->GetBuffer(0, __uuidof(ID3D11Texture2D),
                                                    (void**)&back_buffer);
    if (FAILED(result))
    {
        LOG_ERROR("screenshot: could not get the back buffer: 0x%08lx", (unsigned long)result);
        return false;
    }

    D3D11_TEXTURE2D_DESC desc;
    back_buffer->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.MiscFlags = 0;

    ID3D11Texture2D* staging = nullptr;
    result = renderer.device->CreateTexture2D(&desc, nullptr, &staging);
    if (SUCCEEDED(result))
    {
        renderer.context->CopyResource(staging, back_buffer);
    }
    RELEASE_COM(back_buffer);
    if (FAILED(result))
    {
        LOG_ERROR("screenshot: could not create a staging texture: 0x%08lx",
                  (unsigned long)result);
        return false;
    }

    int width = (int)desc.Width;
    int height = (int)desc.Height;
    uint8_t* rgba = ARENA_PUSH_ARRAY(frame, uint8_t, (size_t)width * (size_t)height * 4);
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    result = E_OUTOFMEMORY;
    if (rgba)
    {
        result = renderer.context->Map(staging, 0, D3D11_MAP_READ, 0, &mapped);
    }
    if (FAILED(result))
    {
        LOG_ERROR("screenshot: could not read pixels: 0x%08lx", (unsigned long)result);
        RELEASE_COM(staging);
        return false;
    }

    // The back buffer is BGRA; PNG wants RGBA.
    for (int y = 0; y < height; y += 1)
    {
        const uint8_t* source = (const uint8_t*)mapped.pData + (size_t)y * mapped.RowPitch;
        uint8_t* dest = rgba + (size_t)y * (size_t)width * 4;
        for (int x = 0; x < width; x += 1)
        {
            dest[x * 4 + 0] = source[x * 4 + 2];
            dest[x * 4 + 1] = source[x * 4 + 1];
            dest[x * 4 + 2] = source[x * 4 + 0];
            dest[x * 4 + 3] = 255;
        }
    }
    renderer.context->Unmap(staging, 0);
    RELEASE_COM(staging);

    return assets_write_png(path, width, height, rgba);
}

void renderer_end_frame()
{
    renderer_flush();

    HRESULT result = renderer.swap_chain->Present(1, 0);
    if (FAILED(result))
    {
        LOG_ERROR("Present failed: 0x%08lx", (unsigned long)result);
    }

    if (renderer.info_queue)
    {
        log_debug_messages();
    }
}

int shader_load(const char* name)
{
    for (int i = 0; i < renderer.shader_count; i += 1)
    {
        if (strcmp(renderer.shaders[i].name, name) == 0)
        {
            return i;
        }
    }
    if (renderer.shader_count >= MAX_SHADERS)
    {
        LOG_ERROR("too many shaders, can't load '%s'", name);
        return -1;
    }

    std::string file_name = std::string("shaders/") + name + ".hlsl";
    std::vector<uint8_t> source;
    if (!assets_read_file(file_name.c_str(), source))
    {
        return -1;
    }

    ID3DBlob* vertex_code = nullptr;
    ID3DBlob* pixel_code = nullptr;
    if (!compile_stage(source, file_name.c_str(), "vs_main", "vs_5_0", &vertex_code) ||
        !compile_stage(source, file_name.c_str(), "ps_main", "ps_5_0", &pixel_code))
    {
        RELEASE_COM(vertex_code);
        return -1;
    }

    Shader shader = {};
    snprintf(shader.name, MAX_SHADER_NAME, "%s", name);
    HRESULT vertex_result = renderer.device->CreateVertexShader(
        vertex_code->GetBufferPointer(), vertex_code->GetBufferSize(), nullptr,
        &shader.vertex_shader);
    HRESULT pixel_result = renderer.device->CreatePixelShader(
        pixel_code->GetBufferPointer(), pixel_code->GetBufferSize(), nullptr,
        &shader.pixel_shader);
    HRESULT layout_result = renderer.device->CreateInputLayout(
        SPRITE_VERTEX_LAYOUT, SPRITE_VERTEX_LAYOUT_COUNT, vertex_code->GetBufferPointer(),
        vertex_code->GetBufferSize(), &shader.input_layout);
    RELEASE_COM(pixel_code);
    RELEASE_COM(vertex_code);

    if (FAILED(vertex_result) || FAILED(pixel_result) || FAILED(layout_result))
    {
        LOG_ERROR("could not create shader objects for '%s'", file_name.c_str());
        RELEASE_COM(shader.input_layout);
        RELEASE_COM(shader.pixel_shader);
        RELEASE_COM(shader.vertex_shader);
        return -1;
    }

    int id = renderer.shader_count;
    renderer.shaders[id] = shader;
    renderer.shader_count += 1;
    LOG_INFO("shader '%s' loaded", name);
    return id;
}

void renderer_set_shader(int shader)
{
    if (shader < 0 || shader >= renderer.shader_count)
    {
        shader = renderer.default_shader;
    }
    if (shader != renderer.batch_shader)
    {
        renderer_flush();
        renderer.batch_shader = shader;
    }
}

int renderer_create_texture(int width, int height, const uint8_t* rgba)
{
    if (renderer.texture_count >= MAX_TEXTURES)
    {
        LOG_ERROR("too many textures");
        return -1;
    }

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = (UINT)width;
    desc.Height = (UINT)height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA data = {};
    data.pSysMem = rgba;
    data.SysMemPitch = (UINT)width * 4;

    Texture texture = {};
    texture.width = width;
    texture.height = height;
    HRESULT result = renderer.device->CreateTexture2D(&desc, &data, &texture.texture);
    if (SUCCEEDED(result))
    {
        result = renderer.device->CreateShaderResourceView(texture.texture, nullptr,
                                                           &texture.view);
    }
    if (FAILED(result))
    {
        LOG_ERROR("could not create a %dx%d texture: 0x%08lx", width, height,
                  (unsigned long)result);
        RELEASE_COM(texture.view);
        RELEASE_COM(texture.texture);
        return -1;
    }

    int id = renderer.texture_count;
    renderer.textures[id] = texture;
    renderer.texture_count += 1;
    return id;
}

void renderer_get_texture_size(int texture, int* width, int* height)
{
    if (texture < 0 || texture >= renderer.texture_count)
    {
        *width = 0;
        *height = 0;
        return;
    }
    *width = renderer.textures[texture].width;
    *height = renderer.textures[texture].height;
}

void renderer_set_camera(float x, float y)
{
    if (x == renderer.camera_x && y == renderer.camera_y)
    {
        return;
    }
    renderer_flush();
    renderer.camera_x = x;
    renderer.camera_y = y;
    renderer.constants_dirty = true;
}

void renderer_get_size(int* width, int* height)
{
    *width = renderer.width;
    *height = renderer.height;
}

void renderer_draw_rect(Rect dest, uint32_t color)
{
    push_quad(renderer.white_texture, dest, 0.0f, 0.0f, 1.0f, 1.0f, color);
}

void renderer_draw_sprite(int texture, Rect source, Rect dest, uint32_t tint)
{
    if (texture < 0 || texture >= renderer.texture_count)
    {
        return;
    }

    float texture_width = (float)renderer.textures[texture].width;
    float texture_height = (float)renderer.textures[texture].height;
    float u0 = source.x / texture_width;
    float v0 = source.y / texture_height;
    float u1 = (source.x + source.w) / texture_width;
    float v1 = (source.y + source.h) / texture_height;
    push_quad(texture, dest, u0, v0, u1, v1, tint);
}

static bool create_batch_objects(Arena& permanent)
{
    renderer.vertices = ARENA_PUSH_ARRAY(permanent, SpriteVertex,
                                         (size_t)MAX_QUADS * VERTICES_PER_QUAD);
    if (!renderer.vertices)
    {
        return false;
    }

    D3D11_BUFFER_DESC vertex_desc = {};
    vertex_desc.ByteWidth = (UINT)(sizeof(SpriteVertex) * MAX_QUADS * VERTICES_PER_QUAD);
    vertex_desc.Usage = D3D11_USAGE_DYNAMIC;
    vertex_desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    vertex_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    HRESULT result = renderer.device->CreateBuffer(&vertex_desc, nullptr,
                                                   &renderer.vertex_buffer);

    // Every quad uses the same index pattern, so the index buffer never changes.
    std::vector<uint16_t> indices((size_t)MAX_QUADS * INDICES_PER_QUAD);
    for (int quad = 0; quad < MAX_QUADS; quad += 1)
    {
        uint16_t first = (uint16_t)(quad * VERTICES_PER_QUAD);
        uint16_t* out = indices.data() + (size_t)quad * INDICES_PER_QUAD;
        out[0] = first;
        out[1] = (uint16_t)(first + 1);
        out[2] = (uint16_t)(first + 2);
        out[3] = first;
        out[4] = (uint16_t)(first + 2);
        out[5] = (uint16_t)(first + 3);
    }
    D3D11_BUFFER_DESC index_desc = {};
    index_desc.ByteWidth = (UINT)(sizeof(uint16_t) * indices.size());
    index_desc.Usage = D3D11_USAGE_IMMUTABLE;
    index_desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
    D3D11_SUBRESOURCE_DATA index_data = {};
    index_data.pSysMem = indices.data();
    if (SUCCEEDED(result))
    {
        result = renderer.device->CreateBuffer(&index_desc, &index_data, &renderer.index_buffer);
    }

    D3D11_BUFFER_DESC constant_desc = {};
    constant_desc.ByteWidth = sizeof(FrameConstants);
    constant_desc.Usage = D3D11_USAGE_DEFAULT;
    constant_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    if (SUCCEEDED(result))
    {
        result = renderer.device->CreateBuffer(&constant_desc, nullptr,
                                               &renderer.constant_buffer);
    }

    // Straight (not premultiplied) alpha, as PNGs store it.
    D3D11_BLEND_DESC blend_desc = {};
    blend_desc.RenderTarget[0].BlendEnable = TRUE;
    blend_desc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    blend_desc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    blend_desc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    blend_desc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    blend_desc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    blend_desc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    blend_desc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    if (SUCCEEDED(result))
    {
        result = renderer.device->CreateBlendState(&blend_desc, &renderer.blend_state);
    }

    // Sprites can be mirrored with negative sizes, so nothing is culled.
    D3D11_RASTERIZER_DESC rasterizer_desc = {};
    rasterizer_desc.FillMode = D3D11_FILL_SOLID;
    rasterizer_desc.CullMode = D3D11_CULL_NONE;
    rasterizer_desc.DepthClipEnable = TRUE;
    if (SUCCEEDED(result))
    {
        result = renderer.device->CreateRasterizerState(&rasterizer_desc,
                                                        &renderer.rasterizer_state);
    }

    // Point filtering keeps pixel art sharp when scaled.
    D3D11_SAMPLER_DESC sampler_desc = {};
    sampler_desc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    sampler_desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler_desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler_desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler_desc.MaxLOD = D3D11_FLOAT32_MAX;
    if (SUCCEEDED(result))
    {
        result = renderer.device->CreateSamplerState(&sampler_desc, &renderer.point_sampler);
    }

    if (FAILED(result))
    {
        LOG_ERROR("could not create the sprite batch objects: 0x%08lx", (unsigned long)result);
        return false;
    }
    return true;
}

static void upload_constants()
{
    // Maps pixels (camera at the top-left, y down) to clip space (-1..1, y up).
    float width = (float)renderer.width;
    float height = (float)renderer.height;
    FrameConstants constants = {};
    constants.projection[0] = 2.0f / width;
    constants.projection[5] = -2.0f / height;
    constants.projection[10] = 1.0f;
    constants.projection[12] = -2.0f * renderer.camera_x / width - 1.0f;
    constants.projection[13] = 2.0f * renderer.camera_y / height + 1.0f;
    constants.projection[15] = 1.0f;

    renderer.context->UpdateSubresource(renderer.constant_buffer, 0, nullptr, &constants, 0, 0);
    renderer.constants_dirty = false;
}

static void push_quad(int texture, Rect dest, float u0, float v0, float u1, float v1,
                      uint32_t color)
{
    if (texture != renderer.batch_texture)
    {
        renderer_flush();
        renderer.batch_texture = texture;
    }
    if (renderer.quad_count >= MAX_QUADS)
    {
        renderer_flush();
    }

    float x0 = dest.x;
    float y0 = dest.y;
    float x1 = dest.x + dest.w;
    float y1 = dest.y + dest.h;
    uint32_t vertex_color = color_to_vertex(color);

    SpriteVertex* out = renderer.vertices + (size_t)renderer.quad_count * VERTICES_PER_QUAD;
    out[0] = { .x = x0, .y = y0, .u = u0, .v = v0, .color = vertex_color };
    out[1] = { .x = x1, .y = y0, .u = u1, .v = v0, .color = vertex_color };
    out[2] = { .x = x1, .y = y1, .u = u1, .v = v1, .color = vertex_color };
    out[3] = { .x = x0, .y = y1, .u = u0, .v = v1, .color = vertex_color };
    renderer.quad_count += 1;
}

// 0xRRGGBBAA to the byte order R8G8B8A8_UNORM reads from a little-endian uint32.
static uint32_t color_to_vertex(uint32_t rgba)
{
    uint32_t r = (rgba >> 24) & 0xFF;
    uint32_t g = (rgba >> 16) & 0xFF;
    uint32_t b = (rgba >> 8) & 0xFF;
    uint32_t a = rgba & 0xFF;
    return r | (g << 8) | (b << 16) | (a << 24);
}

static bool create_device()
{
    D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0 };
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#ifndef NDEBUG
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    HRESULT result = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, levels,
                                       1, D3D11_SDK_VERSION, &renderer.device, nullptr,
                                       &renderer.context);
    if (FAILED(result) && (flags & D3D11_CREATE_DEVICE_DEBUG))
    {
        LOG_WARN("D3D11 debug layer not available (install Graphics Tools), continuing without");
        flags &= ~(UINT)D3D11_CREATE_DEVICE_DEBUG;
        result = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, levels, 1,
                                   D3D11_SDK_VERSION, &renderer.device, nullptr,
                                   &renderer.context);
    }
    if (FAILED(result))
    {
        LOG_ERROR("D3D11CreateDevice failed: 0x%08lx", (unsigned long)result);
        return false;
    }

    if (flags & D3D11_CREATE_DEVICE_DEBUG)
    {
        renderer.device->QueryInterface(__uuidof(ID3D11InfoQueue),
                                        (void**)&renderer.info_queue);
    }
    return true;
}

static bool create_swap_chain()
{
    IDXGIDevice* dxgi_device = nullptr;
    IDXGIAdapter* adapter = nullptr;
    IDXGIFactory2* factory = nullptr;

    HRESULT result = renderer.device->QueryInterface(__uuidof(IDXGIDevice),
                                                     (void**)&dxgi_device);
    if (SUCCEEDED(result))
    {
        result = dxgi_device->GetAdapter(&adapter);
    }
    if (SUCCEEDED(result))
    {
        result = adapter->GetParent(__uuidof(IDXGIFactory2), (void**)&factory);
    }

    HWND hwnd = platform_win32_get_hwnd(renderer.window);
    if (SUCCEEDED(result))
    {
        DXGI_SWAP_CHAIN_DESC1 desc = {};
        desc.Width = (UINT)renderer.width;
        desc.Height = (UINT)renderer.height;
        desc.Format = BACK_BUFFER_FORMAT;
        desc.SampleDesc.Count = 1;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount = SWAP_CHAIN_BUFFERS;
        desc.Scaling = DXGI_SCALING_NONE;
        desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        result = factory->CreateSwapChainForHwnd(renderer.device, hwnd, &desc, nullptr, nullptr,
                                                 &renderer.swap_chain);
    }
    if (SUCCEEDED(result))
    {
        // Fullscreen is the game's job, not DXGI's.
        factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);
    }

    RELEASE_COM(factory);
    RELEASE_COM(adapter);
    RELEASE_COM(dxgi_device);

    if (FAILED(result))
    {
        LOG_ERROR("could not create the swap chain: 0x%08lx", (unsigned long)result);
        return false;
    }
    return true;
}

static bool create_render_target()
{
    ID3D11Texture2D* back_buffer = nullptr;
    HRESULT result = renderer.swap_chain->GetBuffer(0, __uuidof(ID3D11Texture2D),
                                                    (void**)&back_buffer);
    if (SUCCEEDED(result))
    {
        result = renderer.device->CreateRenderTargetView(back_buffer, nullptr,
                                                         &renderer.render_target);
    }
    RELEASE_COM(back_buffer);

    if (FAILED(result))
    {
        LOG_ERROR("could not create the render target: 0x%08lx", (unsigned long)result);
        return false;
    }
    return true;
}

static void resize_if_needed()
{
    int width = 0;
    int height = 0;
    platform_get_window_size(renderer.window, &width, &height);

    // Minimized windows report zero; keep the old buffers until it comes back.
    if (width <= 0 || height <= 0 || (width == renderer.width && height == renderer.height))
    {
        return;
    }

    renderer.context->OMSetRenderTargets(0, nullptr, nullptr);
    RELEASE_COM(renderer.render_target);

    HRESULT result = renderer.swap_chain->ResizeBuffers(0, (UINT)width, (UINT)height,
                                                        DXGI_FORMAT_UNKNOWN, 0);
    if (FAILED(result))
    {
        LOG_ERROR("ResizeBuffers failed: 0x%08lx", (unsigned long)result);
    }
    create_render_target();

    renderer.width = width;
    renderer.height = height;
    LOG_INFO("renderer resized to %dx%d", width, height);
}

static void log_debug_messages()
{
    UINT64 count = renderer.info_queue->GetNumStoredMessages();
    if (count > MAX_DEBUG_MESSAGES_PER_FRAME)
    {
        LOG_WARN("D3D11 debug layer: %llu messages, logging the first %llu", count,
                 MAX_DEBUG_MESSAGES_PER_FRAME);
        count = MAX_DEBUG_MESSAGES_PER_FRAME;
    }

    std::vector<uint8_t> buffer;
    for (UINT64 i = 0; i < count; i += 1)
    {
        SIZE_T size = 0;
        renderer.info_queue->GetMessage(i, nullptr, &size);
        buffer.resize(size);
        D3D11_MESSAGE* message = (D3D11_MESSAGE*)buffer.data();
        if (FAILED(renderer.info_queue->GetMessage(i, message, &size)))
        {
            continue;
        }

        switch (message->Severity)
        {
            case D3D11_MESSAGE_SEVERITY_CORRUPTION:
            case D3D11_MESSAGE_SEVERITY_ERROR:
                LOG_ERROR("D3D11: %.*s", (int)message->DescriptionByteLength,
                          message->pDescription);
                break;

            case D3D11_MESSAGE_SEVERITY_WARNING:
                LOG_WARN("D3D11: %.*s", (int)message->DescriptionByteLength,
                         message->pDescription);
                break;

            case D3D11_MESSAGE_SEVERITY_INFO:
            case D3D11_MESSAGE_SEVERITY_MESSAGE:
                LOG_DEBUG("D3D11: %.*s", (int)message->DescriptionByteLength,
                          message->pDescription);
                break;
        }
    }
    renderer.info_queue->ClearStoredMessages();
}

static bool compile_stage(const std::vector<uint8_t>& source, const char* path,
                          const char* entry, const char* target, ID3DBlob** out)
{
    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#ifndef NDEBUG
    flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    ID3DBlob* errors = nullptr;
    HRESULT result = D3DCompile(source.data(), source.size(), path, nullptr, nullptr, entry,
                                target, flags, 0, out, &errors);
    if (FAILED(result))
    {
        if (errors)
        {
            LOG_ERROR("shader compile failed (%s):\n%.*s", entry, (int)errors->GetBufferSize(),
                      (const char*)errors->GetBufferPointer());
        }
        else
        {
            LOG_ERROR("shader compile failed for '%s' (%s): 0x%08lx", path, entry,
                      (unsigned long)result);
        }
        RELEASE_COM(errors);
        return false;
    }

    RELEASE_COM(errors);
    return true;
}

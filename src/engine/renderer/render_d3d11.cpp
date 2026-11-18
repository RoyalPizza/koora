#include "engine/renderer/renderer.hpp"

#include "engine/assets/assets.hpp"
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
constexpr int MAX_FOLDER_LEN = 1024;
constexpr UINT SWAP_CHAIN_BUFFERS = 2;
constexpr DXGI_FORMAT BACK_BUFFER_FORMAT = DXGI_FORMAT_B8G8R8A8_UNORM;
constexpr UINT64 MAX_DEBUG_MESSAGES_PER_FRAME = 32;

constexpr D3D11_INPUT_ELEMENT_DESC SPRITE_VERTEX_LAYOUT[] = {
    { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    { "COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0 },
};

struct Shader
{
    char name[MAX_SHADER_NAME];
    ID3D11VertexShader* vertex_shader;
    ID3D11PixelShader* pixel_shader;
    ID3D11InputLayout* input_layout;
};

struct Renderer
{
    Window* window;
    char shader_folder[MAX_FOLDER_LEN];
    ID3D11Device* device;
    ID3D11DeviceContext* context;
    ID3D11InfoQueue* info_queue; // only with the debug layer
    IDXGISwapChain1* swap_chain;
    ID3D11RenderTargetView* render_target;
    int width;
    int height;
    Shader shaders[MAX_SHADERS];
    int shader_count;
};

// The device is genuinely single-instance, so its state is file-level.
static Renderer renderer;

static bool create_device();
static bool create_swap_chain();
static bool create_render_target();
static void resize_if_needed();
static void log_debug_messages();
static bool compile_stage(const std::vector<uint8_t>& source, const char* path,
                          const char* entry, const char* target, ID3DBlob** out);

bool renderer_init(Window* window, const char* shader_folder)
{
    renderer = {};
    renderer.window = window;
    snprintf(renderer.shader_folder, MAX_FOLDER_LEN, "%s", shader_folder);
    platform_get_window_size(window, &renderer.width, &renderer.height);

    if (!create_device() || !create_swap_chain() || !create_render_target())
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

    for (int i = renderer.shader_count - 1; i >= 0; i -= 1)
    {
        Shader& shader = renderer.shaders[i];
        RELEASE_COM(shader.input_layout);
        RELEASE_COM(shader.pixel_shader);
        RELEASE_COM(shader.vertex_shader);
    }
    renderer.shader_count = 0;

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
}

void renderer_end_frame()
{
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

    std::string path = std::string(renderer.shader_folder) + name + ".hlsl";
    std::vector<uint8_t> source;
    if (!assets_read_file(path.c_str(), source))
    {
        return -1;
    }

    ID3DBlob* vertex_code = nullptr;
    ID3DBlob* pixel_code = nullptr;
    if (!compile_stage(source, path.c_str(), "vs_main", "vs_5_0", &vertex_code) ||
        !compile_stage(source, path.c_str(), "ps_main", "ps_5_0", &pixel_code))
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
    UINT layout_count = (UINT)(sizeof(SPRITE_VERTEX_LAYOUT) / sizeof(SPRITE_VERTEX_LAYOUT[0]));
    HRESULT layout_result = renderer.device->CreateInputLayout(
        SPRITE_VERTEX_LAYOUT, layout_count, vertex_code->GetBufferPointer(), vertex_code->GetBufferSize(), &shader.input_layout);
    RELEASE_COM(pixel_code);
    RELEASE_COM(vertex_code);

    if (FAILED(vertex_result) || FAILED(pixel_result) || FAILED(layout_result))
    {
        LOG_ERROR("could not create shader objects for '%s'", path.c_str());
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

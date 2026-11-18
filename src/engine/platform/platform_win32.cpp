#include "engine/platform/platform.hpp"

#include "engine/core/log.hpp"
#include "engine/platform/platform_win32.hpp"

#include <windows.h>

#include <cstddef>
#include <cstdio>
#include <string>

constexpr int MAX_WIDE_PATH = 1024;
constexpr int MAX_WIDE_TITLE = 256;
constexpr const wchar_t* WINDOW_CLASS_NAME = L"koora_window";

struct Window
{
    HWND hwnd = nullptr;
    int width = 0;
    int height = 0;
    bool quit_requested = false;
};

// Win32 allows one window procedure per class and has no user pointer before WM_CREATE, so the
// single window's state lives here.
static Window window_state;
static bool window_exists = false;

static bool to_wide(const char* text, wchar_t* out, int out_len);
static LRESULT CALLBACK window_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

void* platform_alloc(size_t bytes)
{
    return VirtualAlloc(nullptr, bytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
}

void platform_free(void* memory)
{
    if (memory)
    {
        VirtualFree(memory, 0, MEM_RELEASE);
    }
}

double platform_get_time()
{
    LARGE_INTEGER frequency;
    LARGE_INTEGER counter;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&counter);
    return (double)counter.QuadPart / (double)frequency.QuadPart;
}

std::string platform_get_exe_folder()
{
    wchar_t wide[MAX_WIDE_PATH];
    DWORD len = GetModuleFileNameW(nullptr, wide, MAX_WIDE_PATH);
    if (len == 0 || len >= MAX_WIDE_PATH)
    {
        return std::string();
    }

    while (len > 0 && wide[len - 1] != L'\\' && wide[len - 1] != L'/')
    {
        len -= 1;
    }
    wide[len] = 0;

    int bytes = WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
    if (bytes <= 0)
    {
        return std::string();
    }

    std::string result((size_t)bytes - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide, -1, result.data(), bytes, nullptr, nullptr);
    return result;
}

FILE* platform_file_open(const char* path, const char* mode)
{
    wchar_t wide_path[MAX_WIDE_PATH];
    wchar_t wide_mode[16];
    if (!to_wide(path, wide_path, MAX_WIDE_PATH) || !to_wide(mode, wide_mode, 16))
    {
        return nullptr;
    }

    FILE* file = nullptr;
    if (_wfopen_s(&file, wide_path, wide_mode) != 0)
    {
        return nullptr;
    }
    return file;
}

bool platform_file_rename(const char* from, const char* to)
{
    wchar_t wide_from[MAX_WIDE_PATH];
    wchar_t wide_to[MAX_WIDE_PATH];
    if (!to_wide(from, wide_from, MAX_WIDE_PATH) || !to_wide(to, wide_to, MAX_WIDE_PATH))
    {
        return false;
    }
    return MoveFileExW(wide_from, wide_to, MOVEFILE_REPLACE_EXISTING) != 0;
}

bool platform_file_delete(const char* path)
{
    wchar_t wide[MAX_WIDE_PATH];
    if (!to_wide(path, wide, MAX_WIDE_PATH))
    {
        return false;
    }
    return DeleteFileW(wide) != 0;
}

bool platform_file_exists(const char* path)
{
    wchar_t wide[MAX_WIDE_PATH];
    if (!to_wide(path, wide, MAX_WIDE_PATH))
    {
        return false;
    }
    return GetFileAttributesW(wide) != INVALID_FILE_ATTRIBUTES;
}

bool platform_create_folder(const char* path)
{
    wchar_t wide[MAX_WIDE_PATH];
    if (!to_wide(path, wide, MAX_WIDE_PATH))
    {
        return false;
    }

    // Create each parent in turn; CreateDirectoryW only makes the last component.
    for (int i = 0; wide[i] != 0; i += 1)
    {
        bool is_separator = wide[i] == L'\\' || wide[i] == L'/';
        if (is_separator && i > 0 && wide[i - 1] != L':')
        {
            wide[i] = 0;
            CreateDirectoryW(wide, nullptr);
            wide[i] = L'\\';
        }
    }
    CreateDirectoryW(wide, nullptr);

    DWORD attributes = GetFileAttributesW(wide);
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY);
}

void platform_debug_output(const char* text)
{
    wchar_t wide[2048];
    if (to_wide(text, wide, 2048))
    {
        OutputDebugStringW(wide);
    }
}

Window* platform_create_window(const char* title, int width, int height)
{
    if (window_exists)
    {
        LOG_ERROR("only one window is supported");
        return nullptr;
    }

    // Without this, Windows scales the window up on high-DPI screens and blurs it.
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSEXW window_class = {};
    window_class.cbSize = sizeof(window_class);
    window_class.style = CS_HREDRAW | CS_VREDRAW;
    window_class.lpfnWndProc = window_proc;
    window_class.hInstance = instance;
    window_class.hCursor = LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW);
    window_class.lpszClassName = WINDOW_CLASS_NAME;
    if (!RegisterClassExW(&window_class))
    {
        LOG_ERROR("RegisterClassExW failed: %lu", GetLastError());
        return nullptr;
    }

    DWORD style = WS_OVERLAPPEDWINDOW;
    RECT rect = { 0, 0, width, height };
    AdjustWindowRectEx(&rect, style, FALSE, 0);

    wchar_t wide_title[MAX_WIDE_TITLE];
    if (!to_wide(title, wide_title, MAX_WIDE_TITLE))
    {
        wide_title[0] = 0;
    }

    window_state = {};
    HWND hwnd = CreateWindowExW(0, WINDOW_CLASS_NAME, wide_title, style, CW_USEDEFAULT,
                                CW_USEDEFAULT, rect.right - rect.left, rect.bottom - rect.top,
                                nullptr, nullptr, instance, nullptr);
    if (!hwnd)
    {
        LOG_ERROR("CreateWindowExW failed: %lu", GetLastError());
        UnregisterClassW(WINDOW_CLASS_NAME, instance);
        return nullptr;
    }

    window_state.hwnd = hwnd;
    window_exists = true;

    RECT client;
    GetClientRect(hwnd, &client);
    window_state.width = client.right - client.left;
    window_state.height = client.bottom - client.top;

    ShowWindow(hwnd, SW_SHOW);
    LOG_INFO("window created, client area %dx%d", window_state.width, window_state.height);
    return &window_state;
}

void platform_destroy_window(Window* window)
{
    if (!window || !window_exists)
    {
        return;
    }

    DestroyWindow(window->hwnd);
    UnregisterClassW(WINDOW_CLASS_NAME, GetModuleHandleW(nullptr));
    window->hwnd = nullptr;
    window_exists = false;
    LOG_INFO("window destroyed");
}

bool platform_poll_events(Window* window)
{
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
    {
        if (msg.message == WM_QUIT)
        {
            window->quit_requested = true;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return !window->quit_requested;
}

void platform_get_window_size(Window* window, int* width, int* height)
{
    *width = window->width;
    *height = window->height;
}

HWND platform_win32_get_hwnd(Window* window)
{
    return window->hwnd;
}

static bool to_wide(const char* text, wchar_t* out, int out_len)
{
    return MultiByteToWideChar(CP_UTF8, 0, text, -1, out, out_len) > 0;
}

static LRESULT CALLBACK window_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    switch (msg)
    {
        case WM_SIZE:
            window_state.width = LOWORD(lparam);
            window_state.height = HIWORD(lparam);
            return 0;

        case WM_CLOSE:
            window_state.quit_requested = true;
            return 0;

        case WM_DESTROY:
            return 0;
    }

    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

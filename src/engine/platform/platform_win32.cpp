#include "engine/platform/platform.hpp"

#include <windows.h>

#include <cstddef>
#include <cstdio>
#include <string>

constexpr int MAX_WIDE_PATH = 1024;

static bool to_wide(const char* text, wchar_t* out, int out_len);

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

static bool to_wide(const char* text, wchar_t* out, int out_len)
{
    return MultiByteToWideChar(CP_UTF8, 0, text, -1, out, out_len) > 0;
}

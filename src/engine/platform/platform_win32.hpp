#pragma once

#include "engine/platform/platform.hpp"

#include <windows.h>

// Win32-only access to the window, for other Win32 and D3D11 backend files.

// The native window handle.
HWND platform_win32_get_hwnd(Window* window);

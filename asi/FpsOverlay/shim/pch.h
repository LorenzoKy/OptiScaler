#pragma once

// Stand-in for OptiScaler's precompiled header, so that menu/overlay_telemetry.h and the ImGui Win32 backend
// build inside the standalone FPS overlay plugin. It only has what those files use.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define _CRT_SECURE_NO_WARNINGS
#define NV_WINDOWS

#include <Windows.h>
#include <d3d11.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <nvapi.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include "AsiLog.h"

// The plugin's own module, the ImGui Win32 backend takes its instance from it
inline HMODULE dllModule = nullptr;

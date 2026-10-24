#pragma once

#include <imgui/imgui.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "OverlayStats.h"

// How the swapchain's colors are shown, the overlay's colors are converted to it
enum class UiMode
{
    SDR,
    ScRGB,
    PQ,
};

// What the overlay needs to know about the game's swapchain
struct OverlayHost
{
    ImVec2 displaySize { 0.0f, 0.0f };
    double nowMs = 0.0;
    const char* api = "???";
    HWND window = nullptr;
    UiMode uiMode = UiMode::SDR;
    bool hdrOutputActive = false;
    float menuResScale = 1.0f;
};

// FPS overlay rows, FpsOverlayRowOrder lists their keys. The rows that need OptiScaler's own state (upscaler, frame
// generation, low latency) keep their place so a settings code from OptiScaler keeps its order, but are not shown.
enum OverlayRow : int
{
    OverlayRow_Fps,
    OverlayRow_Upscale,
    OverlayRow_Latency,
    OverlayRow_FrameTime,
    OverlayRow_UpscalerTime,
    OverlayRow_Bottleneck,
    OverlayRow_Refresh,
    OverlayRow_Vram,
    OverlayRow_Gpu,
    OverlayRow_Cpu,
    OverlayRow_Ram,
    OverlayRow_COUNT,
};

extern const char* const overlayRowKeys[OverlayRow_COUNT];
extern const char* const overlayRowNames[OverlayRow_COUNT];

struct OverlayFontEntry
{
    const char* name;
    const char* file;
};

extern const OverlayFontEntry overlayFonts[];
extern const size_t overlayFontCount;

namespace OverlayRender
{
// Rows the plugin can measure on its own
bool RowAvailable(int row);

std::vector<int> RowOrder();
void SetRowOrder(const std::vector<int>& order);

ImVec4 RgbToColor(uint32_t rgb);
uint32_t ColorToRgb(const float* color);
std::filesystem::path FontPath(const std::string& name);

// The scale for a screen height, 1.0 at 1080p
float AutoScale(float displayHeight);

// The default font, before the first frame
void InitFonts();

// Loads the overlay's font when it changed, between frames
void UpdateFont();

void Render(const OverlayHost& host, FrameStats& stats);
} // namespace OverlayRender

#include "pch.h"
#include "OverlayRender.h"
#include "OverlayConfig.h"

#include <imgui/imgui_internal.h>

#include <font/Hack_Compressed.h>
#include <menu/overlay_telemetry.h>

#include <cstdarg>
#include <optional>

static constexpr float fontSize = 14.0f; // The default font size at a scale of 1.0

const char* const overlayRowKeys[OverlayRow_COUNT] = { "fps",      "upscale",    "latency", "frametime",
                                                       "upscaler", "bottleneck", "refresh", "vram",
                                                       "gpu",      "cpu",        "ram" };
const char* const overlayRowNames[OverlayRow_COUNT] = {
    "FPS",        "Upscaler & Ratio", "PC Latency", "Frame Time", "Upscaler Time",
    "Bottleneck", "Refresh Rate",     "VRAM",       "GPU",        "CPU",
    "RAM"
};

// FPS overlay fonts offered in the settings, from the Windows fonts folder
const OverlayFontEntry overlayFonts[] = {
    { "Segoe UI", "segoeui.ttf" },
    { "Segoe UI Semilight", "segoeuisl.ttf" },
    { "Segoe UI Semibold", "seguisb.ttf" },
    { "Segoe UI Bold", "segoeuib.ttf" },
    { "Segoe UI Italic", "segoeuii.ttf" },
    { "Segoe UI Black", "seguibl.ttf" },
    { "Segoe UI Variable", "SegUIVar.ttf" },
    { "Bahnschrift", "bahnschrift.ttf" },
    { "Consolas", "consola.ttf" },
    { "Consolas Bold", "consolab.ttf" },
    { "Consolas Italic", "consolai.ttf" },
    { "Cascadia Mono", "CascadiaMono.ttf" },
    { "Cascadia Code", "CascadiaCode.ttf" },
    { "Unispace Bold", "unispace bd.ttf" },
    { "Lucida Console", "lucon.ttf" },
    { "Lucida Sans Unicode", "l_10646.ttf" },
    { "Courier New", "cour.ttf" },
    { "Courier New Bold", "courbd.ttf" },
    { "Arial", "arial.ttf" },
    { "Arial Bold", "arialbd.ttf" },
    { "Arial Black", "ariblk.ttf" },
    { "Verdana", "verdana.ttf" },
    { "Verdana Bold", "verdanab.ttf" },
    { "Tahoma", "tahoma.ttf" },
    { "Tahoma Bold", "tahomabd.ttf" },
    { "Calibri", "calibri.ttf" },
    { "Calibri Bold", "calibrib.ttf" },
    { "Trebuchet MS", "trebuc.ttf" },
    { "Trebuchet MS Bold", "trebucbd.ttf" },
    { "Century Gothic", "GOTHIC.TTF" },
    { "Century Gothic Bold", "GOTHICB.TTF" },
    { "Franklin Gothic Medium", "framd.ttf" },
    { "Franklin Gothic Demi", "FRADM.TTF" },
    { "Corbel", "corbel.ttf" },
    { "Corbel Bold", "corbelb.ttf" },
    { "Candara", "Candara.ttf" },
    { "Candara Bold", "Candarab.ttf" },
    { "Constantia", "constan.ttf" },
    { "Constantia Bold", "constanb.ttf" },
    { "Palatino Linotype", "pala.ttf" },
    { "Palatino Linotype Bold", "palab.ttf" },
    { "Georgia", "georgia.ttf" },
    { "Georgia Bold", "georgiab.ttf" },
    { "Garamond", "GARA.TTF" },
    { "Garamond Bold", "GARABD.TTF" },
    { "Sitka Text", "Sitka.ttc" },
    { "Sylfaen", "sylfaen.ttf" },
    { "Times New Roman", "times.ttf" },
    { "Times New Roman Bold", "timesbd.ttf" },
    { "Comic Sans MS", "comic.ttf" },
    { "Comic Sans MS Bold", "comicbd.ttf" },
    { "Impact", "impact.ttf" },
};

const size_t overlayFontCount = sizeof(overlayFonts) / sizeof(overlayFonts[0]);

static std::string StrFmt(const char* fmt, ...)
{
    char buffer[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    return buffer;
}

namespace OverlayRender
{
static ImVec2 overlaySize(0.0f, 0.0f);
static ImVec2 overlayPosition(-1000.0f, -1000.0f);

// FPS overlay font, loaded between frames when FpsOverlayFont or its rasterizer settings change
static ImFont* gOverlayFont = nullptr;
static std::string gOverlayFontName;
static float gOverlayLetterSpacing = 0.0f;

bool RowAvailable(int row)
{
    switch (row)
    {
    case OverlayRow_Fps:
    case OverlayRow_FrameTime:
    case OverlayRow_Refresh:
    case OverlayRow_Vram:
    case OverlayRow_Gpu:
    case OverlayRow_Cpu:
    case OverlayRow_Ram:
        return true;

    default:
        return false;
    }
}

// The configured order, rows it doesn't list keep their default place after it
std::vector<int> RowOrder()
{
    // Read on every overlay frame, parsed only when it changes
    static std::string cachedKeys = "-";
    static std::vector<int> cachedOrder;

    const auto configKeys = OverlayConfig::Get().FpsOverlayRowOrder.value_or("");

    if (configKeys == cachedKeys)
        return cachedOrder;

    std::vector<int> order;
    std::stringstream keys(configKeys);
    std::string key;

    while (std::getline(keys, key, ','))
    {
        for (int row = 0; row < OverlayRow_COUNT; row++)
        {
            if (key == overlayRowKeys[row] && std::find(order.begin(), order.end(), row) == order.end())
                order.push_back(row);
        }
    }

    for (int row = 0; row < OverlayRow_COUNT; row++)
    {
        if (std::find(order.begin(), order.end(), row) == order.end())
            order.push_back(row);
    }

    cachedKeys = configKeys;
    cachedOrder = order;

    return order;
}

void SetRowOrder(const std::vector<int>& order)
{
    std::string keys;

    for (auto row : order)
        keys += (keys.empty() ? "" : ",") + std::string(overlayRowKeys[row]);

    if (std::is_sorted(order.begin(), order.end()))
        OverlayConfig::Get().FpsOverlayRowOrder.reset();
    else
        OverlayConfig::Get().FpsOverlayRowOrder = keys;
}

ImVec4 RgbToColor(uint32_t rgb)
{
    return ImVec4(((rgb >> 16) & 0xFF) / 255.0f, ((rgb >> 8) & 0xFF) / 255.0f, (rgb & 0xFF) / 255.0f, 1.0f);
}

uint32_t ColorToRgb(const float* color)
{
    auto channel = [](float c) { return (uint32_t) std::lround(std::clamp(c, 0.0f, 1.0f) * 255.0f); };
    return (channel(color[0]) << 16) | (channel(color[1]) << 8) | channel(color[2]);
}

std::filesystem::path FontPath(const std::string& name)
{
    std::filesystem::path path(name);

    if (path.has_parent_path())
        return path;

    static const std::filesystem::path fontsDir = []()
    {
        wchar_t windowsDir[MAX_PATH] {};
        GetWindowsDirectoryW(windowsDir, MAX_PATH);
        return std::filesystem::path(windowsDir) / L"Fonts";
    }();

    return fontsDir / path;
}

float AutoScale(float displayHeight)
{
    // 1000p is minimum for 1.0 scale
    float result = (float) ((int) (displayHeight / 108.0f)) / 10.0f;

    result = std::round(result * 10.0f) / 10.0f;

    return std::clamp(result, 0.5f, 2.0f);
}

void InitFonts()
{
    auto& io = ImGui::GetIO();

    io.FontDefault = io.Fonts->AddFontFromMemoryCompressedBase85TTF(hack_compressed_compressed_data_base85, fontSize);
}

void UpdateFont()
{
    auto& cfg = OverlayConfig::Get();
    const auto wanted = cfg.FpsOverlayFont.value_or("");
    const float letterSpacing = cfg.FpsOverlayLetterSpacing.get();

    if (wanted == gOverlayFontName && std::abs(letterSpacing - gOverlayLetterSpacing) < 0.001f)
        return;

    gOverlayFontName = wanted;
    gOverlayLetterSpacing = letterSpacing;

    auto& io = ImGui::GetIO();

    if (gOverlayFont != nullptr)
    {
        io.Fonts->RemoveFont(gOverlayFont);
        gOverlayFont = nullptr;
    }

    if (wanted.empty() && std::abs(letterSpacing) < 0.01f)
        return;

    // The settings keep the default font, the first one added would become it
    if (io.FontDefault == nullptr)
        io.FontDefault = io.Fonts->Fonts.empty() ? io.Fonts->AddFontDefault() : io.Fonts->Fonts[0];

    ImFontConfig fontConfig;
    fontConfig.GlyphExtraAdvanceX = letterSpacing;

    if (!wanted.empty())
    {
        auto path = FontPath(wanted);

        if (!std::filesystem::exists(path))
        {
            LOG_WARN("FPS overlay font not found: {}", path.string());
            return;
        }

        gOverlayFont = io.Fonts->AddFontFromFileTTF(path.string().c_str(), fontSize, &fontConfig);
    }
    else
    {
        gOverlayFont = io.Fonts->AddFontFromMemoryCompressedBase85TTF(hack_compressed_compressed_data_base85, fontSize,
                                                                      &fontConfig);
    }
}

static float srgbToLinear(float x)
{
    x = std::clamp(x, 0.0f, 1.0f);

    if (x <= 0.04045f)
        return x / 12.92f;

    return std::pow((x + 0.055f) / 1.055f, 2.4f);
}

static ImVec4 legacyHdrToneMap(const ImVec4& color)
{
    constexpr float exposure = 1.0f;
    constexpr float strength = 1.0f;

    const float peak = std::max(color.x, std::max(color.y, color.z));

    if (peak <= 0.0f)
        return color;

    const float exposedPeak = peak * exposure;
    const float mappedPeak = exposedPeak / (1.0f + exposedPeak);

    const float reinhardScale = mappedPeak / peak;
    const float scale = 1.0f + (reinhardScale - 1.0f) * strength;

    return ImVec4(color.x * scale, color.y * scale, color.z * scale, color.w);
}

// The colors of an SDR overlay as the swapchain shows them
static ImVec4 toneMapColor(const ImVec4& color, UiMode mode, bool hdrOutputActive)
{
    switch (mode)
    {
    case UiMode::ScRGB:
    {
        constexpr float scRgbReferenceWhiteNits = 80.0f;
        constexpr float hdrUiWhiteNits = 203.0f;

        const float uiWhiteNits = hdrOutputActive ? hdrUiWhiteNits : scRgbReferenceWhiteNits;
        const float scale = uiWhiteNits / scRgbReferenceWhiteNits;

        return ImVec4(srgbToLinear(color.x) * scale, srgbToLinear(color.y) * scale, srgbToLinear(color.z) * scale,
                      color.w);
    }

    case UiMode::PQ:
        return legacyHdrToneMap(color);

    default:
        return color;
    }
}

void Render(const OverlayHost& host, FrameStats& stats)
{
    auto& cfg = OverlayConfig::Get();
    auto& io = ImGui::GetIO();
    const double now = host.nowMs;

    if (!cfg.ShowFps.get())
        return;

    const float fpsScale = cfg.FpsScale.value_or(host.menuResScale);
    const bool useTheme = cfg.OverlaysUseTheme.get();

    auto toneMap = [&](const ImVec4& color) { return toneMapColor(color, host.uiMode, host.hdrOutputActive); };

    bool overlayFontPushed = false;

    const static auto defaultStyle = ImGuiStyle();
    const float themeRounding = 6.0f;

    // The overlay's size is its own, the settings window's style is scaled for the screen
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, defaultStyle.WindowPadding * fpsScale);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, defaultStyle.FramePadding * fpsScale);
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, defaultStyle.CellPadding * fpsScale);
    ImGui::PushStyleVar(ImGuiStyleVar_SeparatorTextPadding, defaultStyle.SeparatorTextPadding * fpsScale);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, defaultStyle.ItemSpacing * fpsScale);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, defaultStyle.ItemInnerSpacing * fpsScale);
    ImGui::PushStyleVar(ImGuiStyleVar_IndentSpacing, defaultStyle.IndentSpacing * fpsScale);

    // Corners
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, cfg.FpsOverlayRounding.has()
                                                          ? cfg.FpsOverlayRounding.value() * fpsScale
                                                          : themeRounding * fpsScale);

    // Set overlay position
    ImGui::SetNextWindowPos(overlayPosition, ImGuiCond_Always);

    // Set overlay window properties
    ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(0, 0, 0, 0));  // Transparent border
    ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 0)); // Transparent frame background

    if (!useTheme)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, toneMap(RgbToColor(cfg.FpsOverlayLabelColor.get())));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, toneMap(RgbToColor(cfg.FpsOverlayBGColor.get())));
    }

    ImGui::SetNextWindowBgAlpha(cfg.FpsOverlayAlpha.get()); // Transparent background

    if (!useTheme)
        ImGui::PushStyleColor(ImGuiCol_PlotLines, toneMap(RgbToColor(cfg.FpsOverlayGraphColor.get())));

    if (ImGui::Begin("Performance Overlay", nullptr,
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDecoration |
                         ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing |
                         ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs))
    {
        // Overlay font and size, the default's unless set
        const float overlayFontPx = std::round(fpsScale * cfg.FpsOverlayFontSize.value_or(fontSize));

        if (gOverlayFont != nullptr)
            ImGui::PushFont(gOverlayFont, overlayFontPx);
        else
            ImGui::PushFontSize(overlayFontPx);

        overlayFontPushed = true;

        const auto overlayType = cfg.FpsOverlayType.get();

        // Lines are made of labels (metric names, separators, info) and values, each with its own color and size
        struct OverlayPart
        {
            std::string text;
            bool value = false;
            ImU32 color = 0; // The label or value color when 0
            float width = 0.0f;
        };

        struct OverlayLine
        {
            std::vector<OverlayPart> parts;
            ImVec2 size { 0.0f, 0.0f };
            const char* graphId = nullptr;
            RingBuffer<float, plotWidth>* graphData = nullptr;
            float graphMax = 0.0f;

            OverlayLine& Label(std::string text)
            {
                if (!text.empty())
                    parts.push_back({ std::move(text), false });

                return *this;
            }

            OverlayLine& Value(std::string text, ImU32 color = 0)
            {
                parts.push_back({ std::move(text), true, color });
                return *this;
            }
        };

        // Compact labels for a narrower overlay
        const bool compact = cfg.FpsOverlayCompactLabels.get();
        auto L = [compact](const char* full, const char* shorter) { return std::string(compact ? shorter : full); };

        // The values that change every frame, as their average over the update interval so they can be read
        struct ShownTimes
        {
            double frameRate = 0.0;
            double frameTime = 0.0;
            double averageFrameTime = 0.0;
            double lastFrameTime = 0.0;
        };

        static ShownTimes shown;
        static ShownTimes summed;
        static uint32_t summedFrames = 0;
        static double shownAt = 0.0;

        summed.frameRate += stats.frameRate;
        summed.frameTime += stats.frameTime;
        summed.averageFrameTime += stats.averageFrameTime;
        summed.lastFrameTime += stats.lastFrameTime;
        summedFrames++;

        const float updateIntervalMs = cfg.FpsOverlayUpdateInterval.get();

        if (updateIntervalMs < 1.0f || shownAt <= 0.001 || now < shownAt || now - shownAt >= updateIntervalMs)
        {
            const double frames = static_cast<double>(summedFrames);
            shown = { summed.frameRate / frames, summed.frameTime / frames, summed.averageFrameTime / frames,
                      summed.lastFrameTime / frames };

            summed = {};
            summedFrames = 0;
            shownAt = now;
        }

        const std::string fpsValue = StrFmt("%6.1f", shown.frameRate);

        const bool showLows = cfg.FpsOverlayShowLows.get();
        std::array<std::optional<OverlayLine>, OverlayRow_COUNT> rows;

        // FPS, frame time (Simple), average and 1% low share one row
        if (overlayType == FpsOverlay_JustFPS)
        {
            rows[OverlayRow_Fps] = OverlayLine().Value(fpsValue);
        }
        else
        {
            OverlayLine line;
            line.Label(std::string(host.api) + L(" | FPS: ", " | FPS ")).Value(fpsValue);

            if (overlayType == FpsOverlay_Simple)
                line.Label(", ").Value(StrFmt("%7.2f ms", shown.frameTime));

            if (overlayType >= FpsOverlay_Detailed || showLows)
                line.Label(L(", Avg: ", " Avg ")).Value(StrFmt("%6.1f", 1000.0 / shown.averageFrameTime));

            if (showLows)
                line.Label(L(", 1% Low: ", " 1% ")).Value(StrFmt("%6.1f", stats.onePercentLowFps));

            if (cfg.FpsOverlayShowPointOneLow.get())
                line.Label(L(", 0.1% Low: ", " 0.1% ")).Value(StrFmt("%6.1f", stats.pointOneLowFps));

            rows[OverlayRow_Fps] = std::move(line);
        }

        if (overlayType >= FpsOverlay_Simple &&
            cfg.FpsOverlayShowFrameTime.value_or(overlayType >= FpsOverlay_Detailed))
        {
            OverlayLine line;
            line.Label(L("Frame Time: ", "FT "))
                .Value(StrFmt("%7.2f ms", shown.lastFrameTime))
                .Label(L(", Avg: ", " Avg "))
                .Value(StrFmt("%7.2f ms", shown.averageFrameTime));

            if (overlayType >= FpsOverlay_DetailedGraph)
            {
                line.graphId = "##FrameTimeGraph";
                line.graphData = &stats.frameTimes;
                line.graphMax = 66.6f;
            }

            rows[OverlayRow_FrameTime] = std::move(line);
        }

        auto colorOf = [&](float r, float g, float b)
        { return ImGui::ColorConvertFloat4ToU32(toneMap(ImVec4(r, g, b, 1.0f))); };

        auto& state = State::Instance();

        // The display's refresh rate, with VRR the frames it shows per second
        if (overlayType >= FpsOverlay_Simple && cfg.FpsOverlayShowRefreshRate.get())
        {
            // Often enough to catch the refresh going up to the VRR range's top for a moment, Avg has the trend
            constexpr auto delayBetweenPollsMs = 100;
            static auto previousPoll = 0.0;
            static std::optional<OverlayTelemetry::RefreshRate> refresh;

            if (previousPoll <= 0.001 || previousPoll + delayBetweenPollsMs < now)
            {
                previousPoll = now;
                refresh = OverlayTelemetry::QueryRefreshRate(host.window);
            }

            OverlayLine line;
            line.Label(L("Refresh Rate: ", "Hz "));

            if (refresh.has_value())
            {
                line.Value(StrFmt("%5.1f Hz", refresh->currentHz))
                    .Label(L(", Avg: ", " Avg "))
                    .Value(StrFmt("%5.1f Hz", refresh->averageHz));
            }
            else
            {
                line.Value("N/A");
            }

            rows[OverlayRow_Refresh] = std::move(line);
        }

        const bool showVram = overlayType >= FpsOverlay_Simple && cfg.FpsOverlayShowVram.get();
        const bool showGpu = overlayType >= FpsOverlay_Simple && cfg.FpsOverlayShowGpu.get();
        const bool showCpu = overlayType >= FpsOverlay_Simple && cfg.FpsOverlayShowCpu.get();
        const bool showHardwareNames = cfg.FpsOverlayShowHardwareNames.get();

        static std::optional<std::pair<uint64_t, uint64_t>> vram;
        if (showVram)
        {
            constexpr auto delayBetweenPollsMs = 500;
            static auto previousPoll = 0.0;

            if (previousPoll <= 0.001 || previousPoll + delayBetweenPollsMs < now)
            {
                previousPoll = now;
                vram = OverlayTelemetry::QueryVram(state);
            }
        }

        // The GPU row and the VRAM row's memory clock
        static OverlayTelemetry::GpuMetrics gpu;
        if (showGpu || showVram)
        {
            constexpr auto delayBetweenPollsMs = 500;
            static auto previousPoll = 0.0;

            if (previousPoll <= 0.001 || previousPoll + delayBetweenPollsMs < now)
            {
                previousPoll = now;
                gpu = OverlayTelemetry::QueryGpu(state);
            }
        }

        if (showVram)
        {
            OverlayLine line;
            line.Label(L("VRAM: ", "VRAM "));

            if (vram.has_value() && vram->second != 0)
            {
                constexpr double gb = 1024.0 * 1024.0 * 1024.0;
                const auto [usage, budget] = vram.value();

                // Close to the budget Windows starts moving memory out to the system's
                line.Value(StrFmt("%5.2f", usage / gb), usage > budget * 0.95 ? colorOf(1.0f, 0.67f, 0.24f) : 0)
                    .Label(" / ")
                    .Value(StrFmt("%5.2f GB", budget / gb));
            }
            else
            {
                line.Value("N/A");
            }

            if (gpu.memClockMhz.has_value())
                line.Label(L(" | Clock: ", " ")).Value(StrFmt("%u MHz", gpu.memClockMhz.value()));

            rows[OverlayRow_Vram] = std::move(line);
        }

        if (showGpu)
        {
            OverlayLine line;
            if (showHardwareNames)
                line.Label(OverlayTelemetry::GetGpuLabel(state, compact) + L(": ", " "));
            else
                line.Label(L("GPU: ", "GPU "));

            bool hasAny = false;
            auto sep = [&]()
            {
                if (hasAny)
                    line.Label(L(" | ", " "));
                hasAny = true;
            };

            if (gpu.tempC.has_value())
            {
                sep();
                const double t = gpu.tempC.value();
                const ImU32 col = t >= 90.0 ? colorOf(1.0f, 0.35f, 0.35f) : t >= 82.0 ? colorOf(1.0f, 0.67f, 0.24f) : 0;
                line.Value(StrFmt("%.0f C", t), col);
            }

            if (gpu.powerW.has_value())
            {
                sep();
                line.Value(StrFmt("%.0f W", gpu.powerW.value()));
            }

            if (gpu.voltageV.has_value())
            {
                sep();
                line.Value(StrFmt("%.3f V", gpu.voltageV.value()));
            }

            if (gpu.coreClockMhz.has_value())
            {
                sep();
                line.Value(StrFmt("%u MHz", gpu.coreClockMhz.value()));
            }

            if (!hasAny)
                line.Value("N/A");

            rows[OverlayRow_Gpu] = std::move(line);
        }

        if (showCpu)
        {
            constexpr auto delayBetweenPollsMs = 500;
            static auto previousPoll = 0.0;
            static OverlayTelemetry::CpuMetrics cpu;

            if (previousPoll <= 0.001 || previousPoll + delayBetweenPollsMs < now)
            {
                previousPoll = now;
                cpu = OverlayTelemetry::QueryCpu(state);
            }

            OverlayLine line;
            if (showHardwareNames)
                line.Label(OverlayTelemetry::GetCpuLabel(compact) + L(": ", " "));
            else
                line.Label(L("CPU: ", "CPU "));

            bool hasAny = false;
            auto sep = [&]()
            {
                if (hasAny)
                    line.Label(L(" | ", " "));
                hasAny = true;
            };

            if (cpu.tempC.has_value())
            {
                sep();
                const double t = cpu.tempC.value();
                const ImU32 col = t >= 90.0 ? colorOf(1.0f, 0.35f, 0.35f) : t >= 82.0 ? colorOf(1.0f, 0.67f, 0.24f) : 0;
                line.Value(StrFmt("%.0f C", t), col);
            }

            if (cpu.powerW.has_value())
            {
                sep();
                line.Value(StrFmt("%.0f W", cpu.powerW.value()));
            }

            if (cpu.voltageV.has_value())
            {
                sep();
                line.Value(StrFmt("%.3f V", cpu.voltageV.value()));
            }

            if (cpu.clockMhz.has_value())
            {
                sep();
                line.Value(StrFmt("%u MHz", cpu.clockMhz.value()));
            }

            if (!hasAny)
                line.Value("N/A");

            rows[OverlayRow_Cpu] = std::move(line);
        }

        if (overlayType >= FpsOverlay_Simple && cfg.FpsOverlayShowRam.get())
        {
            constexpr auto delayBetweenPollsMs = 500;
            static auto previousPoll = 0.0;
            static std::optional<OverlayTelemetry::RamMetrics> ram;

            if (previousPoll <= 0.001 || previousPoll + delayBetweenPollsMs < now)
            {
                previousPoll = now;
                ram = OverlayTelemetry::QueryRam();
            }

            OverlayLine line;
            line.Label(L("RAM: ", "RAM "));

            if (ram.has_value() && ram->totalBytes != 0)
            {
                constexpr double gb = 1024.0 * 1024.0 * 1024.0;
                const ImU32 col = ram->usedBytes > ram->totalBytes * 0.90 ? colorOf(1.0f, 0.67f, 0.24f) : 0;

                line.Value(StrFmt("%5.2f", ram->usedBytes / gb), col)
                    .Label(" / ")
                    .Value(StrFmt("%5.2f GB", ram->totalBytes / gb));

                if (cfg.FpsOverlayShowGameRam.get() && ram->gameBytes != 0)
                    line.Label(L(" | Game: ", " G ")).Value(StrFmt("%.2f GB", ram->gameBytes / gb));

                if (cfg.FpsOverlayShowCommitRam.get() && ram->commitUsedBytes != 0)
                    line.Label(L(" | Commit: ", " C ")).Value(StrFmt("%.2f GB", ram->commitUsedBytes / gb));
            }
            else
            {
                line.Value("N/A");
            }

            rows[OverlayRow_Ram] = std::move(line);
        }

        std::vector<OverlayLine> lines;
        for (auto row : RowOrder())
        {
            if (rows[row].has_value())
                lines.push_back(std::move(rows[row].value()));
        }

        // Labels and values are scaled from the overlay's font size
        ImFont* font = ImGui::GetFont();
        const float basePx = ImGui::GetFontSize();
        const float labelPx = std::round(basePx * cfg.FpsOverlayLabelScale.get());
        const float valuePx = std::round(basePx * cfg.FpsOverlayValueScale.get());

        const ImU32 labelColor = ImGui::GetColorU32(ImGuiCol_Text);
        const ImU32 valueColor =
            useTheme ? labelColor : ImGui::ColorConvertFloat4ToU32(toneMap(RgbToColor(cfg.FpsOverlayValueColor.get())));

        // A stronger shadow is thicker and darker
        const auto shadowType = cfg.FpsOverlayShadowType.get();
        const float shadowStrength = cfg.FpsOverlayShadowStrength.get();
        const float shadowSize = std::max(1.0f, std::round(basePx / 14.0f * (0.5f + shadowStrength * 1.5f)));
        auto shadowVec = toneMap(RgbToColor(cfg.FpsOverlayShadowColor.get()));
        shadowVec.w = 0.35f + 0.65f * shadowStrength;
        const ImU32 shadowColor = ImGui::ColorConvertFloat4ToU32(shadowVec);

        // The outline is a filled circle of opaque copies around the text, translucent ones would add up unevenly
        std::vector<ImVec2> outlineOffsets;

        if (shadowType == FpsOverlayShadow_Outline)
        {
            const int radius = static_cast<int>(shadowSize);
            const int limit = radius * radius + radius;

            for (int dy = -radius; dy <= radius; dy++)
            {
                for (int dx = -radius; dx <= radius; dx++)
                {
                    if ((dx != 0 || dy != 0) && dx * dx + dy * dy <= limit)
                        outlineOffsets.emplace_back(static_cast<float>(dx), static_cast<float>(dy));
                }
            }

            shadowVec.w = 1.0f;
        }

        const ImU32 outlineColor = ImGui::ColorConvertFloat4ToU32(shadowVec);

        auto partPx = [&](const OverlayPart& part) { return part.value ? valuePx : labelPx; };

        // Digits of the values take the widest digit's width, so changing numbers don't shift with fonts whose
        // digits differ in width
        float digitWidth = 0.0f;
        for (char digit = '0'; digit <= '9'; digit++)
            digitWidth = std::max(digitWidth, font->CalcTextSizeA(valuePx, FLT_MAX, 0.0f, &digit, &digit + 1).x);

        // Calls place(begin, end, x) for the part's text, the value's digits one at a time centered in their
        // width. The part's width.
        auto layoutPart = [&](const OverlayPart& part, auto&& place)
        {
            const float px = partPx(part);
            const char* text = part.text.c_str();
            const char* end = text + part.text.size();
            float x = 0.0f;

            auto isDigit = [&](const char* c) { return part.value && *c >= '0' && *c <= '9'; };

            while (text < end)
            {
                if (isDigit(text))
                {
                    const float width = font->CalcTextSizeA(px, FLT_MAX, 0.0f, text, text + 1).x;
                    place(text, text + 1, x + std::round((digitWidth - width) * 0.5f));
                    x += digitWidth;
                    text++;
                    continue;
                }

                const char* runEnd = text;
                while (runEnd < end && !isDigit(runEnd))
                    runEnd++;

                place(text, runEnd, x);
                x += font->CalcTextSizeA(px, FLT_MAX, 0.0f, text, runEnd).x;
                text = runEnd;
            }

            return x;
        };

        auto measureLine = [&](OverlayLine& line)
        {
            ImVec2 size { 0.0f, 0.0f };

            for (auto& part : line.parts)
            {
                const float px = partPx(part);
                part.width = layoutPart(part, [](const char*, const char*, float) {});
                size.x += part.width;
                size.y = std::max(size.y, px);
            }

            line.size = size;
        };

        for (auto& line : lines)
            measureLine(line);

        // On whole pixels, the shadows of a line under all its text so they don't cover the next part
        auto drawText = [&](ImDrawList* drawList, float px, ImVec2 pos, ImU32 color, const char* text, const char* end,
                            bool shadowPass)
        {
            pos = ImVec2(std::round(pos.x), std::round(pos.y));

            if (!shadowPass)
            {
                drawList->AddText(font, px, pos, color, text, end);
            }
            else if (shadowType == FpsOverlayShadow_Shadow)
            {
                for (float d = 1.0f; d <= shadowSize; d++)
                    drawList->AddText(font, px, ImVec2(pos.x + d, pos.y + d), shadowColor, text, end);
            }
            else if (shadowType == FpsOverlayShadow_Outline)
            {
                for (const auto& offset : outlineOffsets)
                    drawList->AddText(font, px, pos + offset, outlineColor, text, end);
            }
        };

        // Parts of different sizes share the baseline
        auto drawLine = [&](OverlayLine& line)
        {
            if (line.size.y == 0.0f && !line.parts.empty())
                measureLine(line);

            float maxAscent = 0.0f;
            for (const auto& part : line.parts)
                maxAscent = std::max(maxAscent, font->GetFontBaked(partPx(part))->Ascent);

            auto drawList = ImGui::GetWindowDrawList();
            const auto start = ImGui::GetCursorScreenPos();

            for (const bool shadowPass : { true, false })
            {
                if (shadowPass && shadowType == FpsOverlayShadow_Off)
                    continue;

                auto pos = start;

                for (const auto& part : line.parts)
                {
                    const float px = partPx(part);
                    const float y = pos.y + maxAscent - font->GetFontBaked(px)->Ascent;
                    const ImU32 color = part.color != 0 ? part.color : part.value ? valueColor : labelColor;

                    layoutPart(part, [&](const char* text, const char* end, float x)
                               { drawText(drawList, px, ImVec2(pos.x + x, y), color, text, end, shadowPass); });

                    pos.x += part.width;
                }
            }

            ImGui::Dummy(line.size);
        };

        const bool horizontal = cfg.FpsOverlayHorizontal.get();
        const float graphHeight = cfg.FpsOverlayGraphHeight.get();

        ImVec2 plotSize;
        if (horizontal)
        {
            plotSize = { fpsScale * 150, fpsScale * 16 * graphHeight };
        }
        else
        {
            // Graphs match the widest text line
            auto textWidth = 0.0f;
            for (const auto& line : lines)
                textWidth = std::max(textWidth, line.size.x);

            plotSize = { std::max(textWidth, fpsScale * 300.0f), fpsScale * 30 * graphHeight };
        }

        // Rows are apart by the row spacing, by default as ImGui::Spacing() puts them
        const float itemSpacingY = ImGui::GetStyle().ItemSpacing.y;
        const float rowSpacing =
            cfg.FpsOverlayRowSpacing.has() ? cfg.FpsOverlayRowSpacing.value() * fpsScale : itemSpacingY * 2.0f;

        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, 0.0f));

        OverlayLine sepLine;
        if (horizontal)
        {
            sepLine.Label(" | ");
            measureLine(sepLine);
        }

        // Draw the overlay
        for (size_t i = 0; i < lines.size(); i++)
        {
            auto& line = lines[i];

            if (i > 0)
            {
                if (horizontal)
                {
                    ImGui::SameLine(0.0f, 0.0f);
                    drawLine(sepLine);
                    ImGui::SameLine(0.0f, 0.0f);
                }
                else
                {
                    ImGui::Dummy(ImVec2(0.0f, rowSpacing));
                }
            }

            drawLine(line);

            if (line.graphData != nullptr)
            {
                if (horizontal)
                    ImGui::SameLine(0.0f, 0.0f);
                else
                    ImGui::Dummy(ImVec2(0.0f, itemSpacingY));

                ImGui::PlotLines(
                    line.graphId,
                    [](void* rb, int idx) -> float { return static_cast<RingBuffer<float, plotWidth>*>(rb)->At(idx); },
                    line.graphData, plotWidth, 0, nullptr, 0.0f, line.graphMax, plotSize);
            }
        }

        ImGui::PopStyleVar();
    }

    // Restore the style
    ImGui::PopStyleColor(useTheme ? 2 : 5);

    // Get size for postioning
    overlaySize = ImGui::GetWindowSize();

    if (overlayFontPushed)
    {
        if (gOverlayFont != nullptr)
            ImGui::PopFont();
        else
            ImGui::PopFontSize();
    }

    ImGui::End();

    ImGui::PopStyleVar(8); // The overlay's style and rounding

    // Where the overlay sits in the free space, 0 left/top to 1 right/bottom
    float alignX = 0.0f;
    float alignY = 0.0f;

    switch (cfg.FpsOverlayPosition.get())
    {
    case FpsOverlayPos_TopRight:
        alignX = 1.0f;
        break;

    case FpsOverlayPos_BottomLeft:
        alignY = 1.0f;
        break;

    case FpsOverlayPos_BottomRight:
        alignX = 1.0f;
        alignY = 1.0f;
        break;

    case FpsOverlayPos_TopCenter:
        alignX = 0.5f;
        break;

    case FpsOverlayPos_BottomCenter:
        alignX = 0.5f;
        alignY = 1.0f;
        break;

    case FpsOverlayPos_Custom:
        alignX = cfg.FpsOverlayPosX.get();
        alignY = cfg.FpsOverlayPosY.get();
        break;

    default:
        break;
    }

    overlayPosition.x = std::round(std::max(0.0f, io.DisplaySize.x - overlaySize.x) * alignX);
    overlayPosition.y = std::round(std::max(0.0f, io.DisplaySize.y - overlaySize.y) * alignY);
}
} // namespace OverlayRender

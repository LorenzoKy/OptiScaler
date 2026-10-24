#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// The overlay's settings carry the names, ranges and defaults of OptiScaler's FPS overlay (Menu section of
// OptiScaler.ini), so a settings code made by one works in the other

enum FpsOverlayPos : uint32_t
{
    FpsOverlayPos_TopLeft,
    FpsOverlayPos_TopRight,
    FpsOverlayPos_BottomLeft,
    FpsOverlayPos_BottomRight,
    FpsOverlayPos_TopCenter,
    FpsOverlayPos_BottomCenter,
    FpsOverlayPos_Custom, // FpsOverlayPosX/Y
    FpsOverlayPos_COUNT,
};

enum FpsOverlayLatency : uint32_t
{
    FpsOverlayLatency_Display,
    FpsOverlayLatency_Render,
    FpsOverlayLatency_Both,
    FpsOverlayLatency_COUNT,
};

enum FpsOverlayShadow : uint32_t
{
    FpsOverlayShadow_Off,
    FpsOverlayShadow_Shadow,
    FpsOverlayShadow_Outline,
    FpsOverlayShadow_COUNT,
};

enum FpsOverlay : uint32_t
{
    FpsOverlay_JustFPS,
    FpsOverlay_Simple,
    FpsOverlay_Detailed,
    FpsOverlay_DetailedGraph,
    FpsOverlay_Full,
    FpsOverlay_FullGraph,
    FpsOverlay_ReflexTimings,
    FpsOverlay_COUNT,
};

// A setting that is either set or follows its default
template <typename T> struct Opt
{
    T def {};
    std::optional<T> v;

    Opt() = default;
    explicit Opt(T d) : def(std::move(d)) {}

    bool has() const { return v.has_value(); }
    T get() const { return v.value_or(def); }
    T value() const { return *v; }
    T value_or(T other) const { return v.value_or(std::move(other)); }
    void reset() { v.reset(); }

    Opt& operator=(const T& x)
    {
        v = x;
        return *this;
    }
};

enum class SettingKind
{
    Bool,
    Float,
    Color,
    Index,
    String,
};

struct Setting
{
    const char* key;
    SettingKind kind;
    void* opt; // The Opt<bool>, Opt<float>, Opt<uint32_t> or Opt<std::string>
    float lo;  // Float range
    float hi;
    uint32_t count; // Index: number of values
    bool shared;    // Part of the settings code
};

class OverlayConfig
{
  public:
    static OverlayConfig& Get();

    // Overlay -----------------------------------------------------------------------------------------------
    Opt<bool> ShowFps { true };
    Opt<bool> OverlaysUseTheme { false };
    Opt<uint32_t> FpsOverlayPosition { FpsOverlayPos_TopLeft };
    Opt<uint32_t> FpsOverlayType { FpsOverlay_JustFPS };
    Opt<bool> FpsOverlayHorizontal { false };
    Opt<bool> FpsOverlayShowRatio { false };
    Opt<bool> FpsOverlayShowLows { false };
    Opt<bool> FpsOverlayShowLatency { false };
    Opt<uint32_t> FpsOverlayLatencyType { FpsOverlayLatency_Display };
    Opt<bool> FpsOverlayShowFrameTime; // Unset means Detailed and above
    Opt<bool> FpsOverlayShowUpscalerTime { false };
    Opt<bool> FpsOverlayShowFpsCost { false };
    Opt<bool> FpsOverlayShowPointOneLow { false };
    Opt<bool> FpsOverlayShowBottleneck { false };
    Opt<bool> FpsOverlayShowVram { false };
    Opt<bool> FpsOverlayShowGpu { false };
    Opt<bool> FpsOverlayShowCpu { false };
    Opt<bool> FpsOverlayShowRefreshRate { false };
    Opt<bool> FpsOverlayShowRam { false };
    Opt<bool> FpsOverlayShowGameRam { true };
    Opt<bool> FpsOverlayShowCommitRam { false };
    Opt<bool> FpsOverlayShowHardwareNames { false };
    Opt<bool> FpsOverlayCompactLabels { false };
    Opt<std::string> FpsOverlayRowOrder;
    Opt<float> FpsOverlayRowSpacing; // Unset means the default spacing
    Opt<float> FpsOverlayUpdateInterval { 0.0f };
    Opt<float> FpsOverlayAlpha { 0.4f };
    Opt<float> FpsScale; // Unset means the scale for the screen's height
    Opt<float> FpsOverlayGraphHeight { 1.0f };
    Opt<float> FpsOverlayPosX { 0.0f };
    Opt<float> FpsOverlayPosY { 0.0f };
    Opt<std::string> FpsOverlayFont;
    Opt<float> FpsOverlayFontSize; // Unset means the default font size
    Opt<float> FpsOverlayLetterSpacing { 0.0f };
    Opt<float> FpsOverlayLabelScale { 1.0f };
    Opt<float> FpsOverlayValueScale { 1.0f };
    Opt<uint32_t> FpsOverlayLabelColor { 0xFFFFFF };
    Opt<uint32_t> FpsOverlayValueColor { 0xFFFFFF };
    Opt<uint32_t> FpsOverlayGraphColor { 0x00FF00 };
    Opt<uint32_t> FpsOverlayBGColor { 0x000000 };
    Opt<uint32_t> FpsOverlayShadowType { FpsOverlayShadow_Off };
    Opt<float> FpsOverlayShadowStrength { 0.5f };
    Opt<uint32_t> FpsOverlayShadowColor { 0x000000 };
    Opt<float> FpsOverlayRounding; // Unset means the default rounding

    // The plugin ---------------------------------------------------------------------------------------------
    Opt<uint32_t> ToggleKey { 0x23 };   // End, shows or hides the overlay
    Opt<uint32_t> SettingsKey { 0x24 }; // Home, opens the settings window

    OverlayConfig();

    // Sets the folder of OptiFpsOverlay.ini and reads it
    void Init(const std::wstring& folder);
    void Load();
    bool Save();

    // Reads the file again when something else changed it
    bool PollReload();

    // Settings code, as OptiScaler's Copy Code and Paste Code
    std::string ExportCode();
    bool ImportCode(const std::string& code);

    // A preset's settings: Key=Value lines, keys without the FpsOverlay prefix. ShowFps stays as it is.
    bool ApplyPreset(std::string_view text);

    void ResetAppearance();

  private:
    std::vector<Setting> settings_;
    std::wstring iniPath_;
    uint64_t lastWrite_ = 0;
    uint64_t lastPoll_ = 0;

    std::string ToText(const Setting& setting) const;
    void FromText(const Setting& setting, const std::string& text);
    bool ApplyText(const std::string& text);
    uint64_t FileTime() const;
};

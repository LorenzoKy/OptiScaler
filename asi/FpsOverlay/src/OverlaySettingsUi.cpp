#include "pch.h"
#include "OverlaySettingsUi.h"
#include "OverlayConfig.h"

#include <imgui/imgui_internal.h>

#include <cstdarg>

namespace OverlaySettingsUi
{
static std::string StrFmt(const char* fmt, ...)
{
    char buffer[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    return buffer;
}

static void ShowTooltip(const char* tip)
{
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
        ImGui::SetTooltip("%s", tip);
}

// Results and hints on lines of their own, wrapped to the window's width
static void StatusText(const std::string& text)
{
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", text.c_str());
    ImGui::PopStyleColor();
}

static bool OptionCheckbox(const char* label, Opt<bool>& setting, const char* tip)
{
    bool value = setting.get();
    const bool changed = ImGui::Checkbox(label, &value);

    if (changed)
        setting = value;

    ShowTooltip(tip);
    return changed;
}

static void ColorEdit(const char* label, Opt<uint32_t>& setting)
{
    auto color = OverlayRender::RgbToColor(setting.get());
    float rgb[3] = { color.x, color.y, color.z };

    if (ImGui::ColorEdit3(label, rgb, ImGuiColorEditFlags_NoInputs))
        setting = OverlayRender::ColorToRgb(rgb);
}

static void ScaleSlider(const char* label, Opt<float>& setting)
{
    float value = setting.get();

    if (ImGui::SliderFloat(label, &value, 0.5f, 2.0f, "%.2fx", ImGuiSliderFlags_AlwaysClamp))
        setting = value;
}

void Render(const OverlayHost& host, bool& open)
{
    auto& cfg = OverlayConfig::Get();

    ImGui::SetNextWindowSize(ImVec2(host.menuResScale * 560.0f, host.menuResScale * 640.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(host.displaySize.x * 0.5f, host.displaySize.y * 0.5f), ImGuiCond_FirstUseEver,
                            ImVec2(0.5f, 0.5f));

    ImGui::PushFontSize(std::round(14.0f * host.menuResScale));

    if (!ImGui::Begin("FPS Overlay", &open, ImGuiWindowFlags_NoSavedSettings))
    {
        ImGui::End();
        ImGui::PopFontSize();
        return;
    }

    ImGui::TextWrapped("Standalone FPS overlay for OptiScaler. It measures the game's swapchain on its own, the rows "
                       "that need OptiScaler's state (upscaler, frame generation, latency) are not available.");
    StatusText(StrFmt("Overlay key: VK 0x%02X, settings key: VK 0x%02X (ToggleKey and SettingsKey in "
                      "OptiFpsOverlay.ini)",
                      cfg.ToggleKey.get(), cfg.SettingsKey.get()));

    if (ImGui::Button("Save Settings"))
        cfg.Save();

    ShowTooltip("Writes OptiFpsOverlay.ini next to the plugin");

    ImGui::SameLine();

    if (ImGui::Button("Reload File"))
        cfg.Load();

    ShowTooltip("Reads OptiFpsOverlay.ini again, dropping changes that are not saved");

    ImGui::SeparatorText("FPS Overlay");

    bool fpsEnabled = cfg.ShowFps.get();
    if (ImGui::Checkbox("FPS Overlay Enabled", &fpsEnabled))
        cfg.ShowFps = fpsEnabled;

    ImGui::SameLine(0.0f, 6.0f);

    bool fpsHorizontal = cfg.FpsOverlayHorizontal.get();
    if (ImGui::Checkbox("Horizontal", &fpsHorizontal))
        cfg.FpsOverlayHorizontal = fpsHorizontal;

    const char* fpsPosition[] = { "Top Left",   "Top Right",     "Bottom Left", "Bottom Right",
                                  "Top Center", "Bottom Center", "Custom" };
    const char* selectedPosition = fpsPosition[cfg.FpsOverlayPosition.get()];

    if (ImGui::BeginCombo("Overlay Position", selectedPosition))
    {
        for (uint32_t n = 0; n < std::size(fpsPosition); n++)
        {
            if (ImGui::Selectable(fpsPosition[n], cfg.FpsOverlayPosition.get() == n))
                cfg.FpsOverlayPosition = n;
        }

        ImGui::EndCombo();
    }

    if (cfg.FpsOverlayPosition.get() == FpsOverlayPos_Custom)
    {
        // 0.00 is the left or top of the screen, 1.00 the right or bottom
        float posX = cfg.FpsOverlayPosX.get();
        if (ImGui::SliderFloat("Horizontal Position", &posX, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp))
            cfg.FpsOverlayPosX = posX;

        float posY = cfg.FpsOverlayPosY.get();
        if (ImGui::SliderFloat("Vertical Position", &posY, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp))
            cfg.FpsOverlayPosY = posY;
    }

    const char* fpsType[] = { "Just FPS", "Simple", "Detailed", "Detailed + Graph", "Full", "Full + Graph" };
    const uint32_t typeIndex = std::min<uint32_t>(cfg.FpsOverlayType.get(), (uint32_t) std::size(fpsType) - 1);

    if (ImGui::BeginCombo("Overlay Type", fpsType[typeIndex]))
    {
        for (uint32_t n = 0; n < std::size(fpsType); n++)
        {
            if (ImGui::Selectable(fpsType[n], cfg.FpsOverlayType.get() == n))
                cfg.FpsOverlayType = n;
        }

        ImGui::EndCombo();
    }

    // 0 is the automatic scale, then 0.5 to 3.0 in 0.1 steps
    int currentIndex = cfg.FpsScale.has() ? std::clamp((int) std::lround(cfg.FpsScale.value() * 10.0f) - 4, 1, 26) : 0;
    auto scaleText =
        currentIndex == 0 ? StrFmt("Automatic (%.1f)", host.menuResScale) : StrFmt("%.1f", 0.4f + currentIndex / 10.0f);

    if (ImGui::SliderInt("Scale", &currentIndex, 0, 26, scaleText.c_str(), ImGuiSliderFlags_ClampOnInput))
    {
        if (currentIndex == 0)
            cfg.FpsScale.reset();
        else
            cfg.FpsScale = 0.4f + currentIndex / 10.0f;
    }

    ShowTooltip("Size of the FPS overlay, automatic follows the screen's height");

    // Ready made looks, as the text of a settings code: what a preset doesn't list goes back to the default
    if (ImGui::TreeNode("Presets"))
    {
        struct OverlayPreset
        {
            const char* name;
            const char* tip;
            const char* settings;
        };

        static const OverlayPreset presets[] = {
            { "Opti-Like", "OptiScaler's overlay, refined: FPS, frame time graph",
              "OverlaysUseTheme=false\nType=3\nShowLows=true\nShowRatio=true\nShowLatency=true\n"
              "ShowBottleneck=true\nShowUpscalerTime=true\n"
              "RowOrder=fps,frametime,upscale,upscaler,latency,bottleneck\n"
              "Font=seguisb.ttf\nAlpha=0.55\nRounding=6\nLabelColor=0xC8C8C8\nValueColor=0xFFFFFF\n"
              "GraphColor=0x3CD070\nShadow=1\nShadowStrength=0.6\nUpdateInterval=250\n" },
            { "Afterburner-Like", "As MSI Afterburner: hardware rows first, orange labels, outlined text",
              "OverlaysUseTheme=false\nType=3\nShowLows=true\nShowGpu=true\nShowVram=true\nShowCpu=true\n"
              "ShowRam=true\nCompactLabels=true\nRowOrder=gpu,vram,cpu,ram,fps,frametime\n"
              "Font=unispace bd.ttf\nAlpha=0\nRounding=0\nLabelColor=0xFF9C00\nValueColor=0xFFFFFF\n"
              "GraphColor=0xFF9C00\nShadow=2\nShadowStrength=0.5\nShadowColor=0x000000\nUpdateInterval=500\n" },
            { "Latency Lab", "Top center strip: lows, refresh rate",
              "OverlaysUseTheme=false\nPos=4\nHorizontal=true\nType=2\nShowLows=true\nShowPointOneLow=true\n"
              "ShowLatency=true\nLatencyType=2\nShowRefreshRate=true\nShowBottleneck=true\nCompactLabels=true\n"
              "RowOrder=fps,latency,frametime,refresh,bottleneck\nFont=bahnschrift.ttf\nAlpha=0.35\n"
              "BGColor=0x0A0F14\nRounding=10\nLabelColor=0x8A9BA8\nValueColor=0x00E5FF\nGraphColor=0x00E5FF\n"
              "LabelScale=0.85\nValueScale=1.1\nLetterSpacing=0.5\nShadow=1\nShadowStrength=0.5\n"
              "UpdateInterval=250\n" },
        };

        static std::string presetStatus;
        const float presetCount = static_cast<float>(std::size(presets));
        const float buttonWidth =
            (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * (presetCount - 1.0f)) / presetCount;

        for (size_t i = 0; i < std::size(presets); i++)
        {
            if (i > 0)
                ImGui::SameLine();

            if (ImGui::Button(presets[i].name, ImVec2(buttonWidth, 0.0f)))
            {
                if (cfg.ApplyPreset(presets[i].settings))
                    presetStatus = StrFmt("%s applied, Save Settings to keep it", presets[i].name);
            }

            ShowTooltip(presets[i].tip);
        }

        StatusText(presetStatus.empty()
                       ? "A preset replaces the overlay's look and rows, Copy Code in Share Settings keeps yours."
                       : presetStatus);

        ImGui::TreePop();
    }

    // Which rows are shown and their order, from top to bottom (left to right when horizontal)
    if (ImGui::TreeNode("Rows"))
    {
        const auto overlayType = cfg.FpsOverlayType.get();

        // Rows the overlay type decides on show their state, greyed out
        auto fixedCheckbox = [&](const char* label, bool shown, const char* tip)
        {
            ImGui::BeginDisabled();
            ImGui::Checkbox(label, &shown);
            ImGui::EndDisabled();

            ShowTooltip(tip);
        };

        auto order = OverlayRender::RowOrder();
        bool orderChanged = false;

        if (ImGui::BeginTable("overlayRows", 3, ImGuiTableFlags_SizingFixedFit))
        {
            ImGui::TableSetupColumn("Order");
            ImGui::TableSetupColumn("Row", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("From");

            ImGui::TableNextRow();
            for (int i = 0; i < 3; i++)
            {
                ImGui::TableSetColumnIndex(i);
                ImGui::TextDisabled("%s", ImGui::TableGetColumnName(i));
            }

            ShowTooltip("The lowest Overlay Type that shows the row");

            // Drawn as they were at the frame's start, a click moves one row
            const auto shownOrder = order;

            for (size_t i = 0; i < shownOrder.size(); i++)
            {
                const int row = shownOrder[i];
                ImGui::PushID(overlayRowKeys[row]);

                ImGui::TableNextRow();
                ImGui::TableNextColumn();

                ImGui::BeginDisabled(i == 0);
                if (ImGui::ArrowButton("##Up", ImGuiDir_Up))
                {
                    std::swap(order[i], order[i - 1]);
                    orderChanged = true;
                }
                ImGui::EndDisabled();

                ImGui::SameLine(0.0f, 2.0f);

                ImGui::BeginDisabled(i + 1 == order.size());
                if (ImGui::ArrowButton("##Down", ImGuiDir_Down))
                {
                    std::swap(order[i], order[i + 1]);
                    orderChanged = true;
                }
                ImGui::EndDisabled();

                ImGui::TableNextColumn();

                const char* name = overlayRowNames[row];
                const char* from = "Simple";

                switch (row)
                {
                case OverlayRow_Fps:
                    fixedCheckbox(name, true, "Always shown");
                    from = "Just FPS";
                    break;

                case OverlayRow_FrameTime:
                {
                    bool showFrameTime = cfg.FpsOverlayShowFrameTime.value_or(overlayType >= FpsOverlay_Detailed);
                    if (ImGui::Checkbox(name, &showFrameTime))
                        cfg.FpsOverlayShowFrameTime = showFrameTime;

                    ShowTooltip("Frame time and its average, by default from Detailed");
                    from = cfg.FpsOverlayShowFrameTime.has() ? (cfg.FpsOverlayShowFrameTime.value() ? "Simple" : "Off")
                                                             : "Detailed";
                    break;
                }

                case OverlayRow_Refresh:
                    OptionCheckbox(name, cfg.FpsOverlayShowRefreshRate,
                                   "The display's actual refresh rate over the last 0.1 s, and its average.\n"
                                   "With VRR it follows the frames shown.");
                    break;

                case OverlayRow_Vram:
                    OptionCheckbox(name, cfg.FpsOverlayShowVram,
                                   "VRAM the game uses and its budget, above it the game stutters.\n"
                                   "Plus the memory clock when the GPU reports it.");
                    break;

                case OverlayRow_Gpu:
                    OptionCheckbox(name, cfg.FpsOverlayShowGpu, "GPU temperature, power, voltage and clock");
                    break;

                case OverlayRow_Cpu:
                    OptionCheckbox(name, cfg.FpsOverlayShowCpu,
                                   "CPU temperature, power, voltage and clock.\n"
                                   "Some need HWiNFO, Afterburner or Core Temp running.");
                    break;

                case OverlayRow_Ram:
                    OptionCheckbox(name, cfg.FpsOverlayShowRam, "System RAM in use and total");
                    break;

                default:
                    // Needs OptiScaler's own state
                    fixedCheckbox(name, false, "Only in OptiScaler's own overlay, the plugin can't measure it");
                    from = "OptiScaler";
                    break;
                }

                ImGui::TableNextColumn();
                ImGui::TextDisabled("%s", from);

                ImGui::PopID();
            }

            ImGui::EndTable();
        }

        if (orderChanged)
            OverlayRender::SetRowOrder(order);

        ImGui::Spacing();

        // -1 is the default spacing
        float rowSpacing = cfg.FpsOverlayRowSpacing.value_or(-1.0f);
        if (ImGui::SliderFloat("Row Spacing", &rowSpacing, -1.0f, 32.0f,
                               cfg.FpsOverlayRowSpacing.has() ? "%.0f px" : "Default", ImGuiSliderFlags_AlwaysClamp))
        {
            if (rowSpacing < -0.5f)
                cfg.FpsOverlayRowSpacing.reset();
            else
                cfg.FpsOverlayRowSpacing = std::round(std::max(rowSpacing, 0.0f));
        }

        if (ImGui::Button("Reset Order & Spacing"))
        {
            cfg.FpsOverlayRowOrder.reset();
            cfg.FpsOverlayRowSpacing.reset();
            cfg.FpsOverlayShowFrameTime.reset();
        }

        ImGui::TreePop();
    }

    // What the rows add
    if (ImGui::TreeNode("Metric Options"))
    {
        OptionCheckbox("Avg & 1% Low", cfg.FpsOverlayShowLows,
                       "Adds the average FPS and the 1% low FPS of the last 1000 frames to the FPS row");

        OptionCheckbox("0.1% Low", cfg.FpsOverlayShowPointOneLow,
                       "Adds the 0.1% low FPS of the last 5000 frames to the FPS row, for the rare stutters");

        OptionCheckbox("Game RAM", cfg.FpsOverlayShowGameRam,
                       "Adds the game process's physical RAM usage (working set) to the RAM row.");

        OptionCheckbox("Commit Charge", cfg.FpsOverlayShowCommitRam,
                       "Adds the total system commit charge (RAM + pagefile) to the RAM row.");

        OptionCheckbox("Hardware Names", cfg.FpsOverlayShowHardwareNames, "GPU and CPU model names instead of GPU/CPU");

        ImGui::TreePop();
    }

    if (ImGui::TreeNode("Appearance"))
    {
        ImGui::SeparatorText("Layout");

        OptionCheckbox("Compact Labels", cfg.FpsOverlayCompactLabels, "Shorter labels for a narrower overlay");

        float fpsAlpha = cfg.FpsOverlayAlpha.get();
        if (ImGui::SliderFloat("Background Alpha", &fpsAlpha, 0.0f, 1.0f, "%.2f"))
            cfg.FpsOverlayAlpha = fpsAlpha;

        float graphHeight = cfg.FpsOverlayGraphHeight.get();
        if (ImGui::SliderFloat("Graph Height", &graphHeight, 0.5f, 3.0f, "%.1fx", ImGuiSliderFlags_AlwaysClamp))
            cfg.FpsOverlayGraphHeight = graphHeight;

        // -1 is the default rounding
        float rounding = cfg.FpsOverlayRounding.value_or(-1.0f);
        const char* roundingFormat = !cfg.FpsOverlayRounding.has() ? "Default" : rounding < 0.5f ? "Square" : "%.0f px";
        if (ImGui::SliderFloat("Corner Rounding", &rounding, -1.0f, 16.0f, roundingFormat,
                               ImGuiSliderFlags_AlwaysClamp))
        {
            if (rounding < -0.5f)
                cfg.FpsOverlayRounding.reset();
            else
                cfg.FpsOverlayRounding = std::max(rounding, 0.0f);
        }

        ImGui::SeparatorText("Text");

        // Font, from the Windows fonts folder (checked once to avoid per-frame filesystem calls)
        static const std::vector<bool> overlayFontExists = []()
        {
            std::vector<bool> exists;
            exists.reserve(overlayFontCount);
            for (size_t i = 0; i < overlayFontCount; i++)
                exists.push_back(std::filesystem::exists(OverlayRender::FontPath(overlayFonts[i].file)));
            return exists;
        }();

        const auto fontName = cfg.FpsOverlayFont.value_or("");
        std::string selectedFont = fontName.empty() ? "Default font" : fontName;

        for (size_t i = 0; i < overlayFontCount; i++)
        {
            if (_stricmp(overlayFonts[i].file, fontName.c_str()) == 0)
                selectedFont = overlayFonts[i].name;
        }

        if (ImGui::BeginCombo("Font", selectedFont.c_str()))
        {
            if (ImGui::Selectable("Default font", fontName.empty()))
                cfg.FpsOverlayFont.reset();

            for (size_t i = 0; i < overlayFontCount; ++i)
            {
                if (!overlayFontExists[i])
                    continue;

                if (ImGui::Selectable(overlayFonts[i].name, _stricmp(overlayFonts[i].file, fontName.c_str()) == 0))
                    cfg.FpsOverlayFont = std::string(overlayFonts[i].file);
            }

            ImGui::EndCombo();
        }
        ShowTooltip("Choose a Windows system font or enter any custom .ttf/.otf file below");

        char customFontBuf[260] {};
        std::snprintf(customFontBuf, sizeof(customFontBuf), "%s", fontName.c_str());
        if (ImGui::InputText("Custom Font File", customFontBuf, sizeof(customFontBuf),
                             ImGuiInputTextFlags_EnterReturnsTrue))
        {
            std::string entered(customFontBuf);
            if (entered.empty())
                cfg.FpsOverlayFont.reset();
            else
                cfg.FpsOverlayFont = entered;
        }
        ShowTooltip("A file in C:\\Windows\\Fonts or a full path, Enter applies it");

        // 7 is the default font size
        float fontSizeValue = cfg.FpsOverlayFontSize.value_or(7.0f);
        if (ImGui::SliderFloat("Font Size", &fontSizeValue, 7.0f, 48.0f,
                               cfg.FpsOverlayFontSize.has() ? "%.0f px" : "Default font size",
                               ImGuiSliderFlags_AlwaysClamp))
        {
            if (fontSizeValue < 8.0f)
                cfg.FpsOverlayFontSize.reset();
            else
                cfg.FpsOverlayFontSize = std::round(fontSizeValue);
        }
        ShowTooltip("Before the overlay's scale, the far left is the default font size");

        float letterSpacing = cfg.FpsOverlayLetterSpacing.get();
        if (ImGui::SliderFloat("Letter Spacing", &letterSpacing, -2.0f, 8.0f, "%.1f px", ImGuiSliderFlags_AlwaysClamp))
            cfg.FpsOverlayLetterSpacing = letterSpacing;
        ShowTooltip("Extra horizontal tracking in pixels between characters");

        ScaleSlider("Label Size", cfg.FpsOverlayLabelScale);
        ShowTooltip("Metric names like FPS: and Avg:");

        ScaleSlider("Value Size", cfg.FpsOverlayValueScale);
        ShowTooltip("The numbers");

        float updateInterval = cfg.FpsOverlayUpdateInterval.get();
        if (ImGui::SliderFloat("Update Interval", &updateInterval, 0.0f, 1000.0f,
                               updateInterval < 1.0f ? "Every frame" : "%.0f ms", ImGuiSliderFlags_AlwaysClamp))
        {
            cfg.FpsOverlayUpdateInterval = std::round(updateInterval / 10.0f) * 10.0f;
        }
        ShowTooltip("How often FPS and times change, averaged in between. Higher is easier to read.");

        const char* shadowTypes[] = { "Off", "Shadow", "Outline" };
        int shadowType = (int) cfg.FpsOverlayShadowType.get();
        if (ImGui::Combo("Text Shadow", &shadowType, shadowTypes, IM_ARRAYSIZE(shadowTypes)))
            cfg.FpsOverlayShadowType = (uint32_t) shadowType;

        ImGui::BeginDisabled(shadowType == FpsOverlayShadow_Off);
        float shadowStrength = cfg.FpsOverlayShadowStrength.get();
        if (ImGui::SliderFloat("Shadow Strength", &shadowStrength, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp))
            cfg.FpsOverlayShadowStrength = shadowStrength;
        ShowTooltip("Thickness and darkness of the shadow, thickness of the outline");
        ImGui::EndDisabled();

        ImGui::SeparatorText("Colors");

        bool useTheme = cfg.OverlaysUseTheme.get();
        if (ImGui::Checkbox("Use Theme Colors", &useTheme))
            cfg.OverlaysUseTheme = useTheme;

        // Two by two, the theme's when it's used
        ImGui::BeginDisabled(useTheme);
        ColorEdit("Labels", cfg.FpsOverlayLabelColor);
        ImGui::SameLine(0.0f, 16.0f);
        ColorEdit("Values", cfg.FpsOverlayValueColor);
        ColorEdit("Graph", cfg.FpsOverlayGraphColor);
        ImGui::SameLine(0.0f, 16.0f);
        ColorEdit("Background", cfg.FpsOverlayBGColor);
        ImGui::EndDisabled();

        ImGui::BeginDisabled(shadowType == FpsOverlayShadow_Off);
        ColorEdit("Shadow", cfg.FpsOverlayShadowColor);
        ImGui::EndDisabled();

        ImGui::Spacing();

        if (ImGui::Button("Reset Appearance"))
            cfg.ResetAppearance();

        ImGui::TreePop();
    }

    // The overlay's settings as a code, kept outside the ini across updates and shared with others
    if (ImGui::TreeNode("Share Settings"))
    {
        static std::string settingsCodeStatus;

        if (ImGui::Button("Copy Code"))
        {
            ImGui::SetClipboardText(cfg.ExportCode().c_str());
            settingsCodeStatus = "Copied to the clipboard";
        }

        ShowTooltip("Copies your overlay settings as a code to keep or share, OptiScaler reads it too");

        ImGui::SameLine();

        if (ImGui::Button("Paste Code"))
        {
            const char* clipboard = ImGui::GetClipboardText();

            if (clipboard != nullptr && cfg.ImportCode(clipboard))
                settingsCodeStatus = "Applied, Save Settings to keep it";
            else
                settingsCodeStatus = "No FPS overlay code in the clipboard";
        }

        ShowTooltip("Applies a copied code. Settings it doesn't list go back to their defaults.");

        if (!settingsCodeStatus.empty())
            StatusText(settingsCodeStatus);

        ImGui::TreePop();
    }

    ImGui::End();
    ImGui::PopFontSize();
}
} // namespace OverlaySettingsUi

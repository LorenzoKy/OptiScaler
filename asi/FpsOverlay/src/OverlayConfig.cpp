#include "pch.h"
#include "OverlayConfig.h"

#include <cctype>
#include <cstdlib>

static std::wstring Widen(const std::string& text)
{
    if (text.empty())
        return {};

    const int size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);

    if (size <= 1)
        return {};

    std::wstring out(size - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, out.data(), size);
    return out;
}

static std::string Narrow(const std::wstring& text)
{
    if (text.empty())
        return {};

    const int size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, nullptr, 0, nullptr, nullptr);

    if (size <= 1)
        return {};

    std::string out(size - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, out.data(), size, nullptr, nullptr);
    return out;
}

static std::string Trim(std::string text)
{
    while (!text.empty() && std::isspace((unsigned char) text.back()))
        text.pop_back();

    size_t start = 0;
    while (start < text.size() && std::isspace((unsigned char) text[start]))
        start++;

    return text.substr(start);
}

OverlayConfig& OverlayConfig::Get()
{
    static OverlayConfig instance;
    return instance;
}

OverlayConfig::OverlayConfig()
{
    // The order is the settings code's, as OptiScaler's overlayIniKeys
    auto add = [&](const char* key, SettingKind kind, void* opt, float lo, float hi, uint32_t count, bool shared)
    { settings_.push_back({ key, kind, opt, lo, hi, count, shared }); };

    auto flag = [&](const char* key, Opt<bool>& opt) { add(key, SettingKind::Bool, &opt, 0, 0, 0, true); };
    auto index = [&](const char* key, Opt<uint32_t>& opt, uint32_t count)
    { add(key, SettingKind::Index, &opt, 0, 0, count, true); };
    auto number = [&](const char* key, Opt<float>& opt, float lo, float hi)
    { add(key, SettingKind::Float, &opt, lo, hi, 0, true); };
    auto color = [&](const char* key, Opt<uint32_t>& opt) { add(key, SettingKind::Color, &opt, 0, 0, 0, true); };
    auto text = [&](const char* key, Opt<std::string>& opt) { add(key, SettingKind::String, &opt, 0, 0, 0, true); };

    flag("ShowFps", ShowFps);
    flag("OverlaysUseTheme", OverlaysUseTheme);
    index("FpsOverlayPos", FpsOverlayPosition, FpsOverlayPos_COUNT);
    index("FpsOverlayType", FpsOverlayType, FpsOverlay_COUNT);
    flag("FpsOverlayHorizontal", FpsOverlayHorizontal);
    flag("FpsOverlayShowRatio", FpsOverlayShowRatio);
    flag("FpsOverlayShowLows", FpsOverlayShowLows);
    flag("FpsOverlayShowLatency", FpsOverlayShowLatency);
    index("FpsOverlayLatencyType", FpsOverlayLatencyType, FpsOverlayLatency_COUNT);
    flag("FpsOverlayShowFrameTime", FpsOverlayShowFrameTime);
    flag("FpsOverlayShowUpscalerTime", FpsOverlayShowUpscalerTime);
    flag("FpsOverlayShowFpsCost", FpsOverlayShowFpsCost);
    flag("FpsOverlayShowPointOneLow", FpsOverlayShowPointOneLow);
    flag("FpsOverlayShowBottleneck", FpsOverlayShowBottleneck);
    flag("FpsOverlayShowVram", FpsOverlayShowVram);
    flag("FpsOverlayShowGpu", FpsOverlayShowGpu);
    flag("FpsOverlayShowCpu", FpsOverlayShowCpu);
    flag("FpsOverlayShowRefreshRate", FpsOverlayShowRefreshRate);
    flag("FpsOverlayShowRam", FpsOverlayShowRam);
    flag("FpsOverlayShowGameRam", FpsOverlayShowGameRam);
    flag("FpsOverlayShowCommitRam", FpsOverlayShowCommitRam);
    flag("FpsOverlayShowHardwareNames", FpsOverlayShowHardwareNames);
    flag("FpsOverlayCompactLabels", FpsOverlayCompactLabels);
    text("FpsOverlayRowOrder", FpsOverlayRowOrder);
    number("FpsOverlayRowSpacing", FpsOverlayRowSpacing, 0.0f, 32.0f);
    number("FpsOverlayUpdateInterval", FpsOverlayUpdateInterval, 0.0f, 1000.0f);
    number("FpsOverlayAlpha", FpsOverlayAlpha, 0.0f, 1.0f);
    number("FpsScale", FpsScale, 0.5f, 3.0f);
    number("FpsOverlayGraphHeight", FpsOverlayGraphHeight, 0.5f, 3.0f);
    number("FpsOverlayPosX", FpsOverlayPosX, 0.0f, 1.0f);
    number("FpsOverlayPosY", FpsOverlayPosY, 0.0f, 1.0f);
    text("FpsOverlayFont", FpsOverlayFont);
    number("FpsOverlayFontSize", FpsOverlayFontSize, 8.0f, 48.0f);
    number("FpsOverlayLetterSpacing", FpsOverlayLetterSpacing, -2.0f, 8.0f);
    number("FpsOverlayLabelScale", FpsOverlayLabelScale, 0.5f, 2.0f);
    number("FpsOverlayValueScale", FpsOverlayValueScale, 0.5f, 2.0f);
    color("FpsOverlayLabelColor", FpsOverlayLabelColor);
    color("FpsOverlayValueColor", FpsOverlayValueColor);
    color("FpsOverlayGraphColor", FpsOverlayGraphColor);
    color("FpsOverlayBGColor", FpsOverlayBGColor);
    index("FpsOverlayShadow", FpsOverlayShadowType, FpsOverlayShadow_COUNT);
    number("FpsOverlayShadowStrength", FpsOverlayShadowStrength, 0.0f, 1.0f);
    color("FpsOverlayShadowColor", FpsOverlayShadowColor);
    number("FpsOverlayRounding", FpsOverlayRounding, 0.0f, 16.0f);

    // Not in the settings code
    add("ToggleKey", SettingKind::Index, &ToggleKey, 0, 0, 256, false);
    add("SettingsKey", SettingKind::Index, &SettingsKey, 0, 0, 256, false);
}

std::string OverlayConfig::ToText(const Setting& setting) const
{
    switch (setting.kind)
    {
    case SettingKind::Bool:
    {
        auto& opt = *static_cast<Opt<bool>*>(setting.opt);
        return opt.has() ? (opt.value() ? "true" : "false") : "auto";
    }

    case SettingKind::Float:
    {
        auto& opt = *static_cast<Opt<float>*>(setting.opt);
        return opt.has() ? std::to_string(opt.value()) : "auto";
    }

    case SettingKind::Color:
    {
        auto& opt = *static_cast<Opt<uint32_t>*>(setting.opt);
        return opt.has() ? std::format("{:#x}", opt.value()) : "auto";
    }

    case SettingKind::Index:
    {
        auto& opt = *static_cast<Opt<uint32_t>*>(setting.opt);
        return opt.has() ? std::to_string(opt.value()) : "auto";
    }

    case SettingKind::String:
    {
        auto& opt = *static_cast<Opt<std::string>*>(setting.opt);
        return opt.has() && !opt.value().empty() ? opt.value() : "auto";
    }
    }

    return "auto";
}

void OverlayConfig::FromText(const Setting& setting, const std::string& rawText)
{
    const std::string text = Trim(rawText);
    const bool isAuto = text.empty() || _stricmp(text.c_str(), "auto") == 0;

    switch (setting.kind)
    {
    case SettingKind::Bool:
    {
        auto& opt = *static_cast<Opt<bool>*>(setting.opt);

        if (isAuto)
            opt.reset();
        else if (_stricmp(text.c_str(), "true") == 0 || text == "1" || _stricmp(text.c_str(), "yes") == 0 ||
                 _stricmp(text.c_str(), "on") == 0)
            opt = true;
        else if (_stricmp(text.c_str(), "false") == 0 || text == "0" || _stricmp(text.c_str(), "no") == 0 ||
                 _stricmp(text.c_str(), "off") == 0)
            opt = false;
        else
            opt.reset();

        break;
    }

    case SettingKind::Float:
    {
        auto& opt = *static_cast<Opt<float>*>(setting.opt);
        char* end = nullptr;
        const double parsed = isAuto ? 0.0 : std::strtod(text.c_str(), &end);

        if (isAuto || end == text.c_str())
            opt.reset();
        else
            opt = std::clamp(static_cast<float>(parsed), setting.lo, setting.hi);

        break;
    }

    case SettingKind::Color:
    {
        auto& opt = *static_cast<Opt<uint32_t>*>(setting.opt);
        char* end = nullptr;
        const unsigned long parsed = isAuto ? 0 : std::strtoul(text.c_str(), &end, 0);

        if (isAuto || end == text.c_str())
            opt.reset();
        else
            opt = static_cast<uint32_t>(parsed & 0xFFFFFF);

        break;
    }

    case SettingKind::Index:
    {
        auto& opt = *static_cast<Opt<uint32_t>*>(setting.opt);
        char* end = nullptr;
        const unsigned long parsed = isAuto ? 0 : std::strtoul(text.c_str(), &end, 0);

        if (isAuto || end == text.c_str())
            opt.reset();
        else
            opt = std::min(static_cast<uint32_t>(parsed), setting.count - 1);

        break;
    }

    case SettingKind::String:
    {
        auto& opt = *static_cast<Opt<std::string>*>(setting.opt);

        if (isAuto)
            opt.reset();
        else
            opt = text;

        break;
    }
    }
}

uint64_t OverlayConfig::FileTime() const
{
    WIN32_FILE_ATTRIBUTE_DATA data {};

    if (iniPath_.empty() || !GetFileAttributesExW(iniPath_.c_str(), GetFileExInfoStandard, &data))
        return 0;

    return (static_cast<uint64_t>(data.ftLastWriteTime.dwHighDateTime) << 32) | data.ftLastWriteTime.dwLowDateTime;
}

void OverlayConfig::Init(const std::wstring& folder)
{
    iniPath_ = (std::filesystem::path(folder) / L"OptiFpsOverlay.ini").wstring();
    Load();
}

void OverlayConfig::Load()
{
    wchar_t buffer[1024] {};

    for (const auto& setting : settings_)
    {
        const std::wstring key = Widen(setting.key);
        GetPrivateProfileStringW(L"Overlay", key.c_str(), L"auto", buffer, (DWORD) std::size(buffer), iniPath_.c_str());
        FromText(setting, Narrow(buffer));
    }

    lastWrite_ = FileTime();
}

bool OverlayConfig::Save()
{
    bool ok = true;

    for (const auto& setting : settings_)
    {
        const std::wstring key = Widen(setting.key);
        const std::wstring value = Widen(ToText(setting));
        ok = WritePrivateProfileStringW(L"Overlay", key.c_str(), value.c_str(), iniPath_.c_str()) && ok;
    }

    lastWrite_ = FileTime();
    return ok;
}

bool OverlayConfig::PollReload()
{
    const uint64_t now = GetTickCount64();

    if (now - lastPoll_ < 1000)
        return false;

    lastPoll_ = now;

    const uint64_t time = FileTime();

    if (time == 0 || time == lastWrite_)
        return false;

    Load();
    LOG_INFO("OptiFpsOverlay.ini changed, settings read again");
    return true;
}

// The code: a versioned prefix and the changed keys as base64 text, the keys without the FpsOverlay prefix
static constexpr std::string_view overlayCodePrefix = "OptiOverlay1:";
static constexpr std::string_view overlayKeyPrefix = "FpsOverlay";

static std::string_view OverlayCodeKey(std::string_view key)
{
    if (key.starts_with(overlayKeyPrefix))
        key.remove_prefix(overlayKeyPrefix.size());

    return key;
}

static std::string Base64Encode(std::string_view data)
{
    static constexpr char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    std::string out;
    uint32_t bits = 0;
    int count = 0;

    for (unsigned char c : data)
    {
        bits = (bits << 8) | c;
        count += 8;

        while (count >= 6)
        {
            count -= 6;
            out += table[(bits >> count) & 0x3F];
        }
    }

    if (count > 0)
        out += table[(bits << (6 - count)) & 0x3F];

    while (out.size() % 4 != 0)
        out += '=';

    return out;
}

static std::optional<std::string> Base64Decode(std::string_view data)
{
    std::string out;
    uint32_t bits = 0;
    int count = 0;

    for (char c : data)
    {
        int value;

        if (c >= 'A' && c <= 'Z')
            value = c - 'A';
        else if (c >= 'a' && c <= 'z')
            value = c - 'a' + 26;
        else if (c >= '0' && c <= '9')
            value = c - '0' + 52;
        else if (c == '+')
            value = 62;
        else if (c == '/')
            value = 63;
        else if (c == '=')
            break;
        else
            return std::nullopt;

        bits = (bits << 6) | value;
        count += 6;

        if (count >= 8)
        {
            count -= 8;
            out += (char) ((bits >> count) & 0xFF);
        }
    }

    return out;
}

std::string OverlayConfig::ExportCode()
{
    std::string text;

    for (const auto& setting : settings_)
    {
        if (!setting.shared)
            continue;

        const std::string value = ToText(setting);

        if (value == "auto")
            continue;

        text += std::format("{}={}\n", OverlayCodeKey(setting.key), value);
    }

    return std::string(overlayCodePrefix) + Base64Encode(text);
}

bool OverlayConfig::ImportCode(const std::string& code)
{
    std::string_view view = code;

    while (!view.empty() && std::isspace((unsigned char) view.front()))
        view.remove_prefix(1);

    while (!view.empty() && std::isspace((unsigned char) view.back()))
        view.remove_suffix(1);

    if (!view.starts_with(overlayCodePrefix))
        return false;

    view.remove_prefix(overlayCodePrefix.size());

    auto text = Base64Decode(view);

    if (!text.has_value())
        return false;

    return ApplyText(text.value());
}

bool OverlayConfig::ApplyPreset(std::string_view text)
{
    const auto showFps = ShowFps;

    if (!ApplyText(std::string(text)))
        return false;

    ShowFps = showFps.get();
    return true;
}

bool OverlayConfig::ApplyText(const std::string& text)
{
    // Known keys only
    std::vector<std::pair<const Setting*, std::string>> values;
    std::istringstream lines(text);
    std::string line;

    while (std::getline(lines, line))
    {
        const auto separator = line.find('=');

        if (separator == std::string::npos)
            continue;

        const std::string_view name = std::string_view(line).substr(0, separator);
        std::string value = line.substr(separator + 1);

        // A font by its name in the Windows fonts folder, not a path on the computer it was copied from
        if (name == OverlayCodeKey("FpsOverlayFont") && value.find_first_of("\\/:") != std::string::npos)
            continue;

        for (const auto& setting : settings_)
        {
            if (setting.shared && OverlayCodeKey(setting.key) == name)
            {
                values.emplace_back(&setting, std::move(value));
                break;
            }
        }
    }

    // Nothing known in a code that isn't empty, a code of a later version or a broken one
    if (values.empty() && !text.empty())
        return false;

    // Everything it doesn't list back to the default
    for (const auto& setting : settings_)
    {
        if (setting.shared)
            FromText(setting, "auto");
    }

    for (const auto& [setting, value] : values)
        FromText(*setting, value);

    LOG_INFO("Imported {} FPS overlay settings", values.size());
    return true;
}

void OverlayConfig::ResetAppearance()
{
    FpsOverlayCompactLabels.reset();
    FpsOverlayFont.reset();
    FpsOverlayFontSize.reset();
    FpsOverlayLetterSpacing.reset();
    FpsOverlayUpdateInterval.reset();
    FpsOverlayLabelColor.reset();
    FpsOverlayValueColor.reset();
    FpsOverlayLabelScale.reset();
    FpsOverlayValueScale.reset();
    FpsOverlayGraphColor.reset();
    FpsOverlayBGColor.reset();
    FpsOverlayShadowType.reset();
    FpsOverlayShadowStrength.reset();
    FpsOverlayShadowColor.reset();
    FpsOverlayGraphHeight.reset();
    FpsOverlayRounding.reset();
    FpsOverlayAlpha.reset();
}

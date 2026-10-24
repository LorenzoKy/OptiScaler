#pragma once

#include <format>
#include <string>

// OptiFpsOverlay.log next to the plugin
namespace AsiLog
{
void Open(const std::wstring& folder);
void Write(const char* level, const std::string& message);
} // namespace AsiLog

#define LOG_INFO(...) AsiLog::Write("INFO", std::format(__VA_ARGS__))
#define LOG_WARN(...) AsiLog::Write("WARN", std::format(__VA_ARGS__))
#define LOG_ERROR(...) AsiLog::Write("ERROR", std::format(__VA_ARGS__))
#define LOG_DEBUG(...) ((void) 0)
#define LOG_TRACE(...) ((void) 0)

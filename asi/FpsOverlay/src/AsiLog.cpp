#include "pch.h"

#include <cstdio>

namespace AsiLog
{
static std::mutex mutex;
static FILE* file = nullptr;

void Open(const std::wstring& folder)
{
    std::lock_guard lock(mutex);

    if (file != nullptr)
        return;

    const auto path = std::filesystem::path(folder) / L"OptiFpsOverlay.log";
    file = _wfsopen(path.c_str(), L"w", _SH_DENYNO);
}

void Write(const char* level, const std::string& message)
{
    std::lock_guard lock(mutex);

    if (file == nullptr)
        return;

    SYSTEMTIME time {};
    GetLocalTime(&time);

    fprintf(file, "[%02d:%02d:%02d.%03d] [%s] %s\n", time.wHour, time.wMinute, time.wSecond, time.wMilliseconds, level,
            message.c_str());
    fflush(file);
}
} // namespace AsiLog

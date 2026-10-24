#pragma once

#include "pch.h"

namespace VendorId
{
enum Value : uint32_t
{
    Invalid = 0,
    AMD = 0x1002,
    Nvidia = 0x10DE,
    Intel = 0x8086,
};
}

// The fields of OptiScaler's GpuInformation that the telemetry uses
struct GpuInformation
{
    LUID luid {};
    std::string name {};
    VendorId::Value vendorId = VendorId::Invalid;
    uint32_t deviceId = 0x0;
    uint32_t subsystemId = 0x0;
    uint32_t revisionId = 0x0;
    size_t dedicatedVramInBytes = 0;
    bool softwareAdapter = false;
};

inline constexpr bool IsEqualLUID(LUID luid1, LUID luid2)
{
    return luid1.HighPart == luid2.HighPart && luid1.LowPart == luid2.LowPart;
}

// The adapters from DXGI, the one with the most dedicated memory first
class IdentifyGpu
{
  public:
    static std::vector<GpuInformation> getAllGpus()
    {
        static std::mutex mutex;
        static std::vector<GpuInformation> cache;
        static bool queried = false;

        std::lock_guard lock(mutex);

        if (!queried)
        {
            queried = true;

            IDXGIFactory1* factory = nullptr;

            if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))) && factory != nullptr)
            {
                for (UINT i = 0;; i++)
                {
                    IDXGIAdapter1* adapter = nullptr;

                    if (factory->EnumAdapters1(i, &adapter) == DXGI_ERROR_NOT_FOUND)
                        break;

                    DXGI_ADAPTER_DESC1 desc {};

                    if (SUCCEEDED(adapter->GetDesc1(&desc)))
                    {
                        GpuInformation info {};
                        info.luid = desc.AdapterLuid;
                        info.vendorId = static_cast<VendorId::Value>(desc.VendorId);
                        info.deviceId = desc.DeviceId;
                        info.subsystemId = desc.SubSysId;
                        info.revisionId = desc.Revision;
                        info.dedicatedVramInBytes = desc.DedicatedVideoMemory;
                        info.softwareAdapter = (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0;

                        const int size =
                            WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, nullptr, 0, nullptr, nullptr);

                        if (size > 1)
                        {
                            info.name.resize(size - 1);
                            WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, info.name.data(), size, nullptr,
                                                nullptr);
                        }

                        cache.push_back(std::move(info));
                    }

                    adapter->Release();
                }

                factory->Release();
            }

            std::stable_sort(cache.begin(), cache.end(),
                             [](const GpuInformation& a, const GpuInformation& b)
                             {
                                 if (a.softwareAdapter != b.softwareAdapter)
                                     return !a.softwareAdapter;

                                 return a.dedicatedVramInBytes > b.dedicatedVramInBytes;
                             });
        }

        return cache;
    }

    static GpuInformation getPrimaryGpu()
    {
        auto gpus = getAllGpus();
        return gpus.empty() ? GpuInformation {} : gpus.front();
    }
};

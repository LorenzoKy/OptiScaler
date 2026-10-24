#pragma once

#include "pch.h"

// The few fields of OptiScaler's State that the telemetry reads: the plugin sets them from the game's swapchain
class State
{
  public:
    static State& Instance()
    {
        static State instance;
        return instance;
    }

    ID3D12Device* currentD3D12Device = nullptr;
    ID3D11Device* currentD3D11Device = nullptr;
};

// OptiScaler's guards for its own helper threads, nothing to guard here
struct ScopedSkipSpoofingThread
{
};

struct ScopedCreatingD3DDevice
{
};

struct ScopedSkipDxgiLoadChecks
{
};

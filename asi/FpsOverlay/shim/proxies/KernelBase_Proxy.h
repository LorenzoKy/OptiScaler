#pragma once

#include "pch.h"

// OptiScaler calls kernelbase around its own hooks, the plugin has none to go around
class KernelBaseProxy
{
  public:
    static decltype(&GetModuleHandleW) GetModuleHandleW_() { return &::GetModuleHandleW; }
    static decltype(&GetProcAddress) GetProcAddress_() { return &::GetProcAddress; }
};

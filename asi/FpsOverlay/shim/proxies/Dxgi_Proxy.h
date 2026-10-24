#pragma once

#include "pch.h"

class DxgiProxy
{
  public:
    typedef HRESULT(WINAPI* PFN_CreateDxgiFactory)(REFIID riid, IDXGIFactory** ppFactory);

    static void Init(HMODULE = nullptr) {}

    static PFN_CreateDxgiFactory CreateDxgiFactory_()
    {
        return reinterpret_cast<PFN_CreateDxgiFactory>(&::CreateDXGIFactory);
    }
};

#pragma once

#include "pch.h"

class NtdllProxy
{
  public:
    static HMODULE LoadLibraryExW_Ldr(LPCWSTR lpLibFileName, HANDLE hFile, DWORD dwFlags)
    {
        return ::LoadLibraryExW(lpLibFileName, hFile, dwFlags);
    }
};

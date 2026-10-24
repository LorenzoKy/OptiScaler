#pragma once

#include "pch.h"

#include <nvapi_interface.h>
#include <unordered_map>

#define GET_ID(name) NvApiTypes::Instance().getId(#name)
#define GET_INTERFACE(name, queryInterface) reinterpret_cast<decltype(&name)>(queryInterface(GET_ID(name)))

typedef void*(__stdcall* PFN_NvApi_QueryInterface)(unsigned int InterfaceId);

// The ids of NVAPI's functions by name, from NVIDIA's interface table (OptiScaler has the same class)
class NvApiTypes
{
    std::unordered_map<std::string, unsigned int> lookupTable;

    NvApiTypes()
    {
        for (const auto& entry : nvapi_interface_table)
            lookupTable[entry.func] = entry.id;
    }

  public:
    static NvApiTypes& Instance()
    {
        static NvApiTypes instance;
        return instance;
    }

    unsigned int getId(const std::string& name) const
    {
        auto it = lookupTable.find(name);
        return it != lookupTable.end() ? it->second : 0;
    }
};

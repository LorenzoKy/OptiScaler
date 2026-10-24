#include "pch.h"
#include "FGGpuTime_Dx12.h"

#include <misc/DeferredRelease.h>

#include <include/d3dx/d3dx12.h>
#include <detours/detours.h>

namespace
{
typedef void(STDMETHODCALLTYPE* PFN_ExecuteCommandLists)(ID3D12CommandQueue* This, UINT NumCommandLists,
                                                         ID3D12CommandList* const* ppCommandLists);

// ID3D12CommandQueue vtable index of ExecuteCommandLists
constexpr int ExecuteCommandListsIndex = 10;

// Return addresses checked for a frame generation library, the library can call through a wrapper or a layer
constexpr USHORT StackFrames = 8;

// Timestamps work on direct and compute queues, copy queues are left untimed
constexpr UINT TypeCount = 2;
constexpr UINT SlotsPerType = 64;
constexpr UINT SlotCount = SlotsPerType * TypeCount;

// One timed submission: a list with the start timestamp before the library's lists and one with the end timestamp
// and its resolve after them. Recorded once, executed again when the last execution finished.
struct Slot
{
    ID3D12CommandAllocator* allocator = nullptr;
    ID3D12GraphicsCommandList* begin = nullptr;
    ID3D12GraphicsCommandList* end = nullptr;
    bool pending = false;
};

struct ModuleRange
{
    uintptr_t begin = 0;
    uintptr_t end = 0;
};

PFN_ExecuteCommandLists o_ExecuteCommandLists = nullptr;
std::once_flag hookOnce;

std::mutex timerMutex;
std::atomic<bool> timerActive = false;
std::vector<ModuleRange> modules;

ID3D12Device* timerDevice = nullptr;
ID3D12QueryHeap* queryHeap = nullptr;
ID3D12Resource* readback = nullptr;
std::array<Slot, SlotCount> slots {};
std::array<UINT, TypeCount> nextSlot {};
std::array<UINT64, TypeCount> frequencies {};

void ReleaseResources()
{
    for (auto& slot : slots)
    {
        DeferredRelease::Release(slot.begin);
        DeferredRelease::Release(slot.end);
        DeferredRelease::Release(slot.allocator);
        slot.pending = false;
    }

    DeferredRelease::Release(queryHeap);
    DeferredRelease::Release(readback);
    DeferredRelease::Release(timerDevice);

    nextSlot = {};
    frequencies = {};
}

// The query heap and the readback buffer for the device of the queue, the slots' lists are created when first used
bool CreateResources(ID3D12Device* device)
{
    D3D12_QUERY_HEAP_DESC heapDesc = {};
    heapDesc.Count = SlotCount * 2;
    heapDesc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;

    auto result = device->CreateQueryHeap(&heapDesc, IID_PPV_ARGS(&queryHeap));

    if (result != S_OK)
    {
        LOG_ERROR("CreateQueryHeap error: {:X}", (UINT) result);
        return false;
    }

    D3D12_RESOURCE_DESC bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(SlotCount * 2 * sizeof(UINT64));
    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_READBACK;

    result = device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &bufferDesc,
                                             D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readback));

    if (result != S_OK)
    {
        LOG_ERROR("CreateCommittedResource error: {:X}", (UINT) result);
        DeferredRelease::Release(queryHeap);
        return false;
    }

    // A finished slot has both timestamps, unfinished ones stay 0
    UINT64* data = nullptr;
    D3D12_RANGE noRead = { 0, 0 };

    if (readback->Map(0, &noRead, reinterpret_cast<void**>(&data)) == S_OK && data != nullptr)
    {
        memset(data, 0, SlotCount * 2 * sizeof(UINT64));
        readback->Unmap(0, nullptr);
    }

    timerDevice = device;
    timerDevice->AddRef();

    return true;
}

bool RecordSlot(Slot& slot, UINT index, D3D12_COMMAND_LIST_TYPE type)
{
    auto result = timerDevice->CreateCommandAllocator(type, IID_PPV_ARGS(&slot.allocator));

    if (result != S_OK)
    {
        LOG_ERROR("CreateCommandAllocator error: {:X}", (UINT) result);
        return false;
    }

    if (timerDevice->CreateCommandList(0, type, slot.allocator, nullptr, IID_PPV_ARGS(&slot.begin)) != S_OK ||
        timerDevice->CreateCommandList(0, type, slot.allocator, nullptr, IID_PPV_ARGS(&slot.end)) != S_OK)
    {
        LOG_ERROR("CreateCommandList error");
        DeferredRelease::Release(slot.begin);
        DeferredRelease::Release(slot.end);
        DeferredRelease::Release(slot.allocator);
        return false;
    }

    slot.begin->SetName(L"FGGpuTime begin");
    slot.end->SetName(L"FGGpuTime end");

    slot.begin->EndQuery(queryHeap, D3D12_QUERY_TYPE_TIMESTAMP, index * 2);
    slot.begin->Close();

    slot.end->EndQuery(queryHeap, D3D12_QUERY_TYPE_TIMESTAMP, index * 2 + 1);
    slot.end->ResolveQueryData(queryHeap, D3D12_QUERY_TYPE_TIMESTAMP, index * 2, 2, readback,
                               index * 2 * sizeof(UINT64));
    slot.end->Close();

    return true;
}

// Opti's own submissions (its overlay, UI and latency lists) can run inside the frame generation's present
ModuleRange optiRange {};

ModuleRange RangeOf(HMODULE module)
{
    auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(module);
    auto nt = reinterpret_cast<IMAGE_NT_HEADERS*>(reinterpret_cast<BYTE*>(module) + dos->e_lfanew);
    auto begin = reinterpret_cast<uintptr_t>(module);

    return { begin, begin + nt->OptionalHeader.SizeOfImage };
}

bool FromModules(void* const* frames, USHORT frameCount)
{
    if (frameCount == 0)
        return false;

    const auto caller = reinterpret_cast<uintptr_t>(frames[0]);
    if (caller >= optiRange.begin && caller < optiRange.end)
        return false;

    for (USHORT i = 0; i < frameCount; i++)
    {
        const auto address = reinterpret_cast<uintptr_t>(frames[i]);

        for (const auto& range : modules)
        {
            if (address >= range.begin && address < range.end)
                return true;
        }
    }

    return false;
}

// A free slot for the queue, nullptr when the submission can't be timed. Called with timerMutex held
Slot* AcquireSlot(ID3D12CommandQueue* queue)
{
    const auto desc = queue->GetDesc();
    UINT type = 0;

    if (desc.Type == D3D12_COMMAND_LIST_TYPE_DIRECT)
        type = 0;
    else if (desc.Type == D3D12_COMMAND_LIST_TYPE_COMPUTE)
        type = 1;
    else
        return nullptr;

    ID3D12Device* device = nullptr;
    if (queue->GetDevice(IID_PPV_ARGS(&device)) != S_OK || device == nullptr)
        return nullptr;

    // The game can recreate its device
    if (device != timerDevice)
    {
        ReleaseResources();

        if (!CreateResources(device))
        {
            device->Release();
            return nullptr;
        }
    }

    device->Release();

    if (frequencies[type] == 0)
        queue->GetTimestampFrequency(&frequencies[type]);

    // Far behind GPU or not, a submission isn't timed when every slot still runs
    for (UINT i = 0; i < SlotsPerType; i++)
    {
        const UINT index = type * SlotsPerType + (nextSlot[type] + i) % SlotsPerType;
        auto& slot = slots[index];

        if (slot.pending)
            continue;

        if (slot.begin == nullptr && !RecordSlot(slot, index, desc.Type))
            return nullptr;

        nextSlot[type] = (index - type * SlotsPerType + 1) % SlotsPerType;
        slot.pending = true;
        return &slot;
    }

    return nullptr;
}

void STDMETHODCALLTYPE hkExecuteCommandLists(ID3D12CommandQueue* This, UINT NumCommandLists,
                                             ID3D12CommandList* const* ppCommandLists)
{
    if (!timerActive.load(std::memory_order_relaxed) || NumCommandLists == 0 || This == nullptr)
        return o_ExecuteCommandLists(This, NumCommandLists, ppCommandLists);

    void* frames[StackFrames] {};
    const USHORT frameCount = RtlCaptureStackBackTrace(1, StackFrames, frames, nullptr);

    Slot* slot = nullptr;

    {
        std::scoped_lock lock(timerMutex);

        if (FromModules(frames, frameCount))
            slot = AcquireSlot(This);
    }

    if (slot == nullptr)
        return o_ExecuteCommandLists(This, NumCommandLists, ppCommandLists);

    std::vector<ID3D12CommandList*> lists;
    lists.reserve(NumCommandLists + 2);
    lists.push_back(slot->begin);
    lists.insert(lists.end(), ppCommandLists, ppCommandLists + NumCommandLists);
    lists.push_back(slot->end);

    o_ExecuteCommandLists(This, static_cast<UINT>(lists.size()), lists.data());
}
} // namespace

void FGGpuTime_Dx12::Hook(ID3D12CommandQueue* queue)
{
    if (queue == nullptr)
        return;

    std::call_once(hookOnce,
                   [queue]
                   {
                       HMODULE opti = nullptr;
                       if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                                  GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                              reinterpret_cast<LPCWSTR>(&hkExecuteCommandLists), &opti))
                       {
                           optiRange = RangeOf(opti);
                       }

                       auto vtable = *reinterpret_cast<void***>(queue);
                       o_ExecuteCommandLists =
                           reinterpret_cast<PFN_ExecuteCommandLists>(vtable[ExecuteCommandListsIndex]);

                       DetourTransactionBegin();
                       DetourUpdateThread(GetCurrentThread());
                       DetourAttach(reinterpret_cast<PVOID*>(&o_ExecuteCommandLists),
                                    reinterpret_cast<PVOID>(hkExecuteCommandLists));

                       if (auto result = DetourTransactionCommit(); result != NO_ERROR)
                       {
                           LOG_ERROR("Hooking ExecuteCommandLists failed: {}", result);
                           o_ExecuteCommandLists = nullptr;
                           return;
                       }

                       LOG_INFO("Hooked ExecuteCommandLists for the frame generation GPU time");
                   });
}

void FGGpuTime_Dx12::SetModules(const std::vector<HMODULE>& libraries)
{
    std::vector<ModuleRange> ranges;

    for (auto library : libraries)
    {
        if (library == nullptr)
            continue;

        const auto range = RangeOf(library);

        if (std::ranges::none_of(ranges, [&range](const ModuleRange& other) { return other.begin == range.begin; }))
            ranges.push_back(range);
    }

    std::scoped_lock lock(timerMutex);
    modules = std::move(ranges);
    timerActive = o_ExecuteCommandLists != nullptr && !modules.empty();
}

double FGGpuTime_Dx12::Collect()
{
    std::scoped_lock lock(timerMutex);

    if (readback == nullptr || std::ranges::none_of(slots, [](const Slot& slot) { return slot.pending; }))
        return 0.0;

    UINT64* data = nullptr;
    D3D12_RANGE readRange = { 0, SlotCount * 2 * sizeof(UINT64) };

    if (readback->Map(0, &readRange, reinterpret_cast<void**>(&data)) != S_OK || data == nullptr)
        return 0.0;

    double finishedMs = 0.0;
    size_t writtenEnd = 0;

    for (UINT index = 0; index < SlotCount; index++)
    {
        auto& slot = slots[index];

        if (!slot.pending || data[index * 2] == 0 || data[index * 2 + 1] == 0)
            continue;

        const UINT64 start = data[index * 2];
        const UINT64 end = data[index * 2 + 1];
        const UINT64 frequency = frequencies[index / SlotsPerType];

        // Filters out wrong timestamps, as the upscaler time does
        if (end >= start && frequency != 0)
        {
            const double ms = (end - start) * 1000.0 / static_cast<double>(frequency);

            if (ms < 100.0)
                finishedMs += ms;
        }

        data[index * 2] = 0;
        data[index * 2 + 1] = 0;
        writtenEnd = (index + 1) * 2 * sizeof(UINT64);
        slot.pending = false;
    }

    D3D12_RANGE writtenRange = { 0, writtenEnd };
    readback->Unmap(0, &writtenRange);

    return finishedMs;
}

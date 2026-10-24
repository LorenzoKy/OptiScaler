#include "pch.h"
#include "Backends.h"

#include <imgui/imgui_impl_dx11.h>
#include <imgui/imgui_impl_dx12.h>

#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

namespace Backends
{
namespace
{
// The format a render target view draws in, for a typeless back buffer
DXGI_FORMAT RenderTargetFormat(DXGI_FORMAT format)
{
    switch (format)
    {
    case DXGI_FORMAT_R8G8B8A8_TYPELESS:
        return DXGI_FORMAT_R8G8B8A8_UNORM;

    case DXGI_FORMAT_B8G8R8A8_TYPELESS:
        return DXGI_FORMAT_B8G8R8A8_UNORM;

    case DXGI_FORMAT_B8G8R8X8_TYPELESS:
        return DXGI_FORMAT_B8G8R8X8_UNORM;

    case DXGI_FORMAT_R10G10B10A2_TYPELESS:
        return DXGI_FORMAT_R10G10B10A2_UNORM;

    case DXGI_FORMAT_R16G16B16A16_TYPELESS:
        return DXGI_FORMAT_R16G16B16A16_FLOAT;

    default:
        return format;
    }
}

// Direct3D 12 ---------------------------------------------------------------------------------------------

constexpr UINT kFrames = 4; // Frames of command allocators, and of ImGui's vertex buffers
constexpr UINT kSrvDescriptors = 64;

DescriptorHeapAllocator g_srvAlloc;

struct Dx12
{
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12DescriptorHeap> rtvHeap;
    ComPtr<ID3D12DescriptorHeap> srvHeap;
    ComPtr<ID3D12CommandAllocator> allocators[kFrames];
    UINT64 allocatorFence[kFrames] {};
    ComPtr<ID3D12GraphicsCommandList> list;
    ComPtr<ID3D12Fence> fence;
    HANDLE fenceEvent = nullptr;
    UINT64 fenceValue = 0;
    UINT rtvIncrement = 0;
    UINT rtvCount = 0;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    uint64_t frame = 0;
    bool imgui = false;
};

Dx12 g12;
ID3D12CommandQueue* g12Failed = nullptr; // The queue that couldn't start, not tried again

void Dx12WaitIdle()
{
    if (g12.fence && g12.fenceEvent && g12.fence->GetCompletedValue() < g12.fenceValue)
    {
        if (SUCCEEDED(g12.fence->SetEventOnCompletion(g12.fenceValue, g12.fenceEvent)))
            WaitForSingleObject(g12.fenceEvent, 2000);
    }
}

void Dx12Teardown()
{
    Dx12WaitIdle();

    if (g12.imgui)
        ImGui_ImplDX12_Shutdown(false, true);

    g_srvAlloc.Destroy();

    if (g12.fenceEvent != nullptr)
        CloseHandle(g12.fenceEvent);

    g12 = Dx12 {};
}

bool Dx12Init(const Target& target)
{
    if (target.queue12 == nullptr)
        return false;

    if (target.queue12->GetDesc().Type != D3D12_COMMAND_LIST_TYPE_DIRECT)
    {
        LOG_WARN("The swapchain's queue isn't a direct queue, no overlay");
        return false;
    }

    Dx12 next;
    next.queue = target.queue12;

    if (FAILED(target.queue12->GetDevice(IID_PPV_ARGS(&next.device))))
        return false;

    D3D12_DESCRIPTOR_HEAP_DESC rtvDesc {};
    rtvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvDesc.NumDescriptors = std::max<UINT>(target.bufferCount, 8);
    rtvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

    if (FAILED(next.device->CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(&next.rtvHeap))))
        return false;

    next.rtvCount = rtvDesc.NumDescriptors;
    next.rtvIncrement = next.device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    D3D12_DESCRIPTOR_HEAP_DESC srvDesc {};
    srvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvDesc.NumDescriptors = kSrvDescriptors;
    srvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

    if (FAILED(next.device->CreateDescriptorHeap(&srvDesc, IID_PPV_ARGS(&next.srvHeap))))
        return false;

    for (UINT i = 0; i < kFrames; i++)
    {
        if (FAILED(
                next.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&next.allocators[i]))))
            return false;
    }

    if (FAILED(next.device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, next.allocators[0].Get(), nullptr,
                                              IID_PPV_ARGS(&next.list))))
        return false;

    next.list->Close();

    if (FAILED(next.device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&next.fence))))
        return false;

    next.fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);

    if (next.fenceEvent == nullptr)
        return false;

    next.format = RenderTargetFormat(target.format);

    g12 = std::move(next);

    g_srvAlloc.Destroy();
    g_srvAlloc.Create(g12.device.Get(), g12.srvHeap.Get());

    ImGui_ImplDX12_InitInfo info {};
    info.Device = g12.device.Get();
    info.CommandQueue = g12.queue.Get();
    info.NumFramesInFlight = kFrames;
    info.RTVFormat = g12.format;
    info.DSVFormat = DXGI_FORMAT_UNKNOWN;
    info.SrvDescriptorHeap = g12.srvHeap.Get();
    info.SrvDescriptorAllocFn = [](ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE* cpu,
                                   D3D12_GPU_DESCRIPTOR_HANDLE* gpu) { g_srvAlloc.Alloc(cpu, gpu); };
    info.SrvDescriptorFreeFn = [](ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE cpu,
                                  D3D12_GPU_DESCRIPTOR_HANDLE gpu) { g_srvAlloc.Free(cpu, gpu); };

    g12.imgui = ImGui_ImplDX12_Init(&info);

    if (!g12.imgui)
    {
        Dx12Teardown();
        return false;
    }

    LOG_INFO("Direct3D 12 overlay started, format {}, {} back buffers", (int) g12.format, target.bufferCount);
    return true;
}

void Dx12Render(const Target& target)
{
    ComPtr<IDXGISwapChain3> swapChain3;

    if (FAILED(target.swapChain->QueryInterface(IID_PPV_ARGS(&swapChain3))))
        return;

    ComPtr<ID3D12Resource> backBuffer;

    if (FAILED(swapChain3->GetBuffer(swapChain3->GetCurrentBackBufferIndex(), IID_PPV_ARGS(&backBuffer))))
        return;

    const UINT index = swapChain3->GetCurrentBackBufferIndex() % g12.rtvCount;
    const UINT slot = static_cast<UINT>(g12.frame++ % kFrames);

    // The allocator is free again once the GPU is done with the frame that used it
    if (g12.fence->GetCompletedValue() < g12.allocatorFence[slot])
    {
        if (SUCCEEDED(g12.fence->SetEventOnCompletion(g12.allocatorFence[slot], g12.fenceEvent)))
            WaitForSingleObject(g12.fenceEvent, 2000);
    }

    auto* allocator = g12.allocators[slot].Get();

    if (FAILED(allocator->Reset()) || FAILED(g12.list->Reset(allocator, nullptr)))
        return;

    D3D12_RESOURCE_BARRIER barrier {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource = backBuffer.Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    g12.list->ResourceBarrier(1, &barrier);

    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc {};
    rtvDesc.Format = g12.format;
    rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;

    D3D12_CPU_DESCRIPTOR_HANDLE rtv = g12.rtvHeap->GetCPUDescriptorHandleForHeapStart();
    rtv.ptr += static_cast<SIZE_T>(index) * g12.rtvIncrement;
    g12.device->CreateRenderTargetView(backBuffer.Get(), &rtvDesc, rtv);

    ID3D12DescriptorHeap* heaps[] = { g12.srvHeap.Get() };
    g12.list->SetDescriptorHeaps(1, heaps);
    g12.list->OMSetRenderTargets(1, &rtv, FALSE, nullptr);

    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), g12.list.Get());

    std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
    g12.list->ResourceBarrier(1, &barrier);

    if (FAILED(g12.list->Close()))
        return;

    ID3D12CommandList* lists[] = { g12.list.Get() };
    g12.queue->ExecuteCommandLists(1, lists);

    g12.fenceValue++;
    g12.queue->Signal(g12.fence.Get(), g12.fenceValue);
    g12.allocatorFence[slot] = g12.fenceValue;
}

// Direct3D 11 ---------------------------------------------------------------------------------------------

struct Dx11
{
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    bool imgui = false;
};

Dx11 g11;
ID3D11Device* g11Failed = nullptr;

void Dx11Teardown()
{
    if (g11.imgui)
        ImGui_ImplDX11_Shutdown(false);

    g11 = Dx11 {};
}

bool Dx11Init(const Target& target)
{
    if (target.device11 == nullptr)
        return false;

    Dx11 next;
    next.device = target.device11;
    target.device11->GetImmediateContext(&next.context);

    if (next.context == nullptr)
        return false;

    g11 = std::move(next);
    g11.imgui = ImGui_ImplDX11_Init(g11.device.Get(), g11.context.Get());

    if (!g11.imgui)
    {
        Dx11Teardown();
        return false;
    }

    LOG_INFO("Direct3D 11 overlay started");
    return true;
}

void Dx11Render(const Target& target)
{
    ComPtr<ID3D11Texture2D> backBuffer;

    if (FAILED(target.swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer))))
        return;

    D3D11_TEXTURE2D_DESC textureDesc {};
    backBuffer->GetDesc(&textureDesc);

    D3D11_RENDER_TARGET_VIEW_DESC rtvDesc {};
    rtvDesc.Format = RenderTargetFormat(textureDesc.Format);
    rtvDesc.ViewDimension =
        textureDesc.SampleDesc.Count > 1 ? D3D11_RTV_DIMENSION_TEXTURE2DMS : D3D11_RTV_DIMENSION_TEXTURE2D;

    ComPtr<ID3D11RenderTargetView> rtv;

    if (FAILED(g11.device->CreateRenderTargetView(backBuffer.Get(), &rtvDesc, &rtv)))
        return;

    // The game's targets come back after the overlay
    ID3D11RenderTargetView* previousTargets[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] {};
    ID3D11DepthStencilView* previousDepth = nullptr;
    g11.context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, previousTargets, &previousDepth);

    ID3D11RenderTargetView* targets[] = { rtv.Get() };
    g11.context->OMSetRenderTargets(1, targets, nullptr);

    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

    g11.context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, previousTargets, previousDepth);

    for (auto* previous : previousTargets)
    {
        if (previous != nullptr)
            previous->Release();
    }

    if (previousDepth != nullptr)
        previousDepth->Release();
}
} // namespace

bool Prepare(const Target& target)
{
    if (target.kind == Kind::Dx12)
    {
        if (g11.imgui)
            Dx11Teardown();

        if (g12.imgui && (g12.queue.Get() != target.queue12 || g12.format != RenderTargetFormat(target.format)))
            Dx12Teardown();

        if (!g12.imgui)
        {
            if (g12Failed == target.queue12)
                return false;

            if (!Dx12Init(target))
            {
                g12Failed = target.queue12;
                LOG_ERROR("Couldn't start the Direct3D 12 overlay");
                return false;
            }

            g12Failed = nullptr;
        }

        return true;
    }

    if (target.kind == Kind::Dx11)
    {
        if (g12.imgui)
            Dx12Teardown();

        if (g11.imgui && g11.device.Get() != target.device11)
            Dx11Teardown();

        if (!g11.imgui)
        {
            if (g11Failed == target.device11)
                return false;

            if (!Dx11Init(target))
            {
                g11Failed = target.device11;
                LOG_ERROR("Couldn't start the Direct3D 11 overlay");
                return false;
            }

            g11Failed = nullptr;
        }

        return true;
    }

    return false;
}

void NewFrame()
{
    if (g12.imgui)
        ImGui_ImplDX12_NewFrame();
    else if (g11.imgui)
        ImGui_ImplDX11_NewFrame();
}

void Render(const Target& target)
{
    if (g12.imgui && target.kind == Kind::Dx12)
        Dx12Render(target);
    else if (g11.imgui && target.kind == Kind::Dx11)
        Dx11Render(target);
}

void Shutdown()
{
    if (g12.imgui || g12.fence)
        Dx12Teardown();

    if (g11.imgui)
        Dx11Teardown();
}
} // namespace Backends

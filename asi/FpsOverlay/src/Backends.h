#pragma once

#include <imgui/imgui.h>

#include <d3d11.h>
#include <d3d12.h>
#include <dxgi1_4.h>

// Draws ImGui's draw data on the back buffer of the game's swapchain, from inside its Present
namespace Backends
{
enum class Kind
{
    None,
    Dx11,
    Dx12,
};

// What the swapchain is made of
struct Target
{
    Kind kind = Kind::None;
    IDXGISwapChain* swapChain = nullptr;
    ID3D12CommandQueue* queue12 = nullptr; // The queue the swapchain was created with
    ID3D11Device* device11 = nullptr;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    UINT bufferCount = 0;
};

// Starts the backend for the target, or restarts it when the device, queue or format changed. False if it can't draw.
bool Prepare(const Target& target);

// Between Prepare and ImGui::NewFrame: the backend's part of the new frame
void NewFrame();

// Draws ImGui::GetDrawData() on the swapchain's current back buffer
void Render(const Target& target);

// Waits for the GPU and frees the backend
void Shutdown();
} // namespace Backends

#include "pch.h"
#include <State.h>

#include "Backends.h"
#include "OverlayConfig.h"
#include "OverlayRender.h"
#include "OverlaySettingsUi.h"

#include <detours/detours.h>
#include <imgui/imgui.h>
#include <imgui/imgui_impl_win32.h>

#include <psapi.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

// The header of the Win32 backend keeps it out of its includes
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// OptiScaler's FPS overlay as a plugin: loaded by OptiScaler from its plugins folder (or by any ASI loader), it
// hooks the Present of the game's swapchain on its own, so it needs nothing from OptiScaler's DLL.

namespace
{
HMODULE g_self = nullptr;
std::wstring g_folder;

// Everything ImGui does happens under this lock: the settings window's input arrives on the window's thread. It's
// recursive because Present can dispatch window messages on the thread that is drawing.
std::recursive_mutex g_imguiMutex;
bool g_imguiReady = false;
HWND g_imguiWindow = nullptr;
ImGuiStyle g_baseStyle;
float g_styleScale = 0.0f;
std::atomic<bool> g_settingsOpen { false };
FrameStats g_stats;
int g_failures = 0;

// The swapchain the overlay is drawn on: the first one big enough, another takes over when it stops presenting
IDXGISwapChain* g_main = nullptr;
double g_mainLastMs = 0.0;

// The queue each swapchain was created with, Direct3D 12 draws on it to stay in order with the game's frame
std::mutex g_queueMutex;
std::unordered_map<void*, ComPtr<ID3D12CommandQueue>> g_queues;

WNDPROC g_originalProc = nullptr;

thread_local bool g_inPresent = false;

double QpcMs()
{
    static const double frequency = []()
    {
        LARGE_INTEGER value {};
        QueryPerformanceFrequency(&value);
        return static_cast<double>(value.QuadPart);
    }();

    LARGE_INTEGER counter {};
    QueryPerformanceCounter(&counter);
    return static_cast<double>(counter.QuadPart) * 1000.0 / frequency;
}

// The module with that name from the System32 folder. OptiScaler named dxgi.dll is in the process too, and has
// other entry points.
HMODULE LoadSystemModule(const wchar_t* name)
{
    wchar_t systemDir[MAX_PATH] {};
    GetSystemDirectoryW(systemDir, MAX_PATH);
    const auto wanted = std::filesystem::path(systemDir) / name;

    HMODULE modules[1024] {};
    DWORD needed = 0;

    if (EnumProcessModules(GetCurrentProcess(), modules, sizeof(modules), &needed))
    {
        const size_t count = std::min<size_t>(needed / sizeof(HMODULE), std::size(modules));

        for (size_t i = 0; i < count; i++)
        {
            wchar_t path[MAX_PATH] {};

            if (GetModuleFileNameW(modules[i], path, MAX_PATH) != 0 && _wcsicmp(path, wanted.c_str()) == 0)
                return modules[i];
        }
    }

    return LoadLibraryExW(wanted.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
}

// The settings window --------------------------------------------------------------------------------------

BOOL(WINAPI* o_SetCursorPos)(int, int) = SetCursorPos;
BOOL(WINAPI* o_ClipCursor)(const RECT*) = ClipCursor;

BOOL WINAPI hk_SetCursorPos(int x, int y)
{
    // The game keeps the cursor in the middle of the window to look around
    if (g_settingsOpen)
        return TRUE;

    return o_SetCursorPos(x, y);
}

BOOL WINAPI hk_ClipCursor(const RECT* rect)
{
    if (g_settingsOpen && rect != nullptr)
        return TRUE;

    return o_ClipCursor(rect);
}

// One detours transaction at a time exists in a process, OptiScaler makes its own while the game starts
template <typename Attach> LONG RunDetours(Attach&& attach)
{
    LONG result = ERROR_INVALID_OPERATION;

    for (int attempt = 0; attempt < 20; attempt++)
    {
        if (DetourTransactionBegin() == NO_ERROR)
        {
            DetourUpdateThread(GetCurrentThread());
            attach();
            result = DetourTransactionCommit();

            if (result == NO_ERROR)
                return result;
        }

        Sleep(25);
    }

    return result;
}

// The file name of the module that has the code at the address
std::string ModuleOf(void* address)
{
    HMODULE module = nullptr;

    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            (LPCWSTR) address, &module))
        return "unknown";

    wchar_t path[MAX_PATH] {};
    GetModuleFileNameW(module, path, MAX_PATH);
    return std::filesystem::path(path).filename().string();
}

void InstallCursorHooks()
{
    const LONG result = RunDetours(
        []()
        {
            DetourAttach(&(PVOID&) o_SetCursorPos, (PVOID) hk_SetCursorPos);
            DetourAttach(&(PVOID&) o_ClipCursor, (PVOID) hk_ClipCursor);
        });

    if (result != NO_ERROR)
        LOG_WARN("Cursor hooks failed: {}", (int) result);
}

LRESULT CALLBACK HookedWndProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (g_settingsOpen && g_imguiReady)
    {
        std::lock_guard lock(g_imguiMutex);

        ImGui_ImplWin32_WndProcHandler(window, message, wParam, lParam);

        const bool mouseMessage = message >= WM_MOUSEFIRST && message <= WM_MOUSELAST;
        const bool keyMessage =
            message == WM_KEYDOWN || message == WM_SYSKEYDOWN || message == WM_CHAR || message == WM_SYSCHAR;

        // The game doesn't get the clicks and keys that are meant for the window
        if (mouseMessage || (keyMessage && ImGui::GetIO().WantCaptureKeyboard))
            return 0;

        if (message == WM_INPUT)
            return DefWindowProcW(window, message, wParam, lParam);

        if (message == WM_SETCURSOR && LOWORD(lParam) == HTCLIENT)
            return TRUE;
    }

    return CallWindowProcW(g_originalProc, window, message, wParam, lParam);
}

void SetSettingsOpen(bool open)
{
    if (g_settingsOpen == open)
        return;

    g_settingsOpen = open;

    if (open)
    {
        // Let the cursor go where the window is
        o_ClipCursor(nullptr);
    }
}

// ImGui -----------------------------------------------------------------------------------------------------

void ApplyStyle(float scale)
{
    if (std::abs(scale - g_styleScale) < 0.001f)
        return;

    g_styleScale = scale;

    auto& style = ImGui::GetStyle();
    style = g_baseStyle;
    style.ScaleAllSizes(scale);
}

void EnsureImGui(HWND window)
{
    if (!g_imguiReady)
    {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();

        auto& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.LogFilename = nullptr;

        ImGui::StyleColorsDark();
        auto& style = ImGui::GetStyle();
        style.WindowRounding = 6.0f;
        style.FrameRounding = 4.0f;
        style.GrabRounding = 4.0f;
        style.WindowBorderSize = 1.0f;
        g_baseStyle = style;
        g_styleScale = 1.0f;

        OverlayRender::InitFonts();
        g_imguiReady = true;
    }

    if (window != g_imguiWindow)
    {
        if (g_imguiWindow != nullptr)
            ImGui_ImplWin32_Shutdown();

        ImGui_ImplWin32_Init(window);
        g_imguiWindow = window;

        // The settings window's mouse and keyboard
        g_originalProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(window, GWLP_WNDPROC, (LONG_PTR) HookedWndProc));
    }
}

// The display's color space decides how the overlay's colors are converted
UiMode DetectUiMode(IDXGISwapChain* swapChain, DXGI_FORMAT format, bool& hdrOutputActive)
{
    static double checkedAt = -10000.0;
    static bool cachedHdr = false;
    const double now = QpcMs();

    if (now - checkedAt > 2000.0)
    {
        checkedAt = now;
        cachedHdr = false;

        ComPtr<IDXGIOutput> output;

        if (SUCCEEDED(swapChain->GetContainingOutput(&output)) && output)
        {
            ComPtr<IDXGIOutput6> output6;
            DXGI_OUTPUT_DESC1 desc {};

            if (SUCCEEDED(output.As(&output6)) && SUCCEEDED(output6->GetDesc1(&desc)))
                cachedHdr = desc.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
        }
    }

    hdrOutputActive = cachedHdr;

    if (format == DXGI_FORMAT_R16G16B16A16_FLOAT || format == DXGI_FORMAT_R16G16B16A16_TYPELESS)
        return UiMode::ScRGB;

    if ((format == DXGI_FORMAT_R10G10B10A2_UNORM || format == DXGI_FORMAT_R10G10B10A2_TYPELESS) && cachedHdr)
        return UiMode::PQ;

    return UiMode::SDR;
}

bool GameIsForeground()
{
    HWND foreground = GetForegroundWindow();

    if (foreground == nullptr)
        return false;

    DWORD processId = 0;
    GetWindowThreadProcessId(foreground, &processId);
    return processId == GetCurrentProcessId();
}

bool KeyPressed(uint32_t key, bool& wasDown)
{
    if (key == 0)
        return false;

    const bool down = (GetAsyncKeyState((int) key) & 0x8000) != 0;
    const bool pressed = down && !wasDown;
    wasDown = down;
    return pressed;
}

// Which swapchain is the game's
bool ClaimSwapChain(IDXGISwapChain* swapChain, const DXGI_SWAP_CHAIN_DESC& desc, double now)
{
    if (g_main == swapChain)
    {
        g_mainLastMs = now;
        return true;
    }

    // Another swapchain is presenting, small ones (launchers, previews) don't take over
    if (g_main != nullptr && now - g_mainLastMs < 1000.0)
        return false;

    if (desc.BufferDesc.Width < 320 || desc.BufferDesc.Height < 240)
        return false;

    LOG_INFO("Overlay on swapchain {:p}, {}x{}, format {}, {} buffers", (void*) swapChain, desc.BufferDesc.Width,
             desc.BufferDesc.Height, (int) desc.BufferDesc.Format, desc.BufferCount);

    g_main = swapChain;
    g_mainLastMs = now;
    g_stats.Reset();
    return true;
}

bool ResolveTarget(IDXGISwapChain* swapChain, const DXGI_SWAP_CHAIN_DESC& desc, Backends::Target& target,
                   ComPtr<ID3D12CommandQueue>& queueHolder, ComPtr<ID3D11Device>& deviceHolder)
{
    target = {};
    target.swapChain = swapChain;
    target.format = desc.BufferDesc.Format;
    target.bufferCount = desc.BufferCount;

    {
        std::lock_guard lock(g_queueMutex);

        if (auto it = g_queues.find(swapChain); it != g_queues.end())
            queueHolder = it->second;
    }

    // A Direct3D 12 swapchain gives its queue as the device
    if (!queueHolder)
        swapChain->GetDevice(IID_PPV_ARGS(&queueHolder));

    auto& state = State::Instance();

    if (queueHolder)
    {
        ComPtr<ID3D12Device> device;
        queueHolder->GetDevice(IID_PPV_ARGS(&device));

        state.currentD3D12Device = device.Get();
        state.currentD3D11Device = nullptr;

        target.kind = Backends::Kind::Dx12;
        target.queue12 = queueHolder.Get();
        return true;
    }

    if (SUCCEEDED(swapChain->GetDevice(IID_PPV_ARGS(&deviceHolder))) && deviceHolder)
    {
        state.currentD3D12Device = nullptr;
        state.currentD3D11Device = deviceHolder.Get();

        target.kind = Backends::Kind::Dx11;
        target.device11 = deviceHolder.Get();
        return true;
    }

    return false;
}

void OnPresentImpl(IDXGISwapChain* swapChain)
{
    DXGI_SWAP_CHAIN_DESC desc {};

    if (FAILED(swapChain->GetDesc(&desc)))
        return;

    const double now = QpcMs();

    if (!ClaimSwapChain(swapChain, desc, now))
        return;

    auto& cfg = OverlayConfig::Get();

    // The time between presents of the swapchain. Pauses (the game loading, a minimized window) aren't frames.
    if (g_stats.lastPresentMs > 0.0)
    {
        const double delta = now - g_stats.lastPresentMs;

        if (delta > 0.0 && delta < 1000.0)
            g_stats.OnFrame(now, static_cast<float>(delta), cfg.FpsOverlayShowLows.get(),
                            cfg.FpsOverlayShowPointOneLow.get());
    }

    g_stats.lastPresentMs = now;

    cfg.PollReload();

    // Hotkeys, read here so they work with any way the game reads its keys
    static bool toggleWasDown = false;
    static bool settingsWasDown = false;
    const bool foreground = GameIsForeground();

    if (foreground)
    {
        if (KeyPressed(cfg.ToggleKey.get(), toggleWasDown))
            cfg.ShowFps = !cfg.ShowFps.get();

        if (KeyPressed(cfg.SettingsKey.get(), settingsWasDown))
            SetSettingsOpen(!g_settingsOpen);
    }

    if (!cfg.ShowFps.get() && !g_settingsOpen)
        return;

    Backends::Target target;
    ComPtr<ID3D12CommandQueue> queueHolder;
    ComPtr<ID3D11Device> deviceHolder;

    if (!ResolveTarget(swapChain, desc, target, queueHolder, deviceHolder))
        return;

    std::lock_guard lock(g_imguiMutex);

    EnsureImGui(desc.OutputWindow);

    if (!Backends::Prepare(target))
        return;

    auto& io = ImGui::GetIO();

    OverlayHost host;
    host.displaySize = ImVec2(static_cast<float>(desc.BufferDesc.Width), static_cast<float>(desc.BufferDesc.Height));
    host.nowMs = now;
    host.api = target.kind == Backends::Kind::Dx12 ? "D3D12" : "D3D11";
    host.window = desc.OutputWindow;
    host.uiMode = DetectUiMode(swapChain, desc.BufferDesc.Format, host.hdrOutputActive);
    host.menuResScale = OverlayRender::AutoScale(host.displaySize.y);

    ApplyStyle(host.menuResScale);
    OverlayRender::UpdateFont();

    // The window's cursor is ImGui's while the settings are open, else the game's
    if (g_settingsOpen)
    {
        io.ConfigFlags &= ~ImGuiConfigFlags_NoMouseCursorChange;
        io.MouseDrawCursor = true;
    }
    else
    {
        io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
        io.MouseDrawCursor = false;
    }

    Backends::NewFrame();
    ImGui_ImplWin32_NewFrame();
    io.DisplaySize = host.displaySize;

    ImGui::NewFrame();

    OverlayRender::Render(host, g_stats);

    if (g_settingsOpen)
    {
        bool open = true;
        OverlaySettingsUi::Render(host, open);

        if (!open)
            SetSettingsOpen(false);
    }

    ImGui::Render();
    Backends::Render(target);
}

// A failing frame is skipped, the overlay stops after a few of them in a row so the game goes on
void OnPresent(IDXGISwapChain* swapChain)
{
    if (g_failures >= 5)
        return;

    try
    {
        OnPresentImpl(swapChain);
        g_failures = 0;
    }
    catch (const std::exception& e)
    {
        g_failures++;
        LOG_ERROR("Overlay frame failed: {}", e.what());
    }
    catch (...)
    {
        g_failures++;
        LOG_ERROR("Overlay frame failed");
    }

    if (g_failures >= 5)
        LOG_ERROR("The overlay is turned off after repeated errors");
}

// Hooks ------------------------------------------------------------------------------------------------------

using PFN_Present = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
using PFN_Present1 = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain1*, UINT, UINT, const DXGI_PRESENT_PARAMETERS*);
using PFN_CreateSwapChain = HRESULT(STDMETHODCALLTYPE*)(IDXGIFactory*, IUnknown*, DXGI_SWAP_CHAIN_DESC*,
                                                        IDXGISwapChain**);
using PFN_CreateSwapChainForHwnd = HRESULT(STDMETHODCALLTYPE*)(IDXGIFactory2*, IUnknown*, HWND,
                                                               const DXGI_SWAP_CHAIN_DESC1*,
                                                               const DXGI_SWAP_CHAIN_FULLSCREEN_DESC*, IDXGIOutput*,
                                                               IDXGISwapChain1**);
using PFN_CreateSwapChainForCoreWindow = HRESULT(STDMETHODCALLTYPE*)(IDXGIFactory2*, IUnknown*, IUnknown*,
                                                                     const DXGI_SWAP_CHAIN_DESC1*, IDXGIOutput*,
                                                                     IDXGISwapChain1**);
using PFN_CreateSwapChainForComposition = HRESULT(STDMETHODCALLTYPE*)(IDXGIFactory2*, IUnknown*,
                                                                      const DXGI_SWAP_CHAIN_DESC1*, IDXGIOutput*,
                                                                      IDXGISwapChain1**);

PFN_Present o_Present = nullptr;
PFN_Present1 o_Present1 = nullptr;
PFN_CreateSwapChain o_CreateSwapChain = nullptr;
PFN_CreateSwapChainForHwnd o_CreateSwapChainForHwnd = nullptr;
PFN_CreateSwapChainForCoreWindow o_CreateSwapChainForCoreWindow = nullptr;
PFN_CreateSwapChainForComposition o_CreateSwapChainForComposition = nullptr;

HRESULT STDMETHODCALLTYPE hk_Present(IDXGISwapChain* swapChain, UINT syncInterval, UINT flags)
{
    // Present1 can end up here, and the other way around
    if (g_inPresent || (flags & DXGI_PRESENT_TEST))
        return o_Present(swapChain, syncInterval, flags);

    g_inPresent = true;
    OnPresent(swapChain);
    const HRESULT result = o_Present(swapChain, syncInterval, flags);
    g_inPresent = false;
    return result;
}

HRESULT STDMETHODCALLTYPE hk_Present1(IDXGISwapChain1* swapChain, UINT syncInterval, UINT flags,
                                      const DXGI_PRESENT_PARAMETERS* parameters)
{
    if (g_inPresent || (flags & DXGI_PRESENT_TEST))
        return o_Present1(swapChain, syncInterval, flags, parameters);

    g_inPresent = true;
    OnPresent(swapChain);
    const HRESULT result = o_Present1(swapChain, syncInterval, flags, parameters);
    g_inPresent = false;
    return result;
}

// Present is hooked once, from the first swapchain: every swapchain of the same DXGI shares its code
void HookPresent(IDXGISwapChain* swapChain)
{
    static std::mutex mutex;
    std::lock_guard lock(mutex);

    if (o_Present != nullptr)
        return;

    void** vtable = *reinterpret_cast<void***>(swapChain);
    o_Present = reinterpret_cast<PFN_Present>(vtable[8]);

    ComPtr<IDXGISwapChain1> swapChain1;

    if (SUCCEEDED(swapChain->QueryInterface(IID_PPV_ARGS(&swapChain1))))
        o_Present1 = reinterpret_cast<PFN_Present1>((*reinterpret_cast<void***>(swapChain1.Get()))[22]);

    // In DXGI's own code, or in OptiScaler's wrapper of the swapchain when it made the swapchain before this hook
    LOG_INFO("Present is in {}", ModuleOf((void*) o_Present));

    const LONG result = RunDetours(
        []()
        {
            DetourAttach(&(PVOID&) o_Present, (PVOID) hk_Present);

            if (o_Present1 != nullptr)
                DetourAttach(&(PVOID&) o_Present1, (PVOID) hk_Present1);
        });

    if (result != NO_ERROR)
        LOG_ERROR("Present hook failed: {}", (int) result);
    else
        LOG_INFO("Present hooked");
}

void OnSwapChainCreated(IDXGISwapChain* swapChain, IUnknown* deviceOrQueue)
{
    ComPtr<ID3D12CommandQueue> queue;

    if (deviceOrQueue != nullptr && SUCCEEDED(deviceOrQueue->QueryInterface(IID_PPV_ARGS(&queue))))
    {
        std::lock_guard lock(g_queueMutex);
        g_queues[swapChain] = queue;
    }

    LOG_INFO("Swapchain {:p} created, {}", (void*) swapChain, queue ? "Direct3D 12 queue" : "device");
    HookPresent(swapChain);
}

HRESULT STDMETHODCALLTYPE hk_CreateSwapChain(IDXGIFactory* self, IUnknown* device, DXGI_SWAP_CHAIN_DESC* desc,
                                             IDXGISwapChain** swapChain)
{
    const HRESULT result = o_CreateSwapChain(self, device, desc, swapChain);

    if (SUCCEEDED(result) && swapChain != nullptr && *swapChain != nullptr)
        OnSwapChainCreated(*swapChain, device);

    return result;
}

HRESULT STDMETHODCALLTYPE hk_CreateSwapChainForHwnd(IDXGIFactory2* self, IUnknown* device, HWND window,
                                                    const DXGI_SWAP_CHAIN_DESC1* desc,
                                                    const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* fullscreenDesc,
                                                    IDXGIOutput* restrictToOutput, IDXGISwapChain1** swapChain)
{
    const HRESULT result =
        o_CreateSwapChainForHwnd(self, device, window, desc, fullscreenDesc, restrictToOutput, swapChain);

    if (SUCCEEDED(result) && swapChain != nullptr && *swapChain != nullptr)
        OnSwapChainCreated(*swapChain, device);

    return result;
}

HRESULT STDMETHODCALLTYPE hk_CreateSwapChainForCoreWindow(IDXGIFactory2* self, IUnknown* device, IUnknown* window,
                                                          const DXGI_SWAP_CHAIN_DESC1* desc,
                                                          IDXGIOutput* restrictToOutput, IDXGISwapChain1** swapChain)
{
    const HRESULT result = o_CreateSwapChainForCoreWindow(self, device, window, desc, restrictToOutput, swapChain);

    if (SUCCEEDED(result) && swapChain != nullptr && *swapChain != nullptr)
        OnSwapChainCreated(*swapChain, device);

    return result;
}

HRESULT STDMETHODCALLTYPE hk_CreateSwapChainForComposition(IDXGIFactory2* self, IUnknown* device,
                                                           const DXGI_SWAP_CHAIN_DESC1* desc,
                                                           IDXGIOutput* restrictToOutput, IDXGISwapChain1** swapChain)
{
    const HRESULT result = o_CreateSwapChainForComposition(self, device, desc, restrictToOutput, swapChain);

    if (SUCCEEDED(result) && swapChain != nullptr && *swapChain != nullptr)
        OnSwapChainCreated(*swapChain, device);

    return result;
}

// The swapchains the game makes are caught where they're made. A factory made here, without a device, gives the
// code every factory of the system's DXGI shares.
bool InstallFactoryHooks()
{
    HMODULE dxgi = LoadSystemModule(L"dxgi.dll");

    if (dxgi == nullptr)
    {
        LOG_ERROR("dxgi.dll not found");
        return false;
    }

    auto createFactory = reinterpret_cast<decltype(&CreateDXGIFactory1)>(GetProcAddress(dxgi, "CreateDXGIFactory1"));

    if (createFactory == nullptr)
    {
        LOG_ERROR("CreateDXGIFactory1 not found");
        return false;
    }

    ComPtr<IDXGIFactory1> factory;

    if (FAILED(createFactory(IID_PPV_ARGS(&factory))))
    {
        LOG_ERROR("Couldn't create a DXGI factory");
        return false;
    }

    void** vtable = *reinterpret_cast<void***>(factory.Get());
    o_CreateSwapChain = reinterpret_cast<PFN_CreateSwapChain>(vtable[10]);

    ComPtr<IDXGIFactory2> factory2;

    if (SUCCEEDED(factory.As(&factory2)))
    {
        void** vtable2 = *reinterpret_cast<void***>(factory2.Get());
        o_CreateSwapChainForHwnd = reinterpret_cast<PFN_CreateSwapChainForHwnd>(vtable2[15]);
        o_CreateSwapChainForCoreWindow = reinterpret_cast<PFN_CreateSwapChainForCoreWindow>(vtable2[16]);
        o_CreateSwapChainForComposition = reinterpret_cast<PFN_CreateSwapChainForComposition>(vtable2[24]);
    }

    const LONG result = RunDetours(
        []()
        {
            DetourAttach(&(PVOID&) o_CreateSwapChain, (PVOID) hk_CreateSwapChain);

            if (o_CreateSwapChainForHwnd != nullptr)
                DetourAttach(&(PVOID&) o_CreateSwapChainForHwnd, (PVOID) hk_CreateSwapChainForHwnd);

            if (o_CreateSwapChainForCoreWindow != nullptr)
                DetourAttach(&(PVOID&) o_CreateSwapChainForCoreWindow, (PVOID) hk_CreateSwapChainForCoreWindow);

            if (o_CreateSwapChainForComposition != nullptr)
                DetourAttach(&(PVOID&) o_CreateSwapChainForComposition, (PVOID) hk_CreateSwapChainForComposition);
        });

    if (result != NO_ERROR)
    {
        LOG_ERROR("Swapchain creation hooks failed: {}", (int) result);
        return false;
    }

    LOG_INFO("Swapchain creation hooked");
    return true;
}

DWORD WINAPI InitThread(LPVOID)
{
    wchar_t path[MAX_PATH] {};
    GetModuleFileNameW(g_self, path, MAX_PATH);
    g_folder = std::filesystem::path(path).parent_path().wstring();

    AsiLog::Open(g_folder);
    LOG_INFO("OptiFpsOverlay started");

    OverlayConfig::Get().Init(g_folder);

    InstallCursorHooks();
    InstallFactoryHooks();

    return 0;
}
} // namespace

// OptiScaler calls it after loading the plugin, the work starts from DllMain for the loaders that don't
extern "C" __declspec(dllexport) void InitializeASI() {}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        g_self = module;
        dllModule = module;
        DisableThreadLibraryCalls(module);

        // Not under the loader lock
        if (HANDLE thread = CreateThread(nullptr, 0, InitThread, nullptr, 0, nullptr); thread != nullptr)
            CloseHandle(thread);
    }

    return TRUE;
}

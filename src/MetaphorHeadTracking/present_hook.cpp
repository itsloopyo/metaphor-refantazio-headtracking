#include "present_hook.h"

#include <Windows.h>
#include <d3d11.h>
#include <dxgi.h>

#include <atomic>

#include "MinHook.h"
#include "cameraunlock/logging/file_log.h"

namespace metaphor {
namespace {

using PresentFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);

PresentFn g_originalPresent = nullptr;
void* g_presentTarget = nullptr;
std::function<void()> g_onPresent;

// HookedPresent runs on the game's render thread; RemovePresentHook tears the
// callback down from another thread. Gate invocation on an atomic so the render
// thread never reads a half-cleared std::function, and clear the gate before the
// callback is destroyed.
std::atomic<bool> g_presentActive{false};

HRESULT STDMETHODCALLTYPE HookedPresent(IDXGISwapChain* swapChain, UINT syncInterval, UINT flags) {
    if (g_presentActive.load(std::memory_order_acquire) && g_onPresent) {
        g_onPresent();
    }
    return g_originalPresent(swapChain, syncInterval, flags);
}

// Creates a throwaway device + swapchain bound to a hidden message-only-ish
// window solely to read the live IDXGISwapChain vtable, then releases them.
// The vtable layout is process-wide, so the resolved Present pointer is the
// same one the game's real swapchain uses.
void* ResolvePresentFromTempSwapchain() {
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"MetaphorHTTempWnd";
    if (!RegisterClassExW(&wc)) {
        return nullptr;
    }

    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"", WS_OVERLAPPEDWINDOW,
                                0, 0, 64, 64, nullptr, nullptr, wc.hInstance, nullptr);
    if (!hwnd) {
        UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return nullptr;
    }

    DXGI_SWAP_CHAIN_DESC scd = {};
    scd.BufferCount = 1;
    scd.BufferDesc.Width = 64;
    scd.BufferDesc.Height = 64;
    scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.OutputWindow = hwnd;
    scd.SampleDesc.Count = 1;
    scd.Windowed = TRUE;
    scd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    IDXGISwapChain* swapChain = nullptr;
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0 };

    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
        levels, 1, D3D11_SDK_VERSION, &scd, &swapChain, &device, nullptr, &context);

    void* presentPtr = nullptr;
    if (SUCCEEDED(hr) && swapChain) {
        void** vtable = *reinterpret_cast<void***>(swapChain);
        presentPtr = vtable[8];  // IDXGISwapChain::Present
    }

    if (context) context->Release();
    if (device) device->Release();
    if (swapChain) swapChain->Release();
    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);

    return presentPtr;
}

}  // namespace

bool InstallPresentHook(std::function<void()> onPresent) {
    void* presentTarget = ResolvePresentFromTempSwapchain();
    if (!presentTarget) {
        cameraunlock::logging::Line("[present] failed to resolve Present from temp swapchain");
        return false;
    }
    cameraunlock::logging::Line("[present] resolved Present at %p", presentTarget);

    if (MH_CreateHook(presentTarget, reinterpret_cast<void*>(&HookedPresent),
                      reinterpret_cast<void**>(&g_originalPresent)) != MH_OK) {
        cameraunlock::logging::Line("[present] MH_CreateHook failed");
        return false;
    }
    if (MH_EnableHook(presentTarget) != MH_OK) {
        cameraunlock::logging::Line("[present] MH_EnableHook failed");
        return false;
    }

    g_presentTarget = presentTarget;
    g_onPresent = std::move(onPresent);
    g_presentActive.store(true, std::memory_order_release);
    cameraunlock::logging::Line("[present] hook installed");
    return true;
}

void RemovePresentHook() {
    // Stop dispatching before unhooking/destroying the callback.
    g_presentActive.store(false, std::memory_order_release);
    if (g_presentTarget) {
        MH_DisableHook(g_presentTarget);
        MH_RemoveHook(g_presentTarget);
        g_presentTarget = nullptr;
    }
    g_onPresent = nullptr;
}

}  // namespace metaphor

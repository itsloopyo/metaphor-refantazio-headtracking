#pragma once

#include <functional>

namespace metaphor {

// Installs a hook on IDXGISwapChain::Present (vtable index 8) via a throwaway
// D3D11 device/swapchain to read the vtable, then MinHook. The callback fires
// once per presented frame on the render thread. Returns false if the swapchain
// vtable could not be resolved or the hook could not be created.
bool InstallPresentHook(std::function<void()> onPresent);

void RemovePresentHook();

}  // namespace metaphor

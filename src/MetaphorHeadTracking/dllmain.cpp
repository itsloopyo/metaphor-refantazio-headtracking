#include <Windows.h>

#include "mod.h"

namespace {

DWORD WINAPI BootstrapThread(LPVOID) {
    metaphor::ModMain();
    return 0;
}

}  // namespace

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    switch (reason) {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(module);
        // Bootstrap off-thread so the ASI loader's LoadLibrary returns promptly
        // and we are not doing socket/hook work under the loader lock.
        if (HANDLE t = CreateThread(nullptr, 0, BootstrapThread, nullptr, 0, nullptr)) {
            CloseHandle(t);
        }
        break;
    case DLL_PROCESS_DETACH:
        metaphor::ModShutdown();
        break;
    default:
        break;
    }
    return TRUE;
}

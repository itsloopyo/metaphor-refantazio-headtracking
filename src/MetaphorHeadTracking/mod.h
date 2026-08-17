#pragma once

namespace metaphor {

// Bootstrap entry. Spawned on a worker thread from DllMain so the loader's
// LoadLibrary call returns promptly. Opens the log, fingerprints the running
// EXE, starts the UDP receiver + hotkeys, and installs the Present hook.
void ModMain();

// Called from DLL_PROCESS_DETACH to tear hooks/threads down cleanly.
void ModShutdown();

}  // namespace metaphor

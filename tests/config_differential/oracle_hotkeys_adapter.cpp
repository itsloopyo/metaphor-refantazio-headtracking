// Compiled into the hotkey oracle library only, with `cameraunlock` renamed, so the chord guards
// here are the pin's (oracle/core/include/cameraunlock/input/chord_hotkeys.h) and read
// oracle_fake's keyboard.
#include "fake_keyboard.h"

#include "cameraunlock/input/chord_hotkeys.h"
#include "oracle_adapter.h"

#include <functional>
#include <utility>
#include <vector>

namespace metaphor_oracle_view {

FireTable OracleFires(int yawModeKey, bool diagnosticMode) {
    namespace input = cameraunlock::input;
    std::array<int, kActions> fired{};
    auto toggle = [&fired] { ++fired[0]; };
    auto cycle = [&fired] { ++fired[1]; };
    auto yaw = [&fired] { ++fired[2]; };
    auto diag = [&fired] { ++fired[3]; };

    // The registrations of the dev build's SetupHotkeys (src/MetaphorHeadTracking/mod.cpp at
    // 4bca980), in its order, transcribed because that function cannot be compiled apart from the
    // game hooks. Its poller ran a callback when that callback's key went down.
    std::vector<std::pair<int, std::function<void()>>> registered;
    registered.emplace_back(0x23, input::NavGuarded(toggle));     // End
    registered.emplace_back(0x21, input::NavGuarded(cycle));      // Page Up
    registered.emplace_back(yawModeKey, input::NavGuarded(yaw));  // YawModeKey
    registered.emplace_back('Y', input::ChordGuarded(toggle));
    registered.emplace_back('G', input::ChordGuarded(cycle));
    registered.emplace_back('H', input::ChordGuarded(yaw));
    if (diagnosticMode) {
        registered.emplace_back(0x2D, input::NavGuarded(diag));  // Insert
        registered.emplace_back('U', input::ChordGuarded(diag));
    }

    FireTable table;
    table.reserve((kLastKey - kFirstKey + 1) * kHeldStates);
    for (int vk = kFirstKey; vk <= kLastKey; ++vk) {
        for (int held = 0; held < kHeldStates; ++held) {
            fired = {};
            input::FakeHeld() = held;
            for (const auto& r : registered) {
                if (r.first == vk) r.second();
            }
            table.push_back(fired);
        }
    }
    input::FakeHeld() = 0;
    return table;
}

}  // namespace metaphor_oracle_view

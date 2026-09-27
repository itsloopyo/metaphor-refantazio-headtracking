#pragma once

#include <cstdint>
#include <string>

#include "cameraunlock/config/config_owner.h"
#include "cameraunlock/config/config_table.h"
#include "cameraunlock/config/defaults_file.h"
#include "cameraunlock/config/head_tracking_config.h"
#include "cameraunlock/config/legacy_import.h"

namespace metaphor {

constexpr const char* kConfigFileName = "CameraUnlock.ini";
// The file every build before the canonical format read, beside kConfigFileName. Imported once
// while kConfigFileName is absent, and never written.
constexpr const char* kLegacyConfigFileName = "MetaphorHeadTracking.ini";
// The game's name as cameraunlock-core's data/games.json spells it.
constexpr const char* kConfigDisplayName = "Metaphor: ReFantazio";

// The tracker's metres become game units at this scale, then this gain: the dev build shipped
// [Position] Scale=100 and SensitivityX/Y/Z=5.0, which were its boundary conversion rather than a
// player's choice. The gain goes on after the processor's limits, so a limit is in metres of head
// movement.
constexpr float kWorldUnitsPerMetre = 100.0f;
constexpr float kPositionGain = 5.0f;

struct Config : cameraunlock::HeadTrackingConfig {
    // [Diagnostics] Camera mapping modes for development. DumpFollowCam wins over
    // CameraDiscovery when both are set.
    bool dump_follow_cam = false;
    bool camera_discovery = false;
    std::uint32_t inject_hook_rva = 0;
    // [Hotkeys] Runs the diagnostic action in either diagnostic mode, and nothing otherwise.
    std::string diagnostic_key_name = "Insert, Ctrl+Shift+U";
};

// The rows of CameraUnlock.ini. The tracking mode pair and WorldSpaceYaw are Writable: the mode
// and yaw hotkeys save the player's choice, and End changes the session only.
cameraunlock::config::ConfigTable<Config> MakeConfigTable();

// MetaphorHeadTracking.ini as the dev build read it (legacy_config/), mapped into Config.
cameraunlock::config::LegacyImport<Config> MakeLegacyImport();

// The owner's options for the files in `folder` (with its trailing separator): the settings in
// CameraUnlock.ini, imported once from MetaphorHeadTracking.ini. The mod passes
// DefaultsFile::PerUser() and a test a scratch file.
cameraunlock::config::ConfigOwnerOptions<Config> MakeConfigOwnerOptions(const std::wstring& folder,
                                                                        cameraunlock::config::DefaultsFile defaults);

}  // namespace metaphor

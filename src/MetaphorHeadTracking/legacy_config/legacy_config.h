#pragma once

// The config reader of the dev pre-release (4bca980), the only published build that read
// MetaphorHeadTracking.ini, frozen so a player updating from it is converted exactly as that build
// read the file. Nothing in this folder is ever edited. Three things differ from the reader it was
// taken from (LoadConfig in src/MetaphorHeadTracking/mod.cpp at 4bca980): it fills this frozen copy
// of that build's Config and defaults rather than the runtime type, it writes nothing (the build
// wrote its default file when none existed and read it back, which gives these defaults), and it
// reports an absent file apart from one it read. The build's CameraMode is spelled out here as
// its own enum, and SensitivitySettings as plain fields.

#include <cstdint>
#include <vector>

#include "cameraunlock/config/legacy_import.h"

namespace metaphor::legacy {

enum class ReadStatus {
    Read,
    // No file at the path, or none the old reader could open. Config holds the defaults.
    Absent,
};

enum class CameraMode {
    Normal,
    Discovery,
    Dump,
};

struct Config {
    uint16_t port = 4242;
    bool enableOnStartup = true;
    float yaw = 1.0f;
    float pitch = 1.0f;
    float roll = 1.0f;
    bool invertYaw = false;
    bool invertPitch = true;
    bool invertRoll = false;
    float localSmoothing = 0.0f;
    float remoteSmoothing = 0.15f;
    CameraMode cameraMode = CameraMode::Normal;
    uint32_t injectHookRva = 0;
    float positionScale = 100.0f;
    float positionSensX = 5.0f;
    float positionSensY = 5.0f;
    float positionSensZ = 5.0f;
    float positionLimit = 1000.0f;
    bool positionInvertX = false;
    bool positionInvertY = false;
    bool positionInvertZ = true;
    bool worldSpaceYaw = true;
    int yawModeKey = 0x22;  // Page Down
};

// Reads the file at `path`, the ANSI path the dev build opened it by, into a default-constructed
// `c`.
ReadStatus Read(const char* path, Config& c);

// Every section and key Read reads, in the order it reads them.
std::vector<cameraunlock::config::LegacyKey> ReadKeys();

}  // namespace metaphor::legacy

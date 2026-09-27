#pragma once

// The oracle: the config reader and startup code of the dev pre-release (4bca980), the only
// published build, compiled from oracle/ with the core sources they included at its pin
// (3465659). Two libraries build it, each with its namespaces renamed at compile time so it links
// beside the current core: the reader (LoadConfig and the helpers above it in that build's
// src/MetaphorHeadTracking/mod.cpp), and that build's SetupHotkeys registrations against the chord
// guards of its pin. This header names no core type, so the test includes it without the renaming.

#include <array>
#include <string>
#include <vector>

namespace metaphor_oracle_view {

struct OracleConfig {
    int port;
    bool enableOnStartup;
    float yaw, pitch, roll;
    bool invertYaw, invertPitch, invertRoll;
    float localSmoothing, remoteSmoothing;
    int cameraMode;  // 0 Normal, 1 Discovery, 2 Dump
    unsigned injectHookRva;
    float positionScale;
    float positionSensX, positionSensY, positionSensZ;
    float positionLimit;
    bool positionInvertX, positionInvertY, positionInvertZ;
    bool worldSpaceYaw;
    int yawModeKey;
};

// That build's LoadConfig on `path`, as its ModMain ran it. `existed` says whether a file was
// there before the call; without one the build wrote its default file at `path` and read it back.
OracleConfig RunOracle(const std::string& path, bool& existed);

// Which actions a key press fires, for every key a binding can name (0x01-0xFE) under every set
// of held modifiers. Entry (vk - kFirstKey) * kHeldStates + held counts the toggle, cycle, yaw and
// diagnostic actions fired, in that order. held: 1 Ctrl, 2 Shift, 4 Alt.
constexpr int kFirstKey = 0x01;
constexpr int kLastKey = 0xFE;
constexpr int kHeldStates = 8;
constexpr int kActions = 4;
using FireTable = std::vector<std::array<int, kActions>>;

// That build's SetupHotkeys on the yaw mode code, pressing each key under each held set. The
// diagnostic hotkey was registered only outside the normal camera mode.
FireTable OracleFires(int yawModeKey, bool diagnosticMode);

}  // namespace metaphor_oracle_view

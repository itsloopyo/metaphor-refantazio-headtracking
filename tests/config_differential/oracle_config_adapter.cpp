// Compiled into the config oracle library only, with `cameraunlock` and `metaphor` renamed, so the
// core headers here are the pin's (oracle/core) and the reader is the dev build's own lines
// (oracle/src/mod_config_excerpt.inc, lines 51-210 of src/MetaphorHeadTracking/mod.cpp at 4bca980).
#include <Windows.h>

#include <atomic>
#include <fstream>
#include <string>
#include <sys/stat.h>

#include "cameraunlock/config/ini_reader.h"
#include "cameraunlock/data/tracking_pose.h"
#include "cameraunlock/logging/file_log.h"
#include "oracle_adapter.h"

namespace metaphor {

// The dev build's CameraMode, from its src/MetaphorHeadTracking/camera_hook.h, which cannot be
// compiled apart from the game hooks.
enum class CameraMode {
    Normal,
    Discovery,
    Dump,
};

namespace {
std::string g_oraclePath;
}  // namespace

// Stands in for exe_paths.h's ExeRelativePath, the one call the excerpt makes to find the file:
// the test names the file instead of the folder of the running executable.
std::string ExeRelativePath(const char*) { return g_oraclePath; }

namespace {

using cameraunlock::IniReader;
using cameraunlock::SensitivitySettings;

#include "mod_config_excerpt.inc"

}  // namespace
}  // namespace metaphor

namespace metaphor_oracle_view {

OracleConfig RunOracle(const std::string& path, bool& existed) {
    struct _stat64 st;
    existed = _stat64(path.c_str(), &st) == 0;
    metaphor::g_oraclePath = path;
    const auto c = metaphor::LoadConfig();
    OracleConfig o{};
    o.port = c.port;
    o.enableOnStartup = c.enableOnStartup;
    o.yaw = c.sensitivity.yaw;
    o.pitch = c.sensitivity.pitch;
    o.roll = c.sensitivity.roll;
    o.invertYaw = c.sensitivity.invert_yaw;
    o.invertPitch = c.sensitivity.invert_pitch;
    o.invertRoll = c.sensitivity.invert_roll;
    o.localSmoothing = c.localSmoothing;
    o.remoteSmoothing = c.remoteSmoothing;
    o.cameraMode = static_cast<int>(c.cameraMode);
    o.injectHookRva = c.injectHookRva;
    o.positionScale = c.positionScale;
    o.positionSensX = c.positionSensX;
    o.positionSensY = c.positionSensY;
    o.positionSensZ = c.positionSensZ;
    o.positionLimit = c.positionLimit;
    o.positionInvertX = c.positionInvertX;
    o.positionInvertY = c.positionInvertY;
    o.positionInvertZ = c.positionInvertZ;
    o.worldSpaceYaw = c.worldSpaceYaw;
    o.yawModeKey = c.yawModeKey;
    return o;
}

}  // namespace metaphor_oracle_view

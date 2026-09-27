#include "legacy_config/legacy_config.h"

#include <string>

#include "cameraunlock/config/ini_reader.h"
#include "cameraunlock/logging/file_log.h"

namespace metaphor::legacy {
namespace {

using cameraunlock::IniReader;

constexpr int kVkPageDown = 0x22;

// Warned once per process rather than once per load: config is reloadable, and
// repeating this on every reload buries it.
//
// The old value is deliberately NOT migrated into the new keys. The single
// smoothing value carried a hidden 0.15 floor, so the number in an existing
// config does not mean what it used to: copying it across would hand a local
// user smoothing they never chose under the new semantics, and copying it into
// only one of the two keys would be a guess about which connection they were on.
void WarnRetiredSmoothingKey(const IniReader& ini, const char* section, const char* key) {
    static bool warned = false;
    if (warned) return;
    if (ini.ReadString(section, key, "").empty()) return;
    warned = true;
    cameraunlock::logging::Line(
        "Config key [%s] %s has been retired and is IGNORED. Smoothing is now two "
        "keys: LocalSmoothing (default 0, applies to a tracker on this machine) and "
        "RemoteSmoothing (default 0.15, applies to a tracker on the network). The "
        "old value is not migrated because the semantics changed - it carried a "
        "hidden 0.15 floor that no longer exists. Set the two new keys.",
        section, key);
}

}  // namespace

ReadStatus Read(const char* path, Config& cfg) {
    IniReader ini;
    if (!ini.Open(path)) return ReadStatus::Absent;
    int port = ini.ReadInt("General", "UdpPort", cfg.port);
    if (port >= 1024 && port <= 65535) cfg.port = static_cast<uint16_t>(port);
    cfg.enableOnStartup = ini.ReadBool("General", "EnableOnStartup", true);
    cfg.worldSpaceYaw = ini.ReadBool("General", "WorldSpaceYaw", true);
    int yawModeKey = static_cast<int>(ini.ReadHex("Hotkeys", "YawModeKey", kVkPageDown));
    if (yawModeKey >= 0x01 && yawModeKey <= 0xFE) {
        cfg.yawModeKey = yawModeKey;
    } else {
        cameraunlock::logging::Line(
            "[config] YawModeKey 0x%X out of virtual-key range (0x01-0xFE); using default", yawModeKey);
    }
    cfg.yaw = ini.ReadFloat("Sensitivity", "Yaw", 1.0f);
    cfg.pitch = ini.ReadFloat("Sensitivity", "Pitch", 1.0f);
    cfg.roll = ini.ReadFloat("Sensitivity", "Roll", 1.0f);
    cfg.invertYaw = ini.ReadBool("Sensitivity", "InvertYaw", false);
    cfg.invertPitch = ini.ReadBool("Sensitivity", "InvertPitch", true);
    cfg.invertRoll = ini.ReadBool("Sensitivity", "InvertRoll", false);
    float localSmoothing = ini.ReadFloat("Smoothing", "LocalSmoothing", cfg.localSmoothing);
    if (localSmoothing >= 0.0f && localSmoothing <= 1.0f) {
        cfg.localSmoothing = localSmoothing;
    } else {
        cameraunlock::logging::Line(
            "[config] LocalSmoothing %.3f out of range (0.0-1.0); using default %.3f",
            localSmoothing, cfg.localSmoothing);
    }
    float remoteSmoothing = ini.ReadFloat("Smoothing", "RemoteSmoothing", cfg.remoteSmoothing);
    if (remoteSmoothing >= 0.0f && remoteSmoothing <= 1.0f) {
        cfg.remoteSmoothing = remoteSmoothing;
    } else {
        cameraunlock::logging::Line(
            "[config] RemoteSmoothing %.3f out of range (0.0-1.0); using default %.3f",
            remoteSmoothing, cfg.remoteSmoothing);
    }
    WarnRetiredSmoothingKey(ini, "Smoothing", "Factor");
    if (ini.ReadBool("Diagnostics", "DumpFollowCam", false)) {
        cfg.cameraMode = CameraMode::Dump;
    } else if (ini.ReadBool("Discovery", "Enabled", false)) {
        cfg.cameraMode = CameraMode::Discovery;
    }
    cfg.injectHookRva = static_cast<uint32_t>(ini.ReadHex("Inject", "HookRva", 0));
    cfg.positionScale = ini.ReadFloat("Position", "Scale", 100.0f);
    cfg.positionSensX = ini.ReadFloat("Position", "SensitivityX", cfg.positionSensX);
    cfg.positionSensY = ini.ReadFloat("Position", "SensitivityY", cfg.positionSensY);
    cfg.positionSensZ = ini.ReadFloat("Position", "SensitivityZ", cfg.positionSensZ);
    cfg.positionLimit = ini.ReadFloat("Position", "Limit", cfg.positionLimit);
    cfg.positionInvertX = ini.ReadBool("Position", "InvertX", cfg.positionInvertX);
    cfg.positionInvertY = ini.ReadBool("Position", "InvertY", cfg.positionInvertY);
    cfg.positionInvertZ = ini.ReadBool("Position", "InvertZ", cfg.positionInvertZ);
    cameraunlock::logging::Line("[config] loaded from INI (port=%u, cameraMode=%d)",
                                cfg.port, static_cast<int>(cfg.cameraMode));
    return ReadStatus::Read;
}

std::vector<cameraunlock::config::LegacyKey> ReadKeys() {
    return {
        {"General", "UdpPort"},
        {"General", "EnableOnStartup"},
        {"General", "WorldSpaceYaw"},
        {"Hotkeys", "YawModeKey"},
        {"Sensitivity", "Yaw"},
        {"Sensitivity", "Pitch"},
        {"Sensitivity", "Roll"},
        {"Sensitivity", "InvertYaw"},
        {"Sensitivity", "InvertPitch"},
        {"Sensitivity", "InvertRoll"},
        {"Smoothing", "LocalSmoothing"},
        {"Smoothing", "RemoteSmoothing"},
        {"Smoothing", "Factor"},
        {"Diagnostics", "DumpFollowCam"},
        {"Discovery", "Enabled"},
        {"Inject", "HookRva"},
        {"Position", "Scale"},
        {"Position", "SensitivityX"},
        {"Position", "SensitivityY"},
        {"Position", "SensitivityZ"},
        {"Position", "Limit"},
        {"Position", "InvertX"},
        {"Position", "InvertY"},
        {"Position", "InvertZ"},
    };
}

}  // namespace metaphor::legacy

// Nav-cluster virtual key codes.
constexpr int kVkEnd = 0x23;       // toggle tracking
constexpr int kVkPageUp = 0x21;    // cycle tracking mode
constexpr int kVkPageDown = 0x22;  // toggle yaw mode
// Ctrl+Shift chord letters, drawn from the T/Y/U/G/H/J cluster.
constexpr int kVkY = 0x59;
constexpr int kVkG = 0x47;
constexpr int kVkH = 0x48;
constexpr int kVkU = 0x55;
constexpr int kVkJ = 0x4A;
constexpr int kVkInsert = 0x2D;  // restart discovery (discovery mode only)
constexpr int kVkDelete = 0x2E;  // dump decrypted module image

struct Config {
    uint16_t port = 4242;
    bool enableOnStartup = true;
    SensitivitySettings sensitivity;
    // Picked per connection from the packet's source address; both cover
    // rotation and position, and neither is floored.
    float localSmoothing = 0.0f;
    float remoteSmoothing = 0.15f;
    CameraMode cameraMode = CameraMode::Normal;
    uint32_t injectHookRva = 0;
    float positionScale = 100.0f;
    float positionSensX = 5.0f;
    float positionSensY = 5.0f;
    float positionSensZ = 5.0f;
    // Third-person orbit cam: don't box the offset to a neck-sized envelope.
    // Effectively unbounded (well past any real tracker travel); INI-tunable.
    float positionLimit = 1000.0f;
    bool positionInvertX = false;
    bool positionInvertY = false;
    bool positionInvertZ = true;  // Metaphor's forward/back lean reads inverted
    bool worldSpaceYaw = true;
    int yawModeKey = kVkPageDown;
};

void WriteDefaultIni(const std::string& path) {
    std::ofstream f(path, std::ios::out | std::ios::trunc);
    if (!f) {
        cameraunlock::logging::Line("[config] could not write default INI to %s", path.c_str());
        return;
    }
    f << "[General]\n"
      << "UdpPort=4242\n"
      << "EnableOnStartup=true\n"
      << "WorldSpaceYaw=true\n"
      << "\n"
      << "[Sensitivity]\n"
      << "Yaw=1.0\n"
      << "Pitch=1.0\n"
      << "Roll=1.0\n"
      << "InvertYaw=false\n"
      << "InvertPitch=true\n"
      << "InvertRoll=false\n"
      << "\n"
      << "[Smoothing]\n"
      << "; Applied when the tracker runs on this machine (loopback).\n"
      << "; 0 = no smoothing, 1 = heavy. Covers rotation and position.\n"
      << "LocalSmoothing=0.0\n"
      << "; Applied when the tracker is a remote device on the network.\n"
      << "; 0 = no smoothing, 1 = heavy. Covers rotation and position.\n"
      << "RemoteSmoothing=0.15\n"
      << "\n"
      << "[Position]\n"
      << "SensitivityX=5.0\n"
      << "SensitivityY=5.0\n"
      << "SensitivityZ=5.0\n"
      << "InvertX=false\n"
      << "InvertY=false\n"
      << "InvertZ=true\n";
    cameraunlock::logging::Line("[config] wrote default INI to %s", path.c_str());
}

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

Config LoadConfig() {
    Config cfg;
    IniReader ini;
    const std::string iniPath = ExeRelativePath("MetaphorHeadTracking.ini");
    if (!ini.Open(iniPath)) {
        cameraunlock::logging::Line("[config] no INI found, writing defaults");
        WriteDefaultIni(iniPath);
        // Read back the file we just wrote so runtime matches it exactly. If the
        // reopen fails, the ReadBool/ReadFloat defaults below mirror the written
        // values, so the result is identical either way.
        ini.Open(iniPath);
    }
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
    cfg.sensitivity.yaw = ini.ReadFloat("Sensitivity", "Yaw", 1.0f);
    cfg.sensitivity.pitch = ini.ReadFloat("Sensitivity", "Pitch", 1.0f);
    cfg.sensitivity.roll = ini.ReadFloat("Sensitivity", "Roll", 1.0f);
    cfg.sensitivity.invert_yaw = ini.ReadBool("Sensitivity", "InvertYaw", false);
    cfg.sensitivity.invert_pitch = ini.ReadBool("Sensitivity", "InvertPitch", true);
    cfg.sensitivity.invert_roll = ini.ReadBool("Sensitivity", "InvertRoll", false);
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
    return cfg;
}

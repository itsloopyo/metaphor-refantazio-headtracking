#include "mod.h"

#include <Windows.h>
#include <atomic>
#include <fstream>
#include <string>

#include "build_profiles.h"
#include "camera_hook.h"
#include "exe_paths.h"
#include "present_hook.h"
#include "version.h"

#include "cameraunlock/config/ini_reader.h"
#include "cameraunlock/data/tracking_pose.h"
#include "cameraunlock/input/chord_hotkeys.h"
#include "cameraunlock/hooks/hook_manager.h"
#include "cameraunlock/input/hotkey_poller.h"
#include "cameraunlock/logging/file_log.h"
#include "cameraunlock/memory/pe_fingerprint.h"
#include "cameraunlock/protocol/udp_receiver.h"
#include "cameraunlock/time/frame_clock.h"
#include "cameraunlock/tracking/head_tracking_session.h"

namespace metaphor {
namespace {

using cameraunlock::HeadTrackingSession;
using cameraunlock::IniReader;
using cameraunlock::SensitivitySettings;
using cameraunlock::TrackingMode;
using cameraunlock::UdpReceiver;
using cameraunlock::input::ChordGuarded;
using cameraunlock::input::HotkeyPoller;
using cameraunlock::input::NavGuarded;
using cameraunlock::time::FrameClock;

UdpReceiver g_receiver;
HeadTrackingSession<UdpReceiver> g_session(g_receiver);
// Without IsRemoteConnection() on the receiver the session silently falls back
// to LocalSmoothing forever, with nothing at the call site to show it.
static_assert(decltype(g_session)::kHasRemoteConnection,
              "receiver must expose IsRemoteConnection() or remote smoothing never applies");
CameraHook g_camera;
HotkeyPoller g_hotkeys;
FrameClock g_clock;

std::atomic<bool> g_enabled{true};
bool g_initialized = false;

// Nav-cluster virtual key codes.
constexpr int kVkHome = 0x24;      // recenter
constexpr int kVkEnd = 0x23;       // toggle tracking
constexpr int kVkPageUp = 0x21;    // cycle tracking mode
constexpr int kVkPageDown = 0x22;  // toggle yaw mode
// Ctrl+Shift chord letters (T/Y/G/H cluster).
constexpr int kVkT = 0x54;
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

void OnPresentFrame() {
    if (!g_initialized) return;
    float dt = g_clock.Tick();

    g_camera.Tick();  // drives discovery mode; no-op otherwise

    if (!g_enabled.load(std::memory_order_relaxed)) {
        g_camera.SetInjectionActive(false);
        return;
    }

    g_session.Update(dt);

    float yaw, pitch, roll;
    if (g_session.GetRotation(yaw, pitch, roll)) {
        g_camera.ApplyHeadRotation(yaw, pitch, roll);
        float px, py, pz;
        if (g_session.GetPositionOffset(px, py, pz)) {
            g_camera.ApplyHeadPosition(px, py, pz);
        } else {
            g_camera.ApplyHeadPosition(0.0f, 0.0f, 0.0f);
        }
        g_camera.SetInjectionActive(true);
    } else {
        g_camera.SetInjectionActive(false);
    }
}

void CycleTrackingMode() {
    TrackingMode mode = g_session.CycleMode();
    const char* label =
        mode == TrackingMode::RotationAndPosition ? "normal (rotation + position)"
        : mode == TrackingMode::RotationOnly      ? "rotation only (position off)"
                                                  : "position only (rotation off)";
    cameraunlock::logging::Line("[hotkey] tracking mode: %s", label);
}

void SetupHotkeys(int yawModeKey) {
    auto recenter = [] { g_session.Recenter(); cameraunlock::logging::Line("[hotkey] recenter"); };
    auto toggle = [] {
        bool now = !g_enabled.load(std::memory_order_relaxed);
        g_enabled.store(now, std::memory_order_relaxed);
        cameraunlock::logging::Line("[hotkey] tracking %s", now ? "ON" : "OFF");
    };
    auto cycleMode = [] { CycleTrackingMode(); };
    auto toggleYaw = [] { g_camera.ToggleYawMode(); };

    // Nav cluster (guarded so the chord path is the sole Ctrl+Shift trigger).
    g_hotkeys.AddHotkey(kVkHome, NavGuarded(recenter));
    g_hotkeys.AddHotkey(kVkEnd, NavGuarded(toggle));
    g_hotkeys.AddHotkey(kVkPageUp, NavGuarded(cycleMode));
    g_hotkeys.AddHotkey(yawModeKey, NavGuarded(toggleYaw));

    // Ctrl+Shift chord alternatives.
    g_hotkeys.AddHotkey(kVkT, ChordGuarded(recenter));
    g_hotkeys.AddHotkey(kVkY, ChordGuarded(toggle));
    g_hotkeys.AddHotkey(kVkG, ChordGuarded(cycleMode));
    g_hotkeys.AddHotkey(kVkH, ChordGuarded(toggleYaw));

    if (g_camera.Mode() != CameraMode::Normal) {
        auto diag = [] { g_camera.OnDiagnosticHotkey(); };
        g_hotkeys.AddHotkey(kVkInsert, NavGuarded(diag));
        g_hotkeys.AddHotkey(kVkU, ChordGuarded(diag));
    }

    g_hotkeys.Start(16);
}

}  // namespace

void ModMain() {
    cameraunlock::logging::Open(ExeRelativePath(L"MetaphorHeadTracking.log"));
    cameraunlock::logging::Line("=== %s v%s ===", METAPHOR_HT_NAME, METAPHOR_HT_VERSION);
    cameraunlock::logging::Line("[init] DLL attached, bootstrap thread running");

    void* exeBase = GetModuleHandleW(nullptr);
    const BuildProfile* profile = FindMatchingProfile(exeBase);
    if (profile) {
        cameraunlock::logging::Line("[init] matched build profile '%s'", profile->Name);
    } else {
        const BuildProfile& primary = DiagnosticPrimaryProfile();
        cameraunlock::memory::PeFingerprint running{};
        if (cameraunlock::memory::ReadPeFingerprint(exeBase, running)) {
            cameraunlock::logging::Line(
                "[init] running EXE (TDS=0x%08X size=0x%08X chk=0x%08X) matches no known "
                "profile; newest known is '%s'. Mod will run infra but camera stays dormant.",
                running.TimeDateStamp, running.SizeOfImage, running.CheckSum, primary.Name);
        }
    }

    Config cfg = LoadConfig();
    g_enabled.store(cfg.enableOnStartup, std::memory_order_relaxed);
    g_session.GetProcessor().SetSensitivity(cfg.sensitivity);
    cameraunlock::logging::Line(
        "[config] invert yaw=%d pitch=%d roll=%d | worldSpaceYaw=%d | posInvert x=%d y=%d z=%d",
        cfg.sensitivity.invert_yaw, cfg.sensitivity.invert_pitch, cfg.sensitivity.invert_roll,
        cfg.worldSpaceYaw, cfg.positionInvertX, cfg.positionInvertY, cfg.positionInvertZ);
    {
        auto pos = g_session.GetPositionProcessor().GetSettings();
        pos.sensitivity_x = cfg.positionSensX;
        pos.sensitivity_y = cfg.positionSensY;
        pos.sensitivity_z = cfg.positionSensZ;
        pos.limit_x = cfg.positionLimit;
        pos.limit_y = cfg.positionLimit;
        pos.limit_z = cfg.positionLimit;
        pos.limit_z_back = cfg.positionLimit;
        pos.invert_x = cfg.positionInvertX;
        pos.invert_y = cfg.positionInvertY;
        pos.invert_z = cfg.positionInvertZ;
        g_session.GetPositionProcessor().SetSettings(pos);
    }

    // After SetSettings, never before: the session hands both values to the
    // rotation and the position processor, and the connection flag that picks
    // between them is fed from the receiver inside Update().
    g_session.SetLocalSmoothing(cfg.localSmoothing);
    g_session.SetRemoteSmoothing(cfg.remoteSmoothing);

    using cameraunlock::hooks::HookManager;
    using cameraunlock::hooks::HookStatus;
    if (HookManager::Instance().Initialize() != HookStatus::Ok) {
        cameraunlock::logging::Line("[init] MinHook init failed; aborting");
        return;
    }

    g_receiver.SetLog([](const std::string& msg) {
        cameraunlock::logging::Line("[udp] %s", msg.c_str());
    });
    if (g_receiver.Start(cfg.port)) {
        cameraunlock::logging::Line("[init] UDP receiver listening on %u", cfg.port);
    } else {
        cameraunlock::logging::Line("[init] UDP receiver bind pending/retrying on %u", cfg.port);
    }

    g_camera.SetInjectHookRva(cfg.injectHookRva);
    g_camera.SetPositionScale(cfg.positionScale);
    g_camera.Initialize(profile, exeBase, cfg.cameraMode);
    g_camera.SetWorldSpaceYaw(cfg.worldSpaceYaw);

    SetupHotkeys(cfg.yawModeKey);
    cameraunlock::logging::Line("[init] hotkeys registered (Home/End/PgUp/PgDn + Ctrl+Shift T/Y/G/H)");

    if (InstallPresentHook(&OnPresentFrame)) {
        cameraunlock::logging::Line("[init] present hook live; per-frame tick running");
    } else {
        cameraunlock::logging::Line("[init] present hook failed; head tracking will not render");
    }

    g_initialized = true;
    cameraunlock::logging::Line("[init] bootstrap complete (tracking %s)",
                                g_enabled.load() ? "enabled" : "disabled");
}

void ModShutdown() {
    if (!g_initialized) return;
    g_initialized = false;
    RemovePresentHook();
    g_hotkeys.Stop();
    g_receiver.Stop();
    g_camera.Shutdown();
    cameraunlock::hooks::HookManager::Instance().Shutdown();
    cameraunlock::logging::Line("[shutdown] complete");
    cameraunlock::logging::Close();
}

}  // namespace metaphor

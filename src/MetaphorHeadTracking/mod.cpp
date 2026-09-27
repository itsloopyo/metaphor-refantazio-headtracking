#include "mod.h"

#include <Windows.h>
#include <atomic>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

#include "build_profiles.h"
#include "camera_hook.h"
#include "config.h"
#include "exe_paths.h"
#include "present_hook.h"
#include "version.h"

#include "cameraunlock/hooks/hook_manager.h"
#include "cameraunlock/input/hotkey_poller.h"
#include "cameraunlock/input/key_binding_registration.h"
#include "cameraunlock/input/key_bindings.h"
#include "cameraunlock/logging/file_log.h"
#include "cameraunlock/memory/pe_fingerprint.h"
#include "cameraunlock/protocol/udp_receiver.h"
#include "cameraunlock/time/frame_clock.h"
#include "cameraunlock/tracking/head_tracking_session.h"
#include "cameraunlock/tracking/tracking_mode.h"

namespace metaphor {
namespace {

namespace cfg = cameraunlock::config;
using cameraunlock::HeadTrackingSession;
using cameraunlock::TrackingMode;
using cameraunlock::UdpReceiver;
using cameraunlock::input::HotkeyPoller;
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

std::optional<cfg::ConfigOwner<Config>> g_owner;
Config g_config;

// The folder the game's executable runs from, with its trailing separator: where
// MetaphorHeadTracking.ini has always been, and where CameraUnlock.ini goes. Empty when Windows
// reports no path.
std::wstring GameFolder() {
    std::wstring path(32768, L'\0');
    const DWORD len = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (len == 0 || len >= path.size()) return {};
    path.resize(len);
    const size_t slash = path.find_last_of(L"\\/");
    if (slash == std::wstring::npos) return {};
    return path.substr(0, slash + 1);
}

// False when there is no folder to read CameraUnlock.ini from.
bool LoadConfig() {
    const std::wstring folder = GameFolder();
    if (folder.empty()) {
        cameraunlock::logging::Line(
            "[config] Windows reported no path for the game's executable, so there is no folder to read "
            "CameraUnlock.ini from; head tracking does not start.");
        return false;
    }
    cfg::ConfigOwnerOptions<Config> options = MakeConfigOwnerOptions(folder, cfg::DefaultsFile::PerUser());
    // The mod has no overlay, so the player's one-line messages (an import that did not run,
    // Defaults.ini that cannot be read, a save that failed) go to the log.
    options.status_sink = [](const std::string& message) {
        cameraunlock::logging::Line("[config] %s", message.c_str());
    };
    g_owner.emplace(std::move(options));
    const cfg::ConfigLoadResult<Config> loaded = g_owner->Load();
    for (const std::string& line : loaded.log) cameraunlock::logging::Line("[config] %s", line.c_str());
    cameraunlock::logging::Line("[config] %s: %s", kConfigFileName, cfg::ConfigLoadStatusName(loaded.status));
    g_config = loaded.config;
    return true;
}

// Apply-then-save for a toggle: the session already runs on the new value, and a failed save
// leaves it running on it.
void Save(const std::function<void(Config&)>& change, const char* what) {
    const cfg::ConfigSaveResult saved = g_owner->Save(change);
    for (const std::string& line : saved.log) cameraunlock::logging::Line("[config] %s", line.c_str());
    if (saved.status != cfg::ConfigSaveStatus::Saved) {
        cameraunlock::logging::Line("[config] %s not saved (%s): %s", what, cfg::ConfigSaveStatusName(saved.status),
                                    saved.reason.c_str());
    }
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
        // The engine's pitch runs against the tracker's (the dev build shipped InvertPitch=true).
        g_camera.ApplyHeadRotation(yaw, -pitch, roll);
        // Latched, and emitted after the apply so it means what it says. The
        // receiver's own "First UDP packet received" line already proves packets
        // arrived; this one proves a processed pose got as far as the camera
        // hook, which is the other half of the first fork in a no-tracking
        // report (enabled state and session gating sit between the two).
        static bool loggedFirstPose = false;
        if (!loggedFirstPose) {
            loggedFirstPose = true;
            cameraunlock::logging::Line(
                "[udp] first tracker pose handed to the camera hook: "
                "yaw=%.1f pitch=%.1f roll=%.1f (%s sender)",
                yaw, pitch, roll, g_session.IsRemoteConnection() ? "remote" : "local");
        }
        float px, py, pz;
        if (g_session.GetPositionOffset(px, py, pz)) {
            // The shipped position gain, and the engine's forward axis, which runs against the
            // tracker's (the dev build shipped [Position] InvertZ=true).
            g_camera.ApplyHeadPosition(px * kPositionGain, py * kPositionGain, -pz * kPositionGain);
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
    const cameraunlock::TrackingModeChannels channels = cameraunlock::EncodeTrackingMode(mode);
    Save(
        [channels](Config& c) {
            c.rotation_enabled = channels.rotation_enabled;
            c.position_enabled = channels.position_enabled;
        },
        "tracking mode");
}

void ToggleYawMode() {
    const bool world = g_camera.ToggleYawMode();
    Save([world](Config& c) { c.world_space_yaw = world; }, "yaw mode");
}

// The table read every list through the hotkey codec, so a list that does not parse here is a
// bug, not a player's typo.
void RegisterList(const std::string& list, const char* key, std::function<void()> action) {
    const cameraunlock::input::KeyBindingsParseResult parsed = cameraunlock::input::ParseKeyBindings(list);
    if (!parsed.ok()) {
        throw std::logic_error(std::string("[Hotkeys] ") + key + "=" + list + " does not parse: " + parsed.error);
    }
    cameraunlock::input::RegisterKeyBindings(g_hotkeys, parsed.bindings, std::move(action));
}

void SetupHotkeys() {
    // End changes the session only.
    RegisterList(g_config.toggle_key_name, "ToggleKey", [] {
        bool now = !g_enabled.load(std::memory_order_relaxed);
        g_enabled.store(now, std::memory_order_relaxed);
        cameraunlock::logging::Line("[hotkey] tracking %s", now ? "ON" : "OFF");
    });
    RegisterList(g_config.cycle_tracking_mode_key_name, "CycleTrackingModeKey", [] { CycleTrackingMode(); });
    RegisterList(g_config.yaw_mode_key_name, "YawModeKey", [] { ToggleYawMode(); });
    if (g_camera.Mode() != CameraMode::Normal) {
        RegisterList(g_config.diagnostic_key_name, "DiagnosticKey", [] { g_camera.OnDiagnosticHotkey(); });
    }
    g_hotkeys.Start(16);
    cameraunlock::logging::Line("[init] hotkeys: toggle=[%s] cycle mode=[%s] yaw mode=[%s]",
                                g_config.toggle_key_name.c_str(), g_config.cycle_tracking_mode_key_name.c_str(),
                                g_config.yaw_mode_key_name.c_str());
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

    if (!LoadConfig()) return;
    const Config& cfg = g_config;
    g_enabled.store(cfg.enable_on_startup, std::memory_order_relaxed);
    // The table reads a pair that names no mode as its defaults, so the pair always decodes.
    g_session.SetMode(cameraunlock::DecodeTrackingMode(cfg.rotation_enabled, cfg.position_enabled).value());
    // The pose arrives unshaped: the processors keep their identity sensitivity and no inversion.
    g_session.GetPositionProcessor().SetSettings(cfg.position);

    // After SetSettings, never before: the session hands both values to the rotation and the
    // position processor, and the connection flag that picks between them is fed from the
    // receiver inside Update().
    g_session.SetLocalSmoothing(cfg.local_smoothing);
    g_session.SetRemoteSmoothing(cfg.remote_smoothing);

    using cameraunlock::hooks::HookManager;
    using cameraunlock::hooks::HookStatus;
    if (HookManager::Instance().Initialize() != HookStatus::Ok) {
        cameraunlock::logging::Line("[init] MinHook init failed; aborting");
        return;
    }

    g_receiver.SetLog([](const std::string& msg) {
        cameraunlock::logging::Line("[udp] %s", msg.c_str());
    });
    const uint16_t port = static_cast<uint16_t>(cfg.udp_port);
    if (g_receiver.Start(port)) {
        cameraunlock::logging::Line("[init] UDP receiver listening on %u", port);
    } else {
        cameraunlock::logging::Line("[init] UDP receiver bind pending/retrying on %u", port);
    }

    const CameraMode cameraMode = cfg.dump_follow_cam    ? CameraMode::Dump
                                  : cfg.camera_discovery ? CameraMode::Discovery
                                                         : CameraMode::Normal;
    g_camera.SetInjectHookRva(cfg.inject_hook_rva);
    g_camera.Initialize(profile, exeBase, cameraMode);
    g_camera.SetWorldSpaceYaw(cfg.world_space_yaw);

    SetupHotkeys();

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

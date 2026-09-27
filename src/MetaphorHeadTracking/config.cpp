#include "config.h"

#include <cmath>
#include <string>
#include <utility>
#include <vector>

#include "legacy_config/legacy_config.h"

#include "cameraunlock/config/head_tracking_config_table.h"
#include "cameraunlock/config/hotkey_codec.h"
#include "cameraunlock/config/value_codecs.h"
#include "cameraunlock/input/key_bindings.h"

namespace metaphor {

namespace {

namespace cfg = cameraunlock::config;
using cfg::DroppedValue;
using cfg::ImportResult;
using cfg::LegacyFollowsDefaultsIni;
using cfg::LegacyInput;
using cfg::LegacyPoseShaping;
using cfg::PoseShapingValue;
using cfg::schema::Concept;
using cameraunlock::input::KeyBinding;
using cameraunlock::input::KeyModifiers;

// A legacy hotkey code and the Ctrl+Shift chord the dev build always registered beside it, as one
// key list: the code's binding (none for a Ctrl, Shift or Alt key alone, N3), then the chord.
std::string KeyList(int vk, char letter, const char* key, std::vector<DroppedValue>& dropped) {
    const std::string code = cfg::LegacyVirtualKeyToBindings(vk, "Hotkeys", key, dropped);
    const std::string chord = cameraunlock::input::FormatKeyBindings(
        std::vector<KeyBinding>{{KeyModifiers::kCtrl | KeyModifiers::kShift, letter}});
    return code.empty() ? chord : code + ", " + chord;
}

ImportResult Import(const LegacyInput& input, Config& out) {
    // The dev build opened the file by the ANSI path GetModuleFileNameA gave it, which is the
    // owner's ANSI form of the same path.
    legacy::Config c;
    const legacy::ReadStatus read = legacy::Read(input.ansi_path.c_str(), c);
    const legacy::Config shipped;

    std::vector<DroppedValue> dropped;
    std::vector<PoseShapingValue> shaping;

    // The dev build kept the default for a port outside 1024-65535, so the port is always one the
    // canonical row holds.
    out.udp_port = c.port;
    out.enable_on_startup = c.enableOnStartup;
    out.world_space_yaw = c.worldSpaceYaw;

    // The legacy reader already kept both in [0, 1], a non-finite value included.
    out.local_smoothing = c.localSmoothing;
    out.position.local_smoothing = c.localSmoothing;
    out.remote_smoothing = c.remoteSmoothing;
    out.position.remote_smoothing = c.remoteSmoothing;

    // One [Position] Limit bounded the offset on every side after the gain of 5, which now goes on
    // after the limits, so the same bound in metres of head movement is Limit / 5 on each row. The
    // shipped 1000 bounded nothing a tracker reaches; those rows follow Defaults.ini like any
    // untouched row and keep the table's defaults here. A non-finite Limit imports as each row's
    // default (N2), and one outside 0 to 50 has no canonical form, so the owner defers the import.
    if (!(c.positionLimit == shipped.positionLimit)) {
        const float limit = cfg::LegacyFiniteOrDefault(c.positionLimit / kPositionGain, out.position.limit_x,
                                                       "Position", "Limit", dropped);
        const bool finite = std::isfinite(c.positionLimit);
        out.position.limit_x = limit;
        out.position.limit_y = finite ? limit : out.position.limit_y;
        out.position.limit_y_down = finite ? limit : out.position.limit_y_down;
        out.position.limit_z = finite ? limit : out.position.limit_z;
        out.position.limit_z_back = finite ? limit : out.position.limit_z_back;
    }

    // Pose shaping is the tracker's (approved change pose_shaping). The shipped InvertPitch=true,
    // [Position] InvertZ=true, SensitivityX/Y/Z=5.0 and Scale=100 were the boundary conversion and
    // are folded into it (mod.cpp, kPositionGain, kWorldUnitsPerMetre). A value the player changed
    // is dropped.
    const auto shape = [&](auto value, auto ship, const char* section, const char* key) {
        LegacyPoseShaping(value, ship, section, key, shaping, dropped);
    };
    shape(c.yaw, shipped.yaw, "Sensitivity", "Yaw");
    shape(c.pitch, shipped.pitch, "Sensitivity", "Pitch");
    shape(c.roll, shipped.roll, "Sensitivity", "Roll");
    shape(c.invertYaw, shipped.invertYaw, "Sensitivity", "InvertYaw");
    shape(c.invertPitch, shipped.invertPitch, "Sensitivity", "InvertPitch");
    shape(c.invertRoll, shipped.invertRoll, "Sensitivity", "InvertRoll");
    shape(c.positionScale, shipped.positionScale, "Position", "Scale");
    shape(c.positionSensX, shipped.positionSensX, "Position", "SensitivityX");
    shape(c.positionSensY, shipped.positionSensY, "Position", "SensitivityY");
    shape(c.positionSensZ, shipped.positionSensZ, "Position", "SensitivityZ");
    shape(c.positionInvertX, shipped.positionInvertX, "Position", "InvertX");
    shape(c.positionInvertY, shipped.positionInvertY, "Position", "InvertY");
    shape(c.positionInvertZ, shipped.positionInvertZ, "Position", "InvertZ");

    // End and Page Up, with their chords, were fixed in code; only the yaw mode code was a setting.
    out.yaw_mode_key_name = KeyList(c.yawModeKey, 'H', "YawModeKey", dropped);

    out.dump_follow_cam = c.cameraMode == legacy::CameraMode::Dump;
    out.camera_discovery = c.cameraMode == legacy::CameraMode::Discovery;
    out.inject_hook_rva = c.injectHookRva;

    // A row still at what the dev build ran on with no file is no player's choice, so it follows
    // Defaults.ini. The build always started in rotation and position, and had no setting for the
    // toggle or mode hotkeys.
    LegacyFollowsDefaultsIni follows;
    follows.Setting(Concept::UdpPort, c.port, shipped.port);
    follows.Setting(Concept::EnableOnStartup, c.enableOnStartup, shipped.enableOnStartup);
    follows.Setting(Concept::WorldSpaceYaw, c.worldSpaceYaw, shipped.worldSpaceYaw);
    follows.TrackingMode(true);
    follows.Setting(Concept::LocalSmoothing, c.localSmoothing, shipped.localSmoothing);
    follows.Setting(Concept::RemoteSmoothing, c.remoteSmoothing, shipped.remoteSmoothing);
    for (const Concept limit : {Concept::PositionLimitX, Concept::PositionLimitY, Concept::PositionLimitYDown,
                                Concept::PositionLimitZ, Concept::PositionLimitZBack}) {
        follows.Setting(limit, c.positionLimit, shipped.positionLimit);
    }
    follows.NotInLegacy(Concept::ToggleKey);
    follows.NotInLegacy(Concept::CycleTrackingModeKey);
    follows.Setting(Concept::YawModeKey, c.yawModeKey, shipped.yawModeKey);

    return read == legacy::ReadStatus::Absent
               ? ImportResult::Absent(std::move(dropped), std::move(shaping), follows.Concepts())
               : ImportResult::Imported(std::move(dropped), std::move(shaping), follows.Concepts());
}

}  // namespace

cfg::ConfigTable<Config> MakeConfigTable() {
    cfg::ConfigTable<Config> table = cfg::HeadTrackingConfigTable<Config>(
        {Concept::UdpPort, Concept::EnableOnStartup, Concept::WorldSpaceYaw, Concept::RotationEnabled,
         Concept::LocalSmoothing, Concept::RemoteSmoothing, Concept::PositionEnabled, Concept::PositionLimitX,
         Concept::PositionLimitY, Concept::PositionLimitYDown, Concept::PositionLimitZ, Concept::PositionLimitZBack,
         Concept::ToggleKey, Concept::CycleTrackingModeKey, Concept::YawModeKey});
    table.Select(Concept::WorldSpaceYaw).Writable()
        .Select(Concept::RotationEnabled).Writable()
        .Select(Concept::PositionEnabled).Writable();
    // The view moves five times as far as the head (kPositionGain), and the limits bound the head.
    table.Select(Concept::PositionLimitX)
        .Comment("How far, in metres of head movement, leaning left or right can move the view,\n"
                 "which moves five times as far.")
        .Select(Concept::PositionLimitY)
        .Comment("How far, in metres of head movement, raising your head can move the view,\n"
                 "which moves five times as far.")
        .Select(Concept::PositionLimitYDown)
        .Comment("How far, in metres of head movement, lowering your head can move the view,\n"
                 "which moves five times as far.")
        .Select(Concept::PositionLimitZ)
        .Comment("How far, in metres of head movement, leaning forward can move the view,\n"
                 "which moves five times as far.")
        .Select(Concept::PositionLimitZBack)
        .Comment("How far, in metres of head movement, leaning back can move the view,\n"
                 "which moves five times as far.");
    table.Local("Hotkeys", "DiagnosticKey", &Config::diagnostic_key_name, cfg::HotkeyCodec(),
                "Diagnostics: in a diagnostic mode below, restarts the camera discovery or dumps the\n"
                "follow camera to the log. Does nothing in normal play.");
    table.Local("Diagnostics", "DumpFollowCam", &Config::dump_follow_cam, cfg::BoolCodec(),
                "Diagnostics: true hooks the follow camera and dumps it on DiagnosticKey, with no head\n"
                "tracking. Leave off for play.");
    table.Local("Diagnostics", "CameraDiscovery", &Config::camera_discovery, cfg::BoolCodec(),
                "Diagnostics: true runs the camera discovery instead of head tracking. Leave off for play.");
    table.Local("Diagnostics", "InjectHookRva", &Config::inject_hook_rva, cfg::Hex32Codec(),
                "The game function the camera hook runs through. 0x0 = the one mapped for the running build.")
        .Engine();
    return table;
}

cfg::LegacyImport<Config> MakeLegacyImport() {
    return {&Import, legacy::ReadKeys()};
}

cfg::ConfigOwnerOptions<Config> MakeConfigOwnerOptions(const std::wstring& folder, cfg::DefaultsFile defaults) {
    const auto wide = [](const char* name) { return std::wstring(name, name + std::char_traits<char>::length(name)); };
    cfg::ConfigOwnerOptions<Config> options;
    options.path = folder + wide(kConfigFileName);
    options.legacy_path = folder + wide(kLegacyConfigFileName);
    options.table = MakeConfigTable();
    options.import = MakeLegacyImport();
    options.header.display_name = kConfigDisplayName;
    options.defaults = std::move(defaults);
    return options;
}

}  // namespace metaphor

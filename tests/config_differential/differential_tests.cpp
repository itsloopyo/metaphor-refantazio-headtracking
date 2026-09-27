// The config differential test (convert-a-mod-to-the-canonical-config, section 5). Every input
// is read three ways:
//
//   oracle     the reader of the dev pre-release (4bca980), the only published build, with the
//              core sources it compiled at its pin 3465659 (oracle_adapter.h)
//   import     the frozen reader in src/legacy_config/
//   migration  the config owner in a folder holding only MetaphorHeadTracking.ini, the legacy
//              file, importing it into a new CameraUnlock.ini, then the canonical reader and table
//              on that file
//
// Comparison 1, oracle against import, on every input: load status, every field both read
// (floats bit for bit), the startup state, and which actions every key press fires under every
// set of held modifiers. The differences it may find are kComparison1Differences below.
//
// Comparison 2, import against migration, is the proof for the migration: the settings the mod
// starts on are the import's, apart from the approved changes, each of which the import must
// record as dropped. A sensitivity, inversion or scale the player set away from what the dev
// build shipped is dropped (pose_shaping), a non-finite Limit imports as each limit row's default
// (N2), and a yaw mode hotkey on a Ctrl, Shift or Alt key alone imports as unbound (N3), keeping
// its Ctrl+Shift chord. The one [Position] Limit bounded the offset after the shipped gain of 5,
// which now goes on after the limits, so a Limit the player set carries as Limit / 5 on each of
// the five limit rows; one outside 0 to 50 has no canonical form, so the owner defers that import
// and the session runs on what the import gave (kUnrepresentable). The hotkeys fire as the dev
// build fired them apart from N3 (kFleetHotkeyRule).
//
// A row the player never changed from what the dev build ran on with no file follows Defaults.ini:
// the import lists it in follows_defaults_ini and the migration writes it default, the tracking
// mode pair as one unit, and the toggle and mode hotkeys, which the dev build fixed in code,
// always. The test derives that list from what the import read and holds the import's list to it
// on every input; the first-start file and the empty file list every row and migrate to the
// committed file byte for byte. The shipped Limit of 1000 bounded nothing a tracker reaches, so
// an untouched Limit takes Defaults.ini's limits.
//
// Comparison 2 runs twice, once over a Defaults.ini at the built-in values and once over one a
// player changed, where a row the player never changed takes Defaults.ini's value and a changed
// row keeps the player's. After every load MetaphorHeadTracking.ini keeps its bytes, its write time
// and its attributes, Defaults.ini is never written, and the folder holds the legacy file and
// CameraUnlock.ini and nothing else. The next load reads CameraUnlock.ini, imports nothing and
// writes nothing, and a read-only legacy file imports as a writable one does.
//
// The distinct migrated files are written beside the executable under migrated\, for
// lint-migrated.mjs to run core's canonical config lint over.
//
// Inputs: no file, an empty file, the file the dev build wrote on its first start (it shipped no
// config in its ZIPs and seeded none through the launcher), a yaw mode hotkey on each of Ctrl,
// Shift and Alt, a non-finite value in each float the build read, and core's corpus over the
// first-start file.

#include "config.h"
#include "legacy_config/legacy_config.h"
#include "oracle_adapter.h"

#include "cameraunlock/config/config_owner.h"
#include "cameraunlock/config/defaults_file.h"
#include "cameraunlock/config/testing/ini_mutations.h"
#include "cameraunlock/input/key_binding_registration.h"
#include "cameraunlock/input/key_bindings.h"
#include "cameraunlock/tracking/tracking_mode.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace metaphor;

namespace {

// 0cf8c60 (after the dev build) made [Position] Limit reach the downward bound too. The dev build
// set limit_x, limit_y, limit_z and limit_z_back from it and left limit_y_down at PositionSettings'
// 0.20, so the view stopped following the head down at 0.20 of the scaled offset whatever Limit
// said. The reader itself is unchanged since the dev build.
const char* const kComparison1Differences[] = {
    "0cf8c60: [Position] Limit now also sets the downward limit, which the dev build left at 0.20",
};

// The dev build registered each hotkey code guarded against Ctrl and Shift both held, and the
// Ctrl+Shift chord letters beside them, which is how the fleet's key lists fire. The one
// difference is N3: a yaw mode code on a Ctrl, Shift or Alt key alone fired on the way into every
// chord made with it, and imports as unbound.
const char* const kFleetHotkeyRule =
    "a yaw mode code on a Ctrl, Shift or Alt key alone no longer fires (N3); every other press fires as before";

// Limit / 5 on the five limit rows, which take 0 to 10 metres. Core has no rule for a value outside
// a concept's range, so the owner defers such a file: it stays as it is, the session runs on what
// the import read, and nothing is saved.
const char* const kUnrepresentable =
    "a finite [Position] Limit other than the shipped 1000 below 0 or above 50, whose Limit / 5 the canonical "
    "limit rows cannot hold, so the import defers";

constexpr const char* kFileName = "MetaphorHeadTracking.ini";

int g_failures = 0;
int g_checks = 0;

void Check(bool cond, const std::string& what) {
    ++g_checks;
    if (!cond) {
        if (g_failures < 200) std::printf("  FAIL: %s\n", what.c_str());
        ++g_failures;
    }
}

bool SameBits(float a, float b) { return std::memcmp(&a, &b, sizeof a) == 0; }

std::string ReadBytes(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("could not read " + path.string());
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

void WriteBytes(const fs::path& path, const std::string& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!out) throw std::runtime_error("could not write " + path.string());
}

void SetReadOnly(const fs::path& path, bool readOnly) {
    const DWORD attrs = GetFileAttributesW(path.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) throw std::runtime_error("no attributes for " + path.string());
    const DWORD next = readOnly ? (attrs | FILE_ATTRIBUTE_READONLY) : (attrs & ~FILE_ATTRIBUTE_READONLY);
    if (!SetFileAttributesW(path.c_str(), next)) throw std::runtime_error("could not set attributes on " + path.string());
}

using Listing = std::vector<std::pair<std::string, std::string>>;

Listing List(const fs::path& dir) {
    Listing l;
    for (const auto& e : fs::directory_iterator(dir)) {
        l.emplace_back(e.path().filename().string(), ReadBytes(e.path()));
    }
    std::sort(l.begin(), l.end());
    return l;
}

struct Input {
    std::string name;
    std::optional<std::string> bytes;  // nullopt: no file
};

std::string Join(const std::vector<std::string>& v) {
    std::string s;
    for (const std::string& x : v) s += (s.empty() ? "" : ", ") + x;
    return s;
}

// Every field the import reads, against the oracle's field of the same name.
std::vector<std::string> FieldDifferences(const metaphor_oracle_view::OracleConfig& o, const legacy::Config& i) {
    std::vector<std::string> d;
    auto b = [&d](const char* n, bool x, bool y) { if (x != y) d.push_back(n); };
    auto f = [&d](const char* n, float x, float y) { if (!SameBits(x, y)) d.push_back(n); };
    auto n = [&d](const char* name, long long x, long long y) { if (x != y) d.push_back(name); };
    n("port", o.port, i.port);
    b("enableOnStartup", o.enableOnStartup, i.enableOnStartup);
    f("yaw", o.yaw, i.yaw);
    f("pitch", o.pitch, i.pitch);
    f("roll", o.roll, i.roll);
    b("invertYaw", o.invertYaw, i.invertYaw);
    b("invertPitch", o.invertPitch, i.invertPitch);
    b("invertRoll", o.invertRoll, i.invertRoll);
    f("localSmoothing", o.localSmoothing, i.localSmoothing);
    f("remoteSmoothing", o.remoteSmoothing, i.remoteSmoothing);
    n("cameraMode", o.cameraMode, static_cast<int>(i.cameraMode));
    n("injectHookRva", o.injectHookRva, i.injectHookRva);
    f("positionScale", o.positionScale, i.positionScale);
    f("positionSensX", o.positionSensX, i.positionSensX);
    f("positionSensY", o.positionSensY, i.positionSensY);
    f("positionSensZ", o.positionSensZ, i.positionSensZ);
    f("positionLimit", o.positionLimit, i.positionLimit);
    b("positionInvertX", o.positionInvertX, i.positionInvertX);
    b("positionInvertY", o.positionInvertY, i.positionInvertY);
    b("positionInvertZ", o.positionInvertZ, i.positionInvertZ);
    b("worldSpaceYaw", o.worldSpaceYaw, i.worldSpaceYaw);
    n("yawModeKey", o.yawModeKey, i.yawModeKey);
    return d;
}

// The position bounds each build's ModMain handed the processor. The dev build set four of them
// from Limit; the current code sets all five (ApplyPositionLimit).
struct Bounds {
    float x, y, yDown, z, zBack;
};

Bounds DevBounds(float limit) {
    return {limit, limit, 0.20f, limit, limit};
}

// 0cf8c60's ApplyPositionLimit, which the import's startup code ran until the conversion.
Bounds CurrentBounds(float limit) {
    return {limit, limit, limit, limit, limit};
}

// The startup differences between the dev build and the current code on the same values, which
// must be the recorded one: the downward bound, where Limit is not the dev build's 0.20.
std::vector<std::string> BoundsDifferences(const Bounds& dev, const Bounds& now) {
    std::vector<std::string> d;
    if (!SameBits(dev.x, now.x)) d.push_back("limit_x");
    if (!SameBits(dev.y, now.y)) d.push_back("limit_y");
    if (!SameBits(dev.yDown, now.yDown)) d.push_back("limit_y_down");
    if (!SameBits(dev.z, now.z)) d.push_back("limit_z");
    if (!SameBits(dev.zBack, now.zBack)) d.push_back("limit_z_back");
    return d;
}

// The corpus descriptor of every key the frozen reader reads.
std::vector<cameraunlock::config::testing::MutationKey> MutationKeys() {
    using cameraunlock::config::testing::MutationKey;
    auto plain = [](const char* s, const char* k, const char* alt, std::vector<std::string> oor = {}) {
        MutationKey m;
        m.section = s;
        m.key = k;
        m.alternate = alt;
        m.out_of_range = std::move(oor);
        return m;
    };
    MutationKey yawKey = plain("Hotkeys", "YawModeKey", "0x73", {"0x100"});
    yawKey.hotkey = true;
    return {
        plain("General", "UdpPort", "4243", {"1023", "65536"}),
        plain("General", "EnableOnStartup", "false"),
        plain("General", "WorldSpaceYaw", "false"),
        yawKey,
        plain("Sensitivity", "Yaw", "0.5"),
        plain("Sensitivity", "Pitch", "0.5"),
        plain("Sensitivity", "Roll", "0.5"),
        plain("Sensitivity", "InvertYaw", "true"),
        plain("Sensitivity", "InvertPitch", "false"),
        plain("Sensitivity", "InvertRoll", "true"),
        plain("Smoothing", "LocalSmoothing", "0.3", {"-0.5", "1.5"}),
        plain("Smoothing", "RemoteSmoothing", "0.3", {"-0.5", "1.5"}),
        plain("Smoothing", "Factor", "0.3"),
        plain("Diagnostics", "DumpFollowCam", "true"),
        plain("Discovery", "Enabled", "true"),
        plain("Inject", "HookRva", "0x954D70"),
        plain("Position", "Scale", "50"),
        plain("Position", "SensitivityX", "2.0"),
        plain("Position", "SensitivityY", "2.0"),
        plain("Position", "SensitivityZ", "2.0"),
        plain("Position", "Limit", "2.5", {"-1", "51"}),
        plain("Position", "InvertX", "true"),
        plain("Position", "InvertY", "true"),
        plain("Position", "InvertZ", "false"),
    };
}

// One folder per reading under a root of this process's own, emptied before each input so the
// test never holds more than one input's files.
class Scratch {
public:
    Scratch() {
        root_ = fs::temp_directory_path() / ("metaphor-config-differential-" + std::to_string(GetCurrentProcessId()));
        Remove(root_);
        fs::create_directories(root_);
    }
    ~Scratch() { Remove(root_); }
    Scratch(const Scratch&) = delete;
    Scratch& operator=(const Scratch&) = delete;

    fs::path Clean(const std::string& leaf) {
        const fs::path dir = root_ / leaf;
        Remove(dir);
        fs::create_directories(dir);
        return dir;
    }

private:
    // Read-only files included, which remove_all will not delete.
    static void Remove(const fs::path& dir) {
        std::error_code ec;
        if (!fs::exists(dir, ec)) return;
        for (const auto& e : fs::recursive_directory_iterator(dir, ec)) {
            if (e.is_regular_file()) SetFileAttributesW(e.path().c_str(), FILE_ATTRIBUTE_NORMAL);
        }
        fs::remove_all(dir, ec);
        if (ec) throw std::runtime_error("could not empty " + dir.string() + ": " + ec.message());
    }

    fs::path root_;
};

fs::path Place(const fs::path& dir, const Input& input) {
    const fs::path file = dir / kFileName;
    if (input.bytes) WriteBytes(file, *input.bytes);
    return file;
}

struct ImportRun {
    legacy::Config config;
    legacy::ReadStatus status = legacy::ReadStatus::Read;
};

// The import on a read-only copy of the input, which must leave its folder as it found it.
ImportRun RunImport(Scratch& scratch, const Input& input) {
    const fs::path dir = scratch.Clean("import");
    const fs::path file = Place(dir, input);
    if (input.bytes) SetReadOnly(file, true);
    const Listing before = List(dir);
    ImportRun run;
    run.status = legacy::Read(file.string().c_str(), run.config);
    Check(List(dir) == before, input.name + ": the import changed its folder");
    return run;
}

std::string g_firstRun;

ImportRun Comparison1(Scratch& scratch, const Input& input) {
    const fs::path dir = scratch.Clean("oracle");
    const fs::path file = Place(dir, input);
    bool existed = false;
    const metaphor_oracle_view::OracleConfig oracle = metaphor_oracle_view::RunOracle(file.string(), existed);
    if (!input.bytes) {
        // With no file the dev build wrote its default file and read it back.
        Check(!existed && fs::exists(file) && ReadBytes(file) == g_firstRun,
              input.name + ": the dev build's first start did not write data/first-run-4bca980.ini");
    }
    const ImportRun import = RunImport(scratch, input);

    Check(existed == (import.status == legacy::ReadStatus::Read), input.name + ": the import's status is not the oracle's");
    const std::vector<std::string> fields = FieldDifferences(oracle, import.config);
    Check(fields.empty(), input.name + ": fields differ: " + Join(fields));

    const std::vector<std::string> bounds =
        BoundsDifferences(DevBounds(oracle.positionLimit), CurrentBounds(import.config.positionLimit));
    const bool expected = !SameBits(import.config.positionLimit, 0.20f);
    Check(bounds == (expected ? std::vector<std::string>{"limit_y_down"} : std::vector<std::string>{}),
          input.name + ": the startup bounds differ other than as recorded: " + Join(bounds));

    const bool oracleDiag = oracle.cameraMode != 0;
    const bool importDiag = import.config.cameraMode != legacy::CameraMode::Normal;
    Check(metaphor_oracle_view::OracleFires(oracle.yawModeKey, oracleDiag) ==
              metaphor_oracle_view::OracleFires(import.config.yawModeKey, importDiag),
          input.name + ": hotkeys fire differently");
    return import;
}


// ---------------------------------------------------------------------------
// Comparison 2
// ---------------------------------------------------------------------------

namespace cfg = cameraunlock::config;
using cfg::ConfigLoadStatus;
using cfg::DropRule;
using cfg::DroppedValue;
using cfg::ImportResult;
using cfg::ImportStatus;
using cfg::schema::Concept;
using metaphor_oracle_view::FireTable;
using metaphor_oracle_view::kActions;
using metaphor_oracle_view::kFirstKey;
using metaphor_oracle_view::kHeldStates;
using metaphor_oracle_view::kLastKey;

// Ctrl, Shift and Alt, either side or neither, which N3 unbinds.
bool ModifierKey(int vk) { return (vk >= 0x10 && vk <= 0x12) || (vk >= 0xA0 && vk <= 0xA5); }

bool CtrlShiftHeld(int held) { return (held & 3) == 3; }

bool DiagnosticMode(const legacy::Config& l) { return l.cameraMode != legacy::CameraMode::Normal; }

cameraunlock::input::KeyModifiers g_currentHeld = cameraunlock::input::KeyModifiers::kNone;

cameraunlock::input::KeyModifiers CurrentHeld() { return g_currentHeld; }

cameraunlock::input::KeyModifiers ModifiersOf(int held) {
    using cameraunlock::input::KeyModifiers;
    KeyModifiers m = KeyModifiers::kNone;
    if ((held & 1) != 0) m = m | KeyModifiers::kCtrl;
    if ((held & 2) != 0) m = m | KeyModifiers::kShift;
    if ((held & 4) != 0) m = m | KeyModifiers::kAlt;
    return m;
}

// OracleFires' table for the current build. SetupHotkeys parses each key list and hands it to
// RegisterKeyBindings, which puts one detail::GuardKey callback per distinct key on the poller,
// holding that key's bindings in list order; the diagnostic list only in a diagnostic mode. The
// same callbacks are built here with the held modifiers read from the test rather than the
// keyboard, since the poller keeps its callbacks to itself.
FireTable CurrentFires(const metaphor::Config& m) {
    std::array<int, kActions> fired{};
    std::vector<std::pair<int, std::function<void()>>> registered;
    const bool diagnostics = m.dump_follow_cam || m.camera_discovery;
    const std::string* lists[kActions] = {&m.toggle_key_name, &m.cycle_tracking_mode_key_name, &m.yaw_mode_key_name,
                                          &m.diagnostic_key_name};
    for (int action = 0; action < kActions; ++action) {
        if (action == 3 && !diagnostics) continue;
        const cameraunlock::input::KeyBindingsParseResult parsed = cameraunlock::input::ParseKeyBindings(*lists[action]);
        if (!parsed.ok()) throw std::logic_error("migrated hotkey list '" + *lists[action] + "' does not parse");
        std::vector<int> keys;
        std::vector<std::vector<cameraunlock::input::KeyModifiers>> modifiers;
        for (const cameraunlock::input::KeyBinding& b : parsed.bindings) {
            const auto at = std::find(keys.begin(), keys.end(), b.vk);
            if (at == keys.end()) {
                keys.push_back(b.vk);
                modifiers.push_back({b.modifiers});
            } else {
                modifiers[static_cast<std::size_t>(at - keys.begin())].push_back(b.modifiers);
            }
        }
        for (std::size_t i = 0; i < keys.size(); ++i) {
            registered.emplace_back(keys[i], cameraunlock::input::detail::GuardKey(
                                                 std::move(modifiers[i]), [&fired, action] { ++fired[action]; },
                                                 &CurrentHeld));
        }
    }

    FireTable table;
    table.reserve((kLastKey - kFirstKey + 1) * kHeldStates);
    for (int vk = kFirstKey; vk <= kLastKey; ++vk) {
        for (int held = 0; held < kHeldStates; ++held) {
            fired = {};
            g_currentHeld = ModifiersOf(held);
            for (const auto& r : registered) {
                if (r.first == vk) r.second();
            }
            table.push_back(fired);
        }
    }
    g_currentHeld = cameraunlock::input::KeyModifiers::kNone;
    return table;
}

// kFleetHotkeyRule applied to the dev build's codes, built independently of core: each action's
// code fires with Ctrl and Shift not both held, and its chord letter with both held, once per
// press. A yaw mode code on a Ctrl, Shift or Alt key alone is unbound (N3).
FireTable FleetRuleFires(const legacy::Config& l) {
    const int codes[kActions] = {0x23, 0x21, l.yawModeKey, DiagnosticMode(l) ? 0x2D : 0};
    const int letters[kActions] = {'Y', 'G', 'H', DiagnosticMode(l) ? 'U' : 0};
    FireTable table;
    table.reserve((kLastKey - kFirstKey + 1) * kHeldStates);
    for (int vk = kFirstKey; vk <= kLastKey; ++vk) {
        for (int held = 0; held < kHeldStates; ++held) {
            std::array<int, kActions> fired{};
            for (int a = 0; a < kActions; ++a) {
                const bool plain = codes[a] == vk && !ModifierKey(vk) && !CtrlShiftHeld(held);
                const bool chord = letters[a] == vk && CtrlShiftHeld(held);
                if (plain || chord) fired[a] = 1;
            }
            table.push_back(fired);
        }
    }
    return table;
}

std::string FirstFireDifference(const FireTable& expected, const FireTable& got) {
    for (std::size_t i = 0; i < expected.size() && i < got.size(); ++i) {
        if (expected[i] != got[i]) {
            char text[160];
            std::snprintf(text, sizeof text, "key 0x%02X held %d fires %d/%d/%d/%d, not %d/%d/%d/%d",
                          static_cast<int>(i / kHeldStates) + kFirstKey, static_cast<int>(i % kHeldStates), got[i][0],
                          got[i][1], got[i][2], got[i][3], expected[i][0], expected[i][1], expected[i][2],
                          expected[i][3]);
            return text;
        }
    }
    return expected.size() == got.size() ? "none" : "the tables differ in size";
}

// Where the dev build's table and the fleet rule's differ, the key pressed is a Ctrl, Shift or Alt
// key N3 unbinds, and nowhere else.
bool DiffersOnlyOnModifierKeys(const FireTable& before, const FireTable& after) {
    if (before.size() != after.size()) return false;
    for (std::size_t i = 0; i < before.size(); ++i) {
        const int vk = static_cast<int>(i / kHeldStates) + kFirstKey;
        if (before[i] != after[i] && !ModifierKey(vk)) return false;
    }
    return true;
}

// A file as the test holds it to: its bytes, its last write time and its attributes.
struct FileStamp {
    std::string bytes;
    FILETIME written{};
    DWORD attributes = 0;

    bool operator==(const FileStamp& o) const {
        return bytes == o.bytes && CompareFileTime(&written, &o.written) == 0 && attributes == o.attributes;
    }
};

FileStamp Stamp(const fs::path& path) {
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) {
        throw std::runtime_error("cannot stat " + path.string());
    }
    FileStamp s;
    s.bytes = ReadBytes(path);
    s.written = data.ftLastWriteTime;
    s.attributes = data.dwFileAttributes;
    return s;
}

// Where Defaults.ini is for each run of comparison 2: at the built-in values, which the first load
// creates, and with the values a player changed, written from it.
fs::path g_builtinDefaults;
fs::path g_alteredDefaults;

cfg::ConfigOwnerOptions<metaphor::Config> OwnerOptions(const fs::path& dir, const fs::path& defaults) {
    return MakeConfigOwnerOptions(dir.wstring() + L"\\", cfg::DefaultsFile::At(defaults.wstring()));
}

// The import with its map, for the values it records.
ImportResult RunMappedImport(Scratch& scratch, const Input& input) {
    const fs::path file = Place(scratch.Clean("mapped"), input);
    metaphor::Config out = MakeConfigTable().defaults();
    return MakeLegacyImport().run(cfg::LegacyInput{file.wstring(), file.string(), false}, out);
}

const DroppedValue* FindDrop(const std::vector<DroppedValue>& dropped, DropRule rule, const char* section,
                             const char* key) {
    for (const DroppedValue& d : dropped) {
        if (d.rule == rule && d.section == section && d.key == key) return &d;
    }
    return nullptr;
}

struct Tally {
    std::string committed;
    std::set<std::string> migrated;
    struct Run {
        int created = 0;
        int imported = 0;
        int deferred = 0;
        int with_default_rows = 0;
        int with_values = 0;
    } builtin, altered;
    int with_pose_shaping_dropped = 0;
    int with_non_finite_dropped = 0;
    int with_modifier_key_dropped = 0;
    int touched = 0;
    int limit_touched = 0;
    int with_hotkey_rule_difference = 0;
};

const std::set<Concept>& AllRows() {
    static const std::set<Concept> all = {
        Concept::UdpPort,           Concept::EnableOnStartup, Concept::WorldSpaceYaw,      Concept::RotationEnabled,
        Concept::PositionEnabled,   Concept::LocalSmoothing,  Concept::RemoteSmoothing,    Concept::PositionLimitX,
        Concept::PositionLimitY,    Concept::PositionLimitYDown, Concept::PositionLimitZ, Concept::PositionLimitZBack,
        Concept::ToggleKey,         Concept::CycleTrackingModeKey, Concept::YawModeKey,
    };
    return all;
}

// The rows the player never changed: each reads as the dev build ran on with no file. One Limit
// gave all five limit rows; the mode and the toggle and mode hotkeys were no setting.
std::set<Concept> UntouchedRows(const legacy::Config& l) {
    const legacy::Config d;
    std::set<Concept> u = {Concept::RotationEnabled, Concept::PositionEnabled, Concept::ToggleKey,
                           Concept::CycleTrackingModeKey};
    const auto row = [&u](bool same, std::initializer_list<Concept> ids) {
        if (same) u.insert(ids.begin(), ids.end());
    };
    row(l.port == d.port, {Concept::UdpPort});
    row(l.enableOnStartup == d.enableOnStartup, {Concept::EnableOnStartup});
    row(l.worldSpaceYaw == d.worldSpaceYaw, {Concept::WorldSpaceYaw});
    row(l.localSmoothing == d.localSmoothing, {Concept::LocalSmoothing});
    row(l.remoteSmoothing == d.remoteSmoothing, {Concept::RemoteSmoothing});
    row(l.positionLimit == d.positionLimit, {Concept::PositionLimitX, Concept::PositionLimitY, Concept::PositionLimitYDown,
                                             Concept::PositionLimitZ, Concept::PositionLimitZBack});
    row(l.yawModeKey == d.yawModeKey, {Concept::YawModeKey});
    return u;
}

std::string Names(const std::set<Concept>& rows) {
    std::string text;
    for (const Concept row : rows) {
        text += (text.empty() ? "" : ", ") + std::string(cfg::schema::kConcepts[static_cast<std::size_t>(row)].name);
    }
    return text.empty() ? "none" : text;
}

// Defaults.ini as a player may have changed it (WriteAlteredDefaults), as the table's values.
metaphor::Config AlteredDefaults() {
    metaphor::Config c = MakeConfigTable().defaults();
    c.udp_port = 4243;
    c.enable_on_startup = false;
    c.world_space_yaw = false;
    c.rotation_enabled = true;
    c.position_enabled = false;
    c.local_smoothing = 0.3f;
    c.remote_smoothing = 0.3f;
    c.position.limit_x = 0.5f;
    c.position.limit_y = 0.5f;
    c.position.limit_y_down = 0.5f;
    c.position.limit_z = 0.5f;
    c.position.limit_z_back = 0.5f;
    c.toggle_key_name = "F1, Ctrl+Shift+Y";
    c.cycle_tracking_mode_key_name = "F2, Ctrl+Shift+G";
    c.yaw_mode_key_name = "F4, Ctrl+Shift+H";
    return c;
}

// What the session runs on, derived from the frozen reader's values independently of the map:
// Defaults.ini's value on each row the import left to it, the approved changes applied to the
// rest.
metaphor::Config Expected(const legacy::Config& l, const std::set<Concept>& follows, const metaphor::Config& defaults) {
    const metaphor::Config table = MakeConfigTable().defaults();
    metaphor::Config e = defaults;
    const auto keep = [&follows](Concept id) { return follows.count(id) == 0; };
    if (keep(Concept::UdpPort)) e.udp_port = l.port;
    if (keep(Concept::EnableOnStartup)) e.enable_on_startup = l.enableOnStartup;
    if (keep(Concept::WorldSpaceYaw)) e.world_space_yaw = l.worldSpaceYaw;
    if (keep(Concept::LocalSmoothing)) e.local_smoothing = l.localSmoothing;
    if (keep(Concept::RemoteSmoothing)) e.remote_smoothing = l.remoteSmoothing;
    if (keep(Concept::PositionLimitX)) {
        const bool finite = std::isfinite(l.positionLimit);
        const float limit = l.positionLimit / metaphor::kPositionGain;
        e.position.limit_x = finite ? limit : table.position.limit_x;
        e.position.limit_y = finite ? limit : table.position.limit_y;
        e.position.limit_y_down = finite ? limit : table.position.limit_y_down;
        e.position.limit_z = finite ? limit : table.position.limit_z;
        e.position.limit_z_back = finite ? limit : table.position.limit_z_back;
    }
    if (keep(Concept::YawModeKey)) {
        const std::string code = ModifierKey(l.yawModeKey) ? "" : cfg::LegacyVirtualKeyToBindings(l.yawModeKey);
        e.yaw_mode_key_name = code.empty() ? "Ctrl+Shift+H" : code + ", Ctrl+Shift+H";
    }
    e.dump_follow_cam = l.cameraMode == legacy::CameraMode::Dump;
    e.camera_discovery = l.cameraMode == legacy::CameraMode::Discovery;
    e.inject_hook_rva = l.injectHookRva;
    return e;
}

std::vector<std::string> ConfigDifferences(const metaphor::Config& e, const metaphor::Config& m) {
    std::vector<std::string> d;
    if (m.udp_port != e.udp_port) d.push_back("UdpPort");
    if (m.enable_on_startup != e.enable_on_startup) d.push_back("EnableOnStartup");
    if (m.world_space_yaw != e.world_space_yaw) d.push_back("WorldSpaceYaw");
    if (m.rotation_enabled != e.rotation_enabled || m.position_enabled != e.position_enabled) d.push_back("tracking mode");
    if (!SameBits(m.local_smoothing, e.local_smoothing)) d.push_back("LocalSmoothing");
    if (!SameBits(m.remote_smoothing, e.remote_smoothing)) d.push_back("RemoteSmoothing");
    if (!SameBits(m.position.limit_x, e.position.limit_x)) d.push_back("PositionLimitX");
    if (!SameBits(m.position.limit_y, e.position.limit_y)) d.push_back("PositionLimitY");
    if (!SameBits(m.position.limit_y_down, e.position.limit_y_down)) d.push_back("PositionLimitYDown");
    if (!SameBits(m.position.limit_z, e.position.limit_z)) d.push_back("PositionLimitZ");
    if (!SameBits(m.position.limit_z_back, e.position.limit_z_back)) d.push_back("PositionLimitZBack");
    if (m.toggle_key_name != e.toggle_key_name) d.push_back("ToggleKey " + m.toggle_key_name);
    if (m.cycle_tracking_mode_key_name != e.cycle_tracking_mode_key_name) d.push_back("CycleTrackingModeKey");
    if (m.yaw_mode_key_name != e.yaw_mode_key_name) d.push_back("YawModeKey " + m.yaw_mode_key_name + " not " + e.yaw_mode_key_name);
    if (m.diagnostic_key_name != e.diagnostic_key_name) d.push_back("DiagnosticKey");
    if (m.dump_follow_cam != e.dump_follow_cam) d.push_back("DumpFollowCam");
    if (m.camera_discovery != e.camera_discovery) d.push_back("CameraDiscovery");
    if (m.inject_hook_rva != e.inject_hook_rva) d.push_back("InjectHookRva");
    // The pose arrives unshaped: what the dev build shipped is folded into mod.cpp.
    const cameraunlock::PositionSettings identity;
    if (!SameBits(m.position.sensitivity_x, identity.sensitivity_x) ||
        !SameBits(m.position.sensitivity_y, identity.sensitivity_y) ||
        !SameBits(m.position.sensitivity_z, identity.sensitivity_z) || m.position.invert_x || m.position.invert_y ||
        m.position.invert_z) {
        d.push_back("position shaping");
    }
    return d;
}

// The hotkeys of the builtin run against the dev build's: exactly the fleet rule, which differs
// from the dev build on a modifier key alone.
std::vector<std::string> HotkeyDifferences(const legacy::Config& l, const metaphor::Config& m, Tally& tally) {
    std::vector<std::string> d;
    const FireTable before = metaphor_oracle_view::OracleFires(l.yawModeKey, DiagnosticMode(l));
    const FireTable rule = FleetRuleFires(l);
    const FireTable after = CurrentFires(m);
    if (rule != after) d.push_back("hotkeys against the fleet rule: " + FirstFireDifference(rule, after));
    if (!DiffersOnlyOnModifierKeys(before, rule)) d.push_back("hotkeys differ from the dev build on a key that is not a modifier");
    if (before != rule) ++tally.with_hotkey_rule_difference;
    return d;
}

bool Unrepresentable(const legacy::Config& l) {
    const legacy::Config d;
    if (!std::isfinite(l.positionLimit) || l.positionLimit == d.positionLimit) return false;
    const float limit = l.positionLimit / metaphor::kPositionGain;
    return limit < 0.0f || limit > 10.0f;
}

// Every pose-shaping value the frozen reader read is listed in its place, folded where it holds
// what the dev build shipped and dropped as PoseShaping where it does not; a non-finite Limit is
// dropped as NonFiniteNumber; a yaw mode code on a modifier key as ModifierKey; and nothing is
// dropped by any other rule.
void CheckDrops(const std::string& name, const legacy::Config& l, const ImportResult& imported, Tally& tally) {
    const legacy::Config shipped;
    struct Read {
        const char* section;
        const char* key;
        bool atShipped;
    };
    const Read reads[] = {
        {"Sensitivity", "Yaw", SameBits(l.yaw, shipped.yaw)},
        {"Sensitivity", "Pitch", SameBits(l.pitch, shipped.pitch)},
        {"Sensitivity", "Roll", SameBits(l.roll, shipped.roll)},
        {"Sensitivity", "InvertYaw", l.invertYaw == shipped.invertYaw},
        {"Sensitivity", "InvertPitch", l.invertPitch == shipped.invertPitch},
        {"Sensitivity", "InvertRoll", l.invertRoll == shipped.invertRoll},
        {"Position", "Scale", SameBits(l.positionScale, shipped.positionScale)},
        {"Position", "SensitivityX", SameBits(l.positionSensX, shipped.positionSensX)},
        {"Position", "SensitivityY", SameBits(l.positionSensY, shipped.positionSensY)},
        {"Position", "SensitivityZ", SameBits(l.positionSensZ, shipped.positionSensZ)},
        {"Position", "InvertX", l.positionInvertX == shipped.positionInvertX},
        {"Position", "InvertY", l.positionInvertY == shipped.positionInvertY},
        {"Position", "InvertZ", l.positionInvertZ == shipped.positionInvertZ},
    };
    Check(imported.pose_shaping.size() == std::size(reads),
          name + ": the import lists " + std::to_string(imported.pose_shaping.size()) + " pose-shaping values, not 13");
    if (imported.pose_shaping.size() != std::size(reads)) return;
    bool anyDropped = false;
    for (size_t k = 0; k < std::size(reads); ++k) {
        const cfg::PoseShapingValue& v = imported.pose_shaping[k];
        const std::string label = std::string("[") + reads[k].section + "] " + reads[k].key;
        Check(v.section == reads[k].section && v.key == reads[k].key, name + ": " + label + " is not listed in its place");
        Check(v.folded == reads[k].atShipped, name + ": " + label + " is " + (v.folded ? "folded" : "dropped") + " wrongly");
        const bool listed = FindDrop(imported.dropped, DropRule::PoseShaping, reads[k].section, reads[k].key) != nullptr;
        Check(listed != reads[k].atShipped,
              name + ": " + label + (listed ? " is dropped at its shipped value" : " is changed and not dropped"));
        if (!reads[k].atShipped) anyDropped = true;
    }
    if (anyDropped) ++tally.with_pose_shaping_dropped;

    const bool nonFinite = FindDrop(imported.dropped, DropRule::NonFiniteNumber, "Position", "Limit") != nullptr;
    Check(nonFinite == !std::isfinite(l.positionLimit), name + ": [Position] Limit dropped as non-finite does not match its value");
    if (nonFinite) ++tally.with_non_finite_dropped;

    const bool modifier = FindDrop(imported.dropped, DropRule::ModifierKey, "Hotkeys", "YawModeKey") != nullptr;
    Check(modifier == ModifierKey(l.yawModeKey), name + ": [Hotkeys] YawModeKey dropped as a modifier key does not match its code");
    if (modifier) ++tally.with_modifier_key_dropped;

    for (const DroppedValue& d : imported.dropped) {
        Check(d.rule == DropRule::PoseShaping || d.rule == DropRule::NonFiniteNumber || d.rule == DropRule::ModifierKey,
              name + ": the import drops [" + d.section + "] " + d.key + " by a rule this map never applies");
    }
}

// Every field the table binds, as the canonical renderer writes it, so two Configs compare whole.
std::string AllValues(const metaphor::Config& c) {
    return cfg::RenderCanonical(MakeConfigTable(), c, {metaphor::kConfigDisplayName});
}

bool Contains(const std::vector<std::string>& lines, const std::string& text) {
    for (const std::string& line : lines) {
        if (line.find(text) != std::string::npos) return true;
    }
    return false;
}

// The first-start file and the empty file: neither holds a value the dev build did not run on
// with no file, so every row follows Defaults.ini and the migration gives the committed file.
bool IsUnedited(const std::string& name) { return name == "empty file" || name == "first-run-4bca980.ini"; }

void Comparison2(Scratch& scratch, const Input& input, const ImportRun& import, const ImportResult* mapped,
                 const fs::path& defaults, Tally& tally) {
    const bool builtin = defaults == g_builtinDefaults;
    Tally::Run& run = builtin ? tally.builtin : tally.altered;
    const std::string name =
        input.name + (builtin ? " (Defaults.ini at the built-in values)" : " (Defaults.ini changed)");

    const fs::path dir = scratch.Clean("migration");
    const fs::path config = dir / metaphor::kConfigFileName;
    const fs::path legacyFile = Place(dir, input);
    const FileStamp defaultsBefore = Stamp(defaults);
    FileStamp legacyBefore;
    if (input.bytes) legacyBefore = Stamp(legacyFile);

    const cfg::ConfigLoadResult<metaphor::Config> loaded =
        cfg::ConfigOwner<metaphor::Config>(OwnerOptions(dir, defaults)).Load();
    const Listing after = List(dir);
    Check(Stamp(defaults) == defaultsBefore, name + ": the load wrote Defaults.ini");
    if (input.bytes) {
        Check(Stamp(legacyFile) == legacyBefore,
              name + ": MetaphorHeadTracking.ini did not keep its bytes, write time and attributes");
    }
    const metaphor::Config defaultsValues = builtin ? MakeConfigTable().defaults() : AlteredDefaults();

    if (!input.bytes) {
        // A fresh install, which follows Defaults.ini.
        ++run.created;
        Check(loaded.status == ConfigLoadStatus::Created, name + ": no file is not Created");
        Check(after == Listing{{metaphor::kConfigFileName, tally.committed}},
              name + ": the folder does not hold CameraUnlock.ini as config/CameraUnlock.ini and nothing else");
        const std::vector<std::string> d = ConfigDifferences(Expected(import.config, AllRows(), defaultsValues), loaded.config);
        Check(d.empty(), name + ": comparison 2: " + Join(d));
        return;
    }

    if (builtin) CheckDrops(name, import.config, *mapped, tally);

    const std::set<Concept> follows(mapped->follows_defaults_ini.begin(), mapped->follows_defaults_ini.end());
    {
        std::vector<std::string> d = ConfigDifferences(Expected(import.config, follows, defaultsValues), loaded.config);
        if (builtin) {
            for (std::string& h : HotkeyDifferences(import.config, loaded.config, tally)) d.push_back(std::move(h));
        }
        Check(d.empty(), name + ": comparison 2: " + Join(d));
    }

    if (Unrepresentable(import.config)) {
        ++run.deferred;
        Check(loaded.status == ConfigLoadStatus::Deferred,
              name + ": " + kUnrepresentable + ", but the load is " + cfg::ConfigLoadStatusName(loaded.status));
        Check(after == Listing{{kFileName, *input.bytes}}, name + ": a deferred import created CameraUnlock.ini or another file");
        Check(loaded.reason.find("cannot be converted") != std::string::npos,
              name + ": the player is not told which value stops the import: " + loaded.reason);
        return;
    }

    ++run.imported;
    Check(loaded.status == ConfigLoadStatus::Migrated,
          name + ": the migration is " + cfg::ConfigLoadStatusName(loaded.status) + ": " + loaded.reason);
    if (loaded.status != ConfigLoadStatus::Migrated) return;
    Check(after.size() == 2 && after[0].first == metaphor::kConfigFileName && after[1].first == kFileName &&
              after[1].second == *input.bytes,
          name + ": the folder does not hold MetaphorHeadTracking.ini and CameraUnlock.ini and nothing else");
    Check(Contains(loaded.log, "created from"), name + ": the log does not say where CameraUnlock.ini came from");
    const std::string migrated = ReadBytes(config);
    tally.migrated.insert(migrated);
    if (migrated.find("=default\r\n") != std::string::npos) ++run.with_default_rows;
    if (migrated != tally.committed) ++run.with_values;
    for (const Concept row : mapped->follows_defaults_ini) {
        const std::string key = cfg::schema::kConcepts[static_cast<std::size_t>(row)].key;
        Check(migrated.find("\r\n" + key + "=default\r\n") != std::string::npos, name + ": " + key + " is not written default");
    }
    if (builtin && IsUnedited(input.name)) {
        Check(migrated == tally.committed, name + ": does not migrate to the committed file");
    }

    // The next launch reads CameraUnlock.ini over the same Defaults.ini, with nothing to report, to
    // the same settings, does not import, and writes neither file.
    {
        const cfg::ConfigLoadResult<metaphor::Config> reread =
            cfg::ConfigOwner<metaphor::Config>(OwnerOptions(dir, defaults)).Load();
        Check(reread.status == ConfigLoadStatus::Canonical && reread.diagnostics.empty(),
              name + ": the next launch does not read CameraUnlock.ini cleanly");
        Check(AllValues(reread.config) == AllValues(loaded.config), name + ": the next launch runs on other settings");
        Check(!Contains(reread.log, "created from"), name + ": the next launch imports again");
        Check(Contains(reread.log, "is left as it was and is not read"),
              name + ": the next launch does not say MetaphorHeadTracking.ini is not read");
        Check(List(dir) == after && Stamp(legacyFile) == legacyBefore && Stamp(defaults) == defaultsBefore,
              name + ": the next launch changed a file");
    }

    // A read-only MetaphorHeadTracking.ini imports as a writable one does and keeps its attribute,
    // bytes and write time.
    if (builtin) {
        const fs::path roDir = scratch.Clean("read-only");
        const fs::path roLegacy = Place(roDir, input);
        SetReadOnly(roLegacy, true);
        const FileStamp roBefore = Stamp(roLegacy);
        const cfg::ConfigLoadResult<metaphor::Config> fromReadOnly =
            cfg::ConfigOwner<metaphor::Config>(OwnerOptions(roDir, defaults)).Load();
        Check(fromReadOnly.status == ConfigLoadStatus::Migrated && AllValues(fromReadOnly.config) == AllValues(loaded.config) &&
                  ReadBytes(roDir / metaphor::kConfigFileName) == migrated,
              name + ": a read-only MetaphorHeadTracking.ini does not import as a writable one does");
        Check(Stamp(roLegacy) == roBefore && (roBefore.attributes & FILE_ATTRIBUTE_READONLY) != 0,
              name + ": a read-only MetaphorHeadTracking.ini did not keep its attribute, bytes and write time");
    }
}

// Defaults.ini as a player may have changed it, from the one the owner created: every value this
// game takes from it differs from the built-in one (AlteredDefaults).
void WriteAlteredDefaults() {
    std::string text = ReadBytes(g_builtinDefaults);
    const std::pair<const char*, const char*> changes[] = {
        {"UdpPort=4242", "UdpPort=4243"},
        {"EnableOnStartup=true", "EnableOnStartup=false"},
        {"WorldSpaceYaw=true", "WorldSpaceYaw=false"},
        {"PositionEnabled=true", "PositionEnabled=false"},
        {"LocalSmoothing=0.0", "LocalSmoothing=0.3"},
        {"RemoteSmoothing=0.15", "RemoteSmoothing=0.3"},
        {"PositionLimitX=0.3", "PositionLimitX=0.5"},
        {"PositionLimitY=0.2", "PositionLimitY=0.5"},
        {"PositionLimitYDown=0.2", "PositionLimitYDown=0.5"},
        {"PositionLimitZ=0.4", "PositionLimitZ=0.5"},
        {"PositionLimitZBack=0.1", "PositionLimitZBack=0.5"},
        {"ToggleKey=End, Ctrl+Shift+Y", "ToggleKey=F1, Ctrl+Shift+Y"},
        {"CycleTrackingModeKey=PageUp, Ctrl+Shift+G", "CycleTrackingModeKey=F2, Ctrl+Shift+G"},
        {"YawModeKey=PageDown, Ctrl+Shift+H", "YawModeKey=F4, Ctrl+Shift+H"},
    };
    for (const auto& [from, to] : changes) {
        const std::string line = std::string("\r\n") + from + "\r\n";
        const size_t at = text.find(line);
        if (at == std::string::npos) throw std::runtime_error(std::string("the created Defaults.ini has no line ") + from);
        text.replace(at + 2, std::strlen(from), to);
    }
    fs::create_directories(g_alteredDefaults.parent_path());
    WriteBytes(g_alteredDefaults, text);
}

std::string Data(const char* name) {
    const std::string bytes = ReadBytes(fs::path(METAPHOR_DIFFERENTIAL_DATA) / name);
    Check(!bytes.empty(), std::string("data/") + name + " is empty");
    return bytes;
}

constexpr const char* kFirstRun = "first-run-4bca980.ini";

std::vector<Input> Inputs() {
    using cameraunlock::config::testing::GenerateIniMutations;
    std::vector<Input> inputs;
    inputs.push_back({"no file", std::nullopt});
    inputs.push_back({"empty file", std::string()});
    inputs.push_back({kFirstRun, g_firstRun});
    // A yaw mode hotkey on Ctrl, Shift or Alt alone, which the dev build fired on the way into
    // every chord.
    for (const char* line : {"YawModeKey=0x10", "YawModeKey=0x11", "YawModeKey=0x12", "YawModeKey=0xA0",
                             "YawModeKey=0xA3", "YawModeKey=0xA5"}) {
        inputs.push_back({std::string("modifier key: ") + line, std::string("[Hotkeys]\r\n") + line + "\r\n"});
    }
    // A float the dev build read as not finite.
    for (const char* line : {"Limit=nan", "Limit=inf", "Limit=-inf", "Scale=nan", "SensitivityX=inf"}) {
        inputs.push_back({std::string("non-finite: ") + line, std::string("[Position]\r\n") + line + "\r\n"});
    }
    for (const char* line : {"LocalSmoothing=nan", "RemoteSmoothing=inf"}) {
        inputs.push_back({std::string("non-finite: ") + line, std::string("[Smoothing]\r\n") + line + "\r\n"});
    }
    for (auto& m : GenerateIniMutations(g_firstRun, legacy::ReadKeys(), MutationKeys())) {
        inputs.push_back({std::string("corpus over ") + kFirstRun + ": " + m.name, std::move(m.bytes)});
    }
    return inputs;
}

}  // namespace

int main() {
    try {
        Scratch scratch;
        g_firstRun = Data(kFirstRun);
        Tally tally;
        tally.committed = ReadBytes(fs::path(METAPHOR_COMMITTED_CONFIG));
        Check(!tally.committed.empty(), "config/CameraUnlock.ini is missing");

        // Each Defaults.ini sits outside the game folder, in a user folder of its own whose parent
        // exists, as the owner requires before it creates the file.
        g_builtinDefaults = scratch.Clean("user-builtin") / "CameraUnlock" / "Defaults.ini";
        g_alteredDefaults = scratch.Clean("user-altered") / "CameraUnlock" / "Defaults.ini";
        {
            const fs::path dir = scratch.Clean("first-load");
            Check(cfg::ConfigOwner<metaphor::Config>(OwnerOptions(dir, g_builtinDefaults)).Load().status ==
                      ConfigLoadStatus::Created,
                  "the first load is not Created");
            Check(fs::exists(g_builtinDefaults), "the first load did not create Defaults.ini");
        }
        WriteAlteredDefaults();

        // Fresh equals upgrade: over Defaults.ini at the built-in values, the file the dev build
        // wrote on its first start imports into a CameraUnlock.ini that is the committed file,
        // which is what a fresh install creates. That build shipped and seeded no config.
        {
            const fs::path dir = scratch.Clean("fresh-equals-upgrade");
            WriteBytes(dir / kFileName, g_firstRun);
            Check(cfg::ConfigOwner<metaphor::Config>(OwnerOptions(dir, g_builtinDefaults)).Load().status ==
                          ConfigLoadStatus::Migrated &&
                      ReadBytes(dir / metaphor::kConfigFileName) == tally.committed,
                  std::string(kFirstRun) + " does not import into the committed file");
        }

        const std::vector<Input> inputs = Inputs();
        std::printf("%zu inputs\n", inputs.size());
        std::printf("comparison 1, the oracle (dev, 4bca980) against the import:\n");
        for (const char* d : kComparison1Differences) std::printf("  recorded difference: %s\n", d);
        for (const Input& input : inputs) {
            const ImportRun import = Comparison1(scratch, input);
            std::optional<ImportResult> mapped;
            if (input.bytes) {
                mapped = RunMappedImport(scratch, input);
                Check(mapped->status == ImportStatus::Imported, input.name + ": the mapped import is not Imported");
                const std::set<Concept> follows(mapped->follows_defaults_ini.begin(), mapped->follows_defaults_ini.end());
                Check(follows.size() == mapped->follows_defaults_ini.size(),
                      input.name + ": follows_defaults_ini names a row twice");
                const std::set<Concept> untouched = UntouchedRows(import.config);
                Check(follows == untouched, input.name + ": follows Defaults.ini " + Names(follows) +
                                                ", but the rows the player never changed are " + Names(untouched));
                if (untouched != AllRows()) ++tally.touched;
                if (!untouched.count(Concept::PositionLimitX)) ++tally.limit_touched;
                if (IsUnedited(input.name)) Check(untouched == AllRows(), input.name + ": a row is changed");
            }
            for (const fs::path& defaults : {g_builtinDefaults, g_alteredDefaults}) {
                Comparison2(scratch, input, import, mapped ? &*mapped : nullptr, defaults, tally);
            }
        }

        std::printf("comparison 2, the import against the migration, %zu distinct files:\n", tally.migrated.size());
        for (const auto& [over, run] : {std::pair<const char*, const Tally::Run*>{"at the built-in values", &tally.builtin},
                                        std::pair<const char*, const Tally::Run*>{"changed", &tally.altered}}) {
            std::printf("  over Defaults.ini %s: %d created, %d imported (%d holding a default row, %d differing from "
                        "the committed file), %d deferred\n",
                        over, run->created, run->imported, run->with_default_rows, run->with_values, run->deferred);
            Check(run->deferred > 0, std::string("no input is deferred over ") + over);
            Check(run->with_default_rows > 0, std::string("no import writes default over ") + over);
            Check(run->with_values > 0, std::string("no import writes a value over ") + over);
        }
        std::printf("  %d with a changed sensitivity, inversion or scale dropped (pose_shaping)\n",
                    tally.with_pose_shaping_dropped);
        std::printf("  %d with a non-finite Limit imported as the limit rows' defaults (N2)\n", tally.with_non_finite_dropped);
        std::printf("  %d with a yaw mode hotkey on a Ctrl, Shift or Alt key alone imported as unbound (N3)\n",
                    tally.with_modifier_key_dropped);
        std::printf("  %d inputs changed a row from the dev build's default, %d of them the Limit\n", tally.touched,
                    tally.limit_touched);
        std::printf("  %d loads whose hotkeys differ from the dev build: %s\n", tally.with_hotkey_rule_difference,
                    kFleetHotkeyRule);
        std::printf("  deferred: %s\n", kUnrepresentable);
        Check(tally.with_pose_shaping_dropped > 0, "no input drops a changed pose-shaping value");
        Check(tally.with_non_finite_dropped > 0, "no input drops a non-finite Limit");
        Check(tally.with_modifier_key_dropped > 0, "no input drops a hotkey on a modifier key");
        Check(tally.touched > 0 && tally.limit_touched > 0,
              "no input changes a row, the Limit among them, which then does not follow Defaults.ini");
        Check(tally.with_hotkey_rule_difference > 0, "no input shows the fleet hotkey rule");
        Check(tally.migrated.count(tally.committed) == 1, "no input migrated to the committed file");

        wchar_t exe[MAX_PATH];
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        const fs::path lintDir = fs::path(exe).parent_path() / "migrated";
        fs::remove_all(lintDir);
        fs::create_directories(lintDir);
        int n = 0;
        for (const std::string& file : tally.migrated) WriteBytes(lintDir / (std::to_string(n++) + ".ini"), file);
    } catch (const std::exception& e) {
        std::printf("  FAIL: threw: %s\n", e.what());
        ++g_failures;
    }

    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}

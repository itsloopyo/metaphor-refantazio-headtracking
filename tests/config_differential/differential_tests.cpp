// The config differential test (convert-a-mod-to-the-canonical-config, section 5). Every input
// is read two ways:
//
//   oracle     the reader of the dev pre-release (4bca980), the only published build, with the
//              core sources it compiled at its pin 3465659 (oracle_adapter.h)
//   import     the frozen reader in src/MetaphorHeadTracking/legacy_config/
//
// Comparison 1, oracle against import, on every input: load status, every field both read
// (floats bit for bit), the startup state, and which actions every key press fires under every
// set of held modifiers. The differences it may find are kComparison1Differences below.
//
// Inputs: no file, an empty file, the file the dev build wrote on its first start (it shipped no
// config in its ZIPs and seeded none through the launcher), a yaw mode hotkey on each of Ctrl,
// Shift and Alt, a non-finite value in each float the build read, and core's corpus over the
// first-start file.

#include "legacy_config/legacy_config.h"
#include "oracle_adapter.h"
#include "position_limits.h"

#include "cameraunlock/config/testing/ini_mutations.h"
#include "cameraunlock/data/position_settings.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
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

Bounds CurrentBounds(float limit) {
    cameraunlock::PositionSettings pos;
    ApplyPositionLimit(pos, limit);
    return {pos.limit_x, pos.limit_y, pos.limit_y_down, pos.limit_z, pos.limit_z_back};
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

        const std::vector<Input> inputs = Inputs();
        std::printf("%zu inputs\n", inputs.size());
        std::printf("comparison 1, the oracle (dev, 4bca980) against the import:\n");
        for (const char* d : kComparison1Differences) std::printf("  recorded difference: %s\n", d);
        for (const Input& input : inputs) Comparison1(scratch, input);
    } catch (const std::exception& e) {
        std::printf("  FAIL: threw: %s\n", e.what());
        ++g_failures;
    }

    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}

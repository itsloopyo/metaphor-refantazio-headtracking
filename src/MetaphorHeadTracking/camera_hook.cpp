#include "camera_hook.h"

#include <Windows.h>

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>

#include "exe_paths.h"

#include "cameraunlock/discovery/camera_discovery.h"
#include "cameraunlock/hooks/hook_manager.h"
#include "cameraunlock/logging/file_log.h"

namespace metaphor {
namespace {

constexpr float kDegToRad = 0.01745329252f;

// Camera-instance field offset confirmed by the dump diff.
constexpr std::uint32_t kEyeOff = 0x60;  // eye position xyz (world)

const char* const kCameraCandidates[] = {
    "Camera@gfw",
    "fldTypeCamera_Follow@fld@app",
    "fldTypeCamera_Free@fld@app",
    "CameraController@btl",
};

using GameFn = std::uintptr_t(__fastcall*)(void*, void*, void*, void*);

// Per-frame follow-camera update (used only to capture the active instance and
// drive the dump/watch diagnostics; it runs too late to inject through).
GameFn g_origFollowUpdate = nullptr;
void* g_followHookTarget = nullptr;
std::atomic<void*> g_followThis{nullptr};
std::atomic<bool> g_dumpRequested{false};

// Eye-writer: the function that stores the camera eye each frame, found via a
// HW write-watch. This is the real render input - rotating the eye around the
// target right after it is written orbits the view with head tracking while the
// game's own camera state (yaw/pitch/player position) stays clean.

// INI override: hook only this candidate RVA (0 = hook all candidates).
std::uint32_t g_injectOverrideRva = 0;

void* g_moduleBase = nullptr;

std::atomic<bool> g_injectReady{false};
std::atomic<float> g_headYawRad{0.0f};
std::atomic<float> g_headPitchRad{0.0f};
std::atomic<float> g_headRollRad{0.0f};
std::atomic<float> g_posX{0.0f};  // meters (right, up, forward)
std::atomic<float> g_posY{0.0f};
std::atomic<float> g_posZ{0.0f};
std::atomic<float> g_posScale{100.0f};  // game world units per meter

// Yaw mode: true = world-space (horizon-locked) yaw, false = camera-local yaw.
std::atomic<bool> g_worldSpaceYaw{true};

void DiscoveryLog(const char* msg) {
    cameraunlock::logging::Line("%s", msg);
}

void StartDiscovery(cameraunlock::discovery::CameraDiscovery& disc, void* exeBase) {
    cameraunlock::discovery::DiscoveryConfig cfg;
    cfg.module = exeBase;
    for (const char* name : kCameraCandidates) {
        cfg.candidate_names.emplace_back(name);
    }
    cfg.probe_frames = 240;
    cfg.instance_size = 512;
    disc.Start(cfg);
    cameraunlock::logging::Line(
        "[camera] discovery started (%zu candidates); be in the OVERWORLD then press "
        "Insert / Ctrl+Shift+U.", cfg.candidate_names.size());
}

// Known camera vtable RVAs (from discovery), used to identify linked camera
// objects pointed to from the follow-cam struct.
struct NamedVtable { const char* name; std::uint32_t rva; };
constexpr NamedVtable kKnownVtables[] = {
    { "gfw::Camera", 0x17E2160 },
    { "fldTypeCamera_Follow", 0x17FDA68 },
    { "fldTypeCamera_Free", 0x17FDB78 },
    { "btl::CameraController", 0x17DCA18 },
};

// SEH-guarded qword read.
bool SafeReadQword(const void* p, std::uint64_t& out) {
    __try {
        out = *reinterpret_cast<const volatile std::uint64_t*>(p);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// SEH-guarded dword read.
bool SafeReadDword(const void* p, std::uint32_t& out) {
    __try {
        out = *reinterpret_cast<const volatile std::uint32_t*>(p);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void DumpInstance(void* inst) {
    if (!inst) {
        cameraunlock::logging::Line("[dump] no camera instance captured yet");
        return;
    }
    auto base = reinterpret_cast<const std::uint8_t*>(inst);
    cameraunlock::logging::Line("[dump] follow camera @ %p", inst);
    for (std::uint32_t off = 0; off < 0x200; off += 4) {
        std::uint32_t raw;
        if (!SafeReadDword(base + off, raw)) break;  // struct ended / unmapped page
        float f;
        std::memcpy(&f, &raw, 4);
        bool plausibleFloat = (f > -100000.0f && f < 100000.0f && (f > 1e-6f || f < -1e-6f));
        if (plausibleFloat || raw != 0) {
            cameraunlock::logging::Line("[dump]   +0x%03X : 0x%08X  %.4f", off, raw, f);
        }
    }

    // Scan struct for pointers to linked camera objects (vtable match).
    auto modBase = reinterpret_cast<std::uintptr_t>(g_moduleBase);
    cameraunlock::logging::Line("[dump] scanning for linked camera pointers...");
    for (std::uint32_t off = 0; off < 0x600; off += 8) {
        std::uint64_t ptr = 0;
        if (!SafeReadQword(base + off, ptr)) break;  // struct ended / unmapped page
        if (ptr < 0x10000 || (ptr & 7)) continue;
        std::uint64_t vt = 0;
        if (!SafeReadQword(reinterpret_cast<void*>(ptr), vt)) continue;
        if (vt < modBase) continue;
        std::uint64_t vtRva = vt - modBase;
        for (const auto& kv : kKnownVtables) {
            if (vtRva == kv.rva) {
                cameraunlock::logging::Line("[dump]   +0x%03X -> %p  [%s instance]", off,
                                            reinterpret_cast<void*>(ptr), kv.name);
            }
        }
    }
    cameraunlock::logging::Line("[dump] --- end ---");
}

// --- Hardware-watchpoint diagnostic (finds the eye writer) ----------------
void* g_vehHandle = nullptr;
std::atomic<std::uint32_t> g_camThreadId{0};
std::atomic<void*> g_watchAddr{nullptr};
std::atomic<bool> g_watchArmed{false};
std::atomic<int> g_watchHits{0};

// Distinct RIPs that accessed the watched address (deduped to avoid log storms;
// the writer fires every frame). Captures both readers and writers.
std::uintptr_t g_seenRips[24] = {};
std::atomic<int> g_seenCount{0};

LONG CALLBACK WatchVeh(EXCEPTION_POINTERS* ep) {
    if (ep->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    if ((ep->ContextRecord->Dr6 & 0xF) == 0) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    std::uintptr_t rip = ep->ContextRecord->Rip;
    int n = g_seenCount.load(std::memory_order_relaxed);
    bool seen = false;
    for (int i = 0; i < n && i < 24; i++) {
        if (g_seenRips[i] == rip) { seen = true; break; }
    }
    if (!seen && n < 24) {
        g_seenRips[n] = rip;
        g_seenCount.store(n + 1, std::memory_order_relaxed);
        std::uintptr_t rva = rip - reinterpret_cast<std::uintptr_t>(g_moduleBase);
        cameraunlock::logging::EmergencyLine(
            "[watch] eye accessed by RIP %p (RVA 0x%llX)", reinterpret_cast<void*>(rip),
            static_cast<unsigned long long>(rva));
    }
    ep->ContextRecord->Dr6 = 0;
    return EXCEPTION_CONTINUE_EXECUTION;
}

void ArmEyeWatch() {
    void* addr = g_watchAddr.load(std::memory_order_relaxed);
    std::uint32_t tid = g_camThreadId.load(std::memory_order_relaxed);
    if (!addr || !tid) {
        cameraunlock::logging::Line("[watch] no camera thread/eye address captured yet");
        return;
    }
    HANDLE th = OpenThread(THREAD_ALL_ACCESS, FALSE, tid);
    if (!th) {
        cameraunlock::logging::Line("[watch] OpenThread failed (%lu)", GetLastError());
        return;
    }
    SuspendThread(th);
    CONTEXT ctx = {};
    ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
    if (GetThreadContext(th, &ctx)) {
        ctx.Dr0 = reinterpret_cast<DWORD64>(addr);
        ctx.Dr7 = (1ull << 0) | (0b11ull << 16) | (0b11ull << 18);  // L0, read/write, 4 bytes
        ctx.Dr6 = 0;
        if (SetThreadContext(th, &ctx)) {
            g_watchHits.store(0, std::memory_order_relaxed);
            g_seenCount.store(0, std::memory_order_relaxed);
            g_watchArmed.store(true, std::memory_order_relaxed);
            cameraunlock::logging::Line("[watch] armed read/write-watch on eye %p (thread %lu)", addr, tid);
        } else {
            cameraunlock::logging::Line("[watch] SetThreadContext failed (%lu)", GetLastError());
        }
    }
    ResumeThread(th);
    CloseHandle(th);
}

// --- Detours --------------------------------------------------------------

std::uintptr_t __fastcall FollowUpdateDetour(void* thisPtr, void* a2, void* a3, void* a4) {
    g_followThis.store(thisPtr, std::memory_order_relaxed);
    if (thisPtr) {
        g_camThreadId.store(GetCurrentThreadId(), std::memory_order_relaxed);
        g_watchAddr.store(reinterpret_cast<std::uint8_t*>(thisPtr) + kEyeOff, std::memory_order_relaxed);
    }
    std::uintptr_t ret = g_origFollowUpdate(thisPtr, a2, a3, a4);
    if (g_dumpRequested.exchange(false, std::memory_order_relaxed)) {
        DumpInstance(thisPtr);
    }
    return ret;
}

// Orbits `point` around `pivot` by head yaw (about world up) and head pitch
// (about the horizontal right axis), horizon-locked. Writes result into out[3].
// The injection calls this with (target, eye) to swing the look-at target around
// a fixed eye, giving a head-attached look.
void OrbitPointAroundPivot(const float* point, const float* pivot, float hYaw, float hPitch,
                           float* out) {
    float vx = point[0] - pivot[0];
    float vy = point[1] - pivot[1];
    float vz = point[2] - pivot[2];

    float cy = std::cos(hYaw), sy = std::sin(hYaw);
    float vx1 = vx * cy - vz * sy;
    float vz1 = vx * sy + vz * cy;
    float vy1 = vy;

    float rx = vz1, rz = -vx1;
    float rl = std::sqrt(rx * rx + rz * rz);
    if (rl > 1e-4f) {
        rx /= rl;
        rz /= rl;
        float cp = std::cos(hPitch), sp = std::sin(hPitch);
        float dot = rx * vx1 + rz * vz1;
        float crossx = -rz * vy1;
        float crossy = rz * vx1 - rx * vz1;
        float crossz = rx * vy1;
        float nvx = vx1 * cp + crossx * sp + rx * dot * (1.0f - cp);
        float nvy = vy1 * cp + crossy * sp;
        float nvz = vz1 * cp + crossz * sp + rz * dot * (1.0f - cp);
        vx1 = nvx; vy1 = nvy; vz1 = nvz;
    }

    out[0] = pivot[0] + vx1;
    out[1] = pivot[1] + vy1;
    out[2] = pivot[2] + vz1;
}

// Rodrigues rotation of v around a unit axis by angle, written to out[3].
void RotateAroundAxis(const float* v, const float* axis, float angle, float* out) {
    float c = std::cos(angle), s = std::sin(angle);
    float ax = axis[0], ay = axis[1], az = axis[2];
    float dot = ax * v[0] + ay * v[1] + az * v[2];
    float cx = ay * v[2] - az * v[1];
    float cy = az * v[0] - ax * v[2];
    float cz = ax * v[1] - ay * v[0];
    out[0] = v[0] * c + cx * s + ax * dot * (1.0f - c);
    out[1] = v[1] * c + cy * s + ay * dot * (1.0f - c);
    out[2] = v[2] * c + cz * s + az * dot * (1.0f - c);
}

// Camera-local yaw orbit: rotates the view direction (target around the eye)
// about the camera's CURRENT up axis for yaw, then about the camera-local right
// axis for pitch (standard YXZ order, applied camera-locally). At extreme pitch
// this produces the leaning/rolling artifact some players prefer, in contrast to
// the horizon-locked world-space path in OrbitPointAroundPivot. Writes the new
// target position into out[3].
// Applies head yaw/pitch in the BASE CAMERA's own frame (the quaternion
// composition baseRot * headLocalRot), producing both the new target and a
// tilted up vector. Yaw rotates about the base camera's effective up axis, pitch
// about its right axis - so when the base camera is pitched (the overworld camera
// looks down at an angle) head yaw banks the up vector, rolling the horizon. This
// roll is the leaning artifact that distinguishes camera-local from the horizon
// -locked world-space path. With a flat base camera the two modes coincide. The
// caller must write outUp back so the look-at builder renders the bank; leaving
// up level (world-space mode) re-levels the horizon and kills the lean.
void RotateViewCameraLocal(const float* eye, const float* tgt, const float* upHint,
                           float hYaw, float hPitch, float* outTgt, float* outUp) {
    float v[3] = { tgt[0] - eye[0], tgt[1] - eye[1], tgt[2] - eye[2] };
    float len = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    float ref[3] = { upHint[0], upHint[1], upHint[2] };
    float rl0 = std::sqrt(ref[0] * ref[0] + ref[1] * ref[1] + ref[2] * ref[2]);

    // right = refUp x forward (same handedness as the world-space right axis).
    float fn[3] = { v[0], v[1], v[2] };
    if (len > 1e-4f) { fn[0] /= len; fn[1] /= len; fn[2] /= len; }
    if (rl0 > 1e-4f) { ref[0] /= rl0; ref[1] /= rl0; ref[2] /= rl0; }
    float r0[3] = { ref[1] * fn[2] - ref[2] * fn[1],
                    ref[2] * fn[0] - ref[0] * fn[2],
                    ref[0] * fn[1] - ref[1] * fn[0] };
    float rl = std::sqrt(r0[0] * r0[0] + r0[1] * r0[1] + r0[2] * r0[2]);

    // Looking near-straight up/down, forward || refUp degenerates the basis; fall
    // back to the horizon-locked path (and a level up) so yaw never blows up.
    if (len < 1e-4f || rl < 1e-4f) {
        OrbitPointAroundPivot(tgt, eye, hYaw, hPitch, outTgt);
        outUp[0] = ref[0]; outUp[1] = ref[1]; outUp[2] = ref[2];
        return;
    }
    r0[0] /= rl; r0[1] /= rl; r0[2] /= rl;

    // u0 = forward x right: the base camera's effective up (tilts with base pitch).
    float u0[3] = { fn[1] * r0[2] - fn[2] * r0[1],
                    fn[2] * r0[0] - fn[0] * r0[2],
                    fn[0] * r0[1] - fn[1] * r0[0] };

    // Yaw about u0, then pitch about the yawed right axis, applied to forward AND
    // up. Negated yaw matches the world-space path's handedness (see ToggleYawMode).
    float fn1[3], r1[3];
    RotateAroundAxis(fn, u0, -hYaw, fn1);
    RotateAroundAxis(r0, u0, -hYaw, r1);
    float fn2[3], u2[3];
    RotateAroundAxis(fn1, r1, hPitch, fn2);
    RotateAroundAxis(u0, r1, hPitch, u2);

    outTgt[0] = eye[0] + fn2[0] * len;
    outTgt[1] = eye[1] + fn2[1] * len;
    outTgt[2] = eye[2] + fn2[2] * len;
    outUp[0] = u2[0]; outUp[1] = u2[1]; outUp[2] = u2[2];
}

// The view/look-at builder (Ghidra-confirmed): when cam[+0x154]==0 it copies the
// SOURCE eye/target from +0x1C0/+0x1D0 into +0x60/+0x70, then builds the look-at
// matrix into the render object at *(cam+0x10). The +0x60 copy is derived, so
// injection must rotate the SOURCE eye (+0x1C0) around the SOURCE target (+0x1D0)
// before this runs, and restore after so game state stays clean. The hook RVA
// is the per-build BuildProfile::EyeWriterRva (0x954D60 on steam-win64-20250401).
constexpr std::uint32_t kEyeSrcOff = 0x1C0;
constexpr std::uint32_t kTgtSrcOff = 0x1D0;
constexpr std::uint32_t kUpSrcOff = 0x1E0;  // source up vector (copied to +0x80)
// Derived camera state the builder writes from the source when cam[+0x154]==0.
// These front-of-struct fields are the game-authoritative "current camera" that
// overworld movement reads to compute camera-relative forward. We rotate the
// source (so the look-at matrix at *(cam+0x10) renders head-tracked) but restore
// these derived copies to clean afterwards, so look stays decoupled from move.
constexpr std::uint32_t kEyeDstOff = 0x60;
constexpr std::uint32_t kTgtDstOff = 0x70;
constexpr std::uint32_t kUpDstOff = 0x80;
GameFn g_origViewBuilder = nullptr;
void* g_viewBuilderTarget = nullptr;

bool PlausibleWorldPoint(const float* p) {
    for (int i = 0; i < 3; i++) {
        float v = p[i];
        if (!(v == v) || v > 1e7f || v < -1e7f) return false;  // reject NaN/garbage
    }
    return true;
}

std::uintptr_t __fastcall ViewBuilderDetour(void* a1, void* a2, void* a3, void* a4) {
    bool modEye = false, modTgt = false, modUp = false;
    float savedEye[3] = {}, savedTgt[3] = {}, savedUp[3] = {};
    float* eye = nullptr;
    float* tgt = nullptr;
    float* up = nullptr;
    if (g_injectReady.load(std::memory_order_relaxed) && a1) {
        float hYaw = g_headYawRad.load(std::memory_order_relaxed);
        float hPitch = g_headPitchRad.load(std::memory_order_relaxed);
        float hRoll = g_headRollRad.load(std::memory_order_relaxed);
        float pX = g_posX.load(std::memory_order_relaxed);
        float pY = g_posY.load(std::memory_order_relaxed);
        float pZ = g_posZ.load(std::memory_order_relaxed);
        eye = reinterpret_cast<float*>(reinterpret_cast<std::uint8_t*>(a1) + kEyeSrcOff);
        tgt = reinterpret_cast<float*>(reinterpret_cast<std::uint8_t*>(a1) + kTgtSrcOff);
        up = reinterpret_cast<float*>(reinterpret_cast<std::uint8_t*>(a1) + kUpSrcOff);

        if (PlausibleWorldPoint(eye) && PlausibleWorldPoint(tgt)) {
            auto saveEye = [&] { if (!modEye) { savedEye[0]=eye[0]; savedEye[1]=eye[1]; savedEye[2]=eye[2]; modEye = true; } };
            auto saveTgt = [&] { if (!modTgt) { savedTgt[0]=tgt[0]; savedTgt[1]=tgt[1]; savedTgt[2]=tgt[2]; modTgt = true; } };
            auto saveUp = [&] { if (!modUp) { savedUp[0]=up[0]; savedUp[1]=up[1]; savedUp[2]=up[2]; modUp = true; } };

            // Yaw/pitch: head-attached look - rotate the view direction (target
            // around the eye). The eye stays where the game/mouse put it; head
            // movement adds to the aim 1:1 (mouse still orbits the base camera).
            if (hYaw != 0.0f || hPitch != 0.0f) {
                saveTgt();
                float out[3];
                if (g_worldSpaceYaw.load(std::memory_order_relaxed)) {
                    // World-space (horizon-locked): yaw about world up, pitch
                    // about the horizontal right axis. Up stays level.
                    OrbitPointAroundPivot(tgt, eye, hYaw, hPitch, out);
                    tgt[0] = out[0]; tgt[1] = out[1]; tgt[2] = out[2];
                } else {
                    // Camera-local: rotate in the base camera frame, banking the
                    // up vector so a pitched base camera leans on head yaw.
                    float upv[3] = { up[0], up[1], up[2] };
                    if (!PlausibleWorldPoint(upv)) { upv[0] = 0; upv[1] = 1; upv[2] = 0; }
                    float upOut[3];
                    RotateViewCameraLocal(eye, tgt, upv, hYaw, hPitch, out, upOut);
                    tgt[0] = out[0]; tgt[1] = out[1]; tgt[2] = out[2];
                    saveUp();
                    up[0] = upOut[0]; up[1] = upOut[1]; up[2] = upOut[2];
                }
            }

            // Position (6DOF): strafe eye+target together along camera-local axes.
            if (pX != 0.0f || pY != 0.0f || pZ != 0.0f) {
                float fwd[3] = { tgt[0] - eye[0], tgt[1] - eye[1], tgt[2] - eye[2] };
                float fl = std::sqrt(fwd[0] * fwd[0] + fwd[1] * fwd[1] + fwd[2] * fwd[2]);
                float upv[3] = { up[0], up[1], up[2] };
                if (!PlausibleWorldPoint(upv)) { upv[0] = 0; upv[1] = 1; upv[2] = 0; }
                if (fl > 1e-4f) {
                    fwd[0] /= fl; fwd[1] /= fl; fwd[2] /= fl;
                    float rgt[3] = { upv[1] * fwd[2] - upv[2] * fwd[1],
                                     upv[2] * fwd[0] - upv[0] * fwd[2],
                                     upv[0] * fwd[1] - upv[1] * fwd[0] };
                    float rl = std::sqrt(rgt[0] * rgt[0] + rgt[1] * rgt[1] + rgt[2] * rgt[2]);
                    if (rl > 1e-4f) { rgt[0] /= rl; rgt[1] /= rl; rgt[2] /= rl; }
                    float s = g_posScale.load(std::memory_order_relaxed);
                    float off[3];
                    for (int i = 0; i < 3; i++)
                        off[i] = (rgt[i] * pX + upv[i] * pY + fwd[i] * pZ) * s;
                    saveEye();
                    saveTgt();
                    for (int i = 0; i < 3; i++) { eye[i] += off[i]; tgt[i] += off[i]; }
                }
            }

            // Roll: tilt the up vector about the forward axis (composes on top of
            // any camera-local bank already applied above).
            if (hRoll != 0.0f && PlausibleWorldPoint(up)) {
                float fwd[3] = { tgt[0] - eye[0], tgt[1] - eye[1], tgt[2] - eye[2] };
                float fl = std::sqrt(fwd[0] * fwd[0] + fwd[1] * fwd[1] + fwd[2] * fwd[2]);
                if (fl > 1e-4f) {
                    fwd[0] /= fl; fwd[1] /= fl; fwd[2] /= fl;
                    saveUp();
                    float upOut[3];
                    RotateAroundAxis(up, fwd, hRoll, upOut);
                    up[0] = upOut[0]; up[1] = upOut[1]; up[2] = upOut[2];
                }
            }
        }
    }

    std::uintptr_t ret = g_origViewBuilder(a1, a2, a3, a4);

    if (modEye && eye) { eye[0] = savedEye[0]; eye[1] = savedEye[1]; eye[2] = savedEye[2]; }
    if (modTgt && tgt) { tgt[0] = savedTgt[0]; tgt[1] = savedTgt[1]; tgt[2] = savedTgt[2]; }
    if (modUp && up) { up[0] = savedUp[0]; up[1] = savedUp[1]; up[2] = savedUp[2]; }

    // Strip head tracking out of the game-authoritative derived camera state so
    // movement/aim read clean values - the look-at matrix is already built (and
    // head-tracked) at this point, so the view still renders rotated while the
    // overworld movement system (which reads these derived eye/target fields)
    // stays decoupled from where the head is looking.
    if ((modEye || modTgt || modUp) && a1) {
        auto* base = reinterpret_cast<std::uint8_t*>(a1);
        if (modEye) std::memcpy(base + kEyeDstOff, savedEye, sizeof(savedEye));
        if (modTgt) std::memcpy(base + kTgtDstOff, savedTgt, sizeof(savedTgt));
        if (modUp)  std::memcpy(base + kUpDstOff, savedUp, sizeof(savedUp));
    }
    return ret;
}

bool HookFn(void* exeBase, std::uint32_t rva, void* detour, void** orig, void*& targetOut,
            const char* what) {
    using cameraunlock::hooks::HookManager;
    using cameraunlock::hooks::HookStatus;
    void* target = reinterpret_cast<std::uint8_t*>(exeBase) + rva;
    HookStatus st = HookManager::Instance().CreateHook(target, detour, orig);
    if (st != HookStatus::Ok) {
        cameraunlock::logging::Line("[camera] failed to hook %s (RVA 0x%X): %s", what, rva,
                                    HookStatusToString(st));
        return false;
    }
    if (HookManager::Instance().EnableHook(target) != HookStatus::Ok) {
        cameraunlock::logging::Line("[camera] failed to enable %s hook", what);
        return false;
    }
    targetOut = target;
    cameraunlock::logging::Line("[camera] hooked %s at %p (RVA 0x%X)", what, target, rva);
    return true;
}

}  // namespace

CameraHook::CameraHook() = default;
CameraHook::~CameraHook() = default;

void CameraHook::SetInjectHookRva(std::uint32_t rva) {
    g_injectOverrideRva = rva;
}

bool CameraHook::Initialize(const BuildProfile* profile, void* exeModuleBase, CameraMode mode) {
    m_exeBase = exeModuleBase;
    m_mode = mode;

    if (mode == CameraMode::Discovery) {
        m_discovery = std::make_unique<cameraunlock::discovery::CameraDiscovery>();
        m_discovery->SetLogCallback(&DiscoveryLog);
        StartDiscovery(*m_discovery, exeModuleBase);
        return false;
    }

    if (mode == CameraMode::Dump) {
        g_moduleBase = exeModuleBase;
        g_vehHandle = AddVectoredExceptionHandler(1, &WatchVeh);
        HookFn(exeModuleBase, 0x589060u, reinterpret_cast<void*>(&FollowUpdateDetour),
               reinterpret_cast<void**>(&g_origFollowUpdate), g_followHookTarget, "follow-update (dump)");
        cameraunlock::logging::Line(
            "[camera] dump mode: Insert / Ctrl+Shift+U dumps layout AND arms the eye-writer watch");
        return false;
    }

    // Normal mode: head-tracked eye orbit.
    if (!profile) {
        cameraunlock::logging::Line(
            "[camera] running EXE matches no known build profile; camera work dormant");
        return false;
    }
    if (profile->CameraUpdateRva == 0 || profile->EyeWriterRva == 0) {
        cameraunlock::logging::Line(
            "[camera] profile '%s' recognised but camera not yet mapped; camera work dormant",
            profile->Name);
        return false;
    }

    // Follow-update hook captures the active camera instance each frame.
    if (!HookFn(exeModuleBase, profile->CameraUpdateRva, reinterpret_cast<void*>(&FollowUpdateDetour),
                reinterpret_cast<void**>(&g_origFollowUpdate), g_followHookTarget, "follow-update")) {
        return false;
    }
    // Inject hook: rotate the source eye before the view/look-at builder runs.
    std::uint32_t rva = g_injectOverrideRva != 0 ? g_injectOverrideRva : profile->EyeWriterRva;
    if (!HookFn(exeModuleBase, rva, reinterpret_cast<void*>(&ViewBuilderDetour),
                reinterpret_cast<void**>(&g_origViewBuilder), g_viewBuilderTarget, "view-builder (inject)")) {
        return false;
    }
    cameraunlock::logging::Line("[camera] profile '%s' active: eye-source orbit via RVA 0x%X",
                                profile->Name, rva);
    m_active = true;
    return true;
}

void CameraHook::OnDiagnosticHotkey() {
    if (m_mode == CameraMode::Discovery && m_discovery) {
        m_discovery->Cleanup();
        m_discoveryDone = false;
        cameraunlock::logging::Line("[camera] discovery RESTART requested");
        StartDiscovery(*m_discovery, m_exeBase);
    } else if (m_mode == CameraMode::Dump) {
        g_dumpRequested.store(true, std::memory_order_relaxed);
        cameraunlock::logging::Line("[dump] dump requested");
        ArmEyeWatch();
    }
}

void CameraHook::Tick() {
    if (m_mode != CameraMode::Discovery || m_discoveryDone || !m_discovery) return;

    using cameraunlock::discovery::Phase;
    Phase phase = m_discovery->Advance();
    if (phase == Phase::Complete) {
        const auto& off = m_discovery->GetOffsets();
        cameraunlock::logging::Line(
            "[camera] DISCOVERY COMPLETE: vfunc=%p instance=%p yaw@0x%zX pitch@0x%zX valid=%d",
            m_discovery->GetActiveVfuncTarget(), m_discovery->GetInstancePointer(),
            off.yaw_offset, off.pitch_offset, off.valid ? 1 : 0);
        m_discoveryDone = true;
    } else if (phase == Phase::Failed) {
        cameraunlock::logging::Line("[camera] DISCOVERY FAILED (see DISC lines above)");
        m_discoveryDone = true;
    }
}

void CameraHook::ApplyHeadRotation(float yaw, float pitch, float roll) {
    g_headYawRad.store(yaw * kDegToRad, std::memory_order_relaxed);
    g_headPitchRad.store(pitch * kDegToRad, std::memory_order_relaxed);
    g_headRollRad.store(-roll * kDegToRad, std::memory_order_relaxed);  // engine roll is inverted
}

void CameraHook::ApplyHeadPosition(float x, float y, float z) {
    g_posX.store(x, std::memory_order_relaxed);
    g_posY.store(y, std::memory_order_relaxed);
    g_posZ.store(z, std::memory_order_relaxed);
}

void CameraHook::SetPositionScale(float scale) {
    if (scale > 0.0f) g_posScale.store(scale, std::memory_order_relaxed);
}

void CameraHook::SetWorldSpaceYaw(bool world) {
    g_worldSpaceYaw.store(world, std::memory_order_relaxed);
}

bool CameraHook::IsWorldSpaceYaw() const {
    return g_worldSpaceYaw.load(std::memory_order_relaxed);
}

void CameraHook::ToggleYawMode() {
    bool world = !g_worldSpaceYaw.load(std::memory_order_relaxed);
    g_worldSpaceYaw.store(world, std::memory_order_relaxed);
    cameraunlock::logging::Line("[hotkey] yaw mode: %s",
                                world ? "world-space (horizon-locked)" : "camera-local");
}

void CameraHook::SetInjectionActive(bool active) {
    if (!m_active) return;
    if (!active) {
        g_headYawRad.store(0.0f, std::memory_order_relaxed);
        g_headPitchRad.store(0.0f, std::memory_order_relaxed);
        g_headRollRad.store(0.0f, std::memory_order_relaxed);
        g_posX.store(0.0f, std::memory_order_relaxed);
        g_posY.store(0.0f, std::memory_order_relaxed);
        g_posZ.store(0.0f, std::memory_order_relaxed);
    }
    g_injectReady.store(active, std::memory_order_relaxed);
}

void CameraHook::Shutdown() {
    g_injectReady.store(false, std::memory_order_relaxed);
    g_watchArmed.store(false, std::memory_order_relaxed);
    if (g_vehHandle) {
        RemoveVectoredExceptionHandler(g_vehHandle);
        g_vehHandle = nullptr;
    }
    if (m_discovery) {
        m_discovery->Cleanup();
        m_discovery.reset();
    }
    auto& hm = cameraunlock::hooks::HookManager::Instance();
    if (g_viewBuilderTarget) { hm.DisableHook(g_viewBuilderTarget); hm.RemoveHook(g_viewBuilderTarget); g_viewBuilderTarget = nullptr; }
    if (g_followHookTarget) { hm.DisableHook(g_followHookTarget); hm.RemoveHook(g_followHookTarget); g_followHookTarget = nullptr; }
    m_active = false;
}

}  // namespace metaphor

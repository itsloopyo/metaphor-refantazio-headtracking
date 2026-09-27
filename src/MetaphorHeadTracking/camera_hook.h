#pragma once

#include <cstdint>
#include <memory>

#include "build_profiles.h"

namespace cameraunlock::discovery { class CameraDiscovery; }

namespace metaphor {

enum class CameraMode {
    Normal,     // inject head tracking (once offsets are mapped)
    Discovery,  // run the shared CameraDiscovery state machine
    Dump,       // hook the known per-frame camera fn and dump its layout on demand
};

// Camera injection surface plus opt-in discovery/dump diagnostics.
//
// Normal mode injects head rotation once a build profile's camera offsets are
// mapped; until then it stays dormant and ApplyHeadRotation() is a no-op.
//
// Discovery mode ([Diagnostics] CameraDiscovery=true) runs the shared CameraDiscovery
// state machine to locate the per-frame camera update and guess its angle
// fields.
//
// Dump mode ([Diagnostics] DumpFollowCam=true) hooks the confirmed overworld
// follow-camera update function and, on DiagnosticKey, dumps
// the camera object's float layout to the log. Capturing two dumps at different
// in-game camera orientations identifies the real yaw/pitch/roll fields (the
// discovery heuristic mis-guesses them).
class CameraHook {
public:
    CameraHook();
    ~CameraHook();

    // Overrides the view-builder hook RVA used for injection (0 keeps the
    // default). Lets candidate functions be A/B tested from CameraUnlock.ini
    // without a rebuild. Call before Initialize.
    void SetInjectHookRva(std::uint32_t rva);

    bool Initialize(const BuildProfile* profile, void* exeModuleBase, CameraMode mode);

    bool IsActive() const { return m_active; }
    CameraMode Mode() const { return m_mode; }

    // DiagnosticKey action: restart discovery (Discovery mode) or request a
    // layout dump on the next camera frame (Dump mode).
    void OnDiagnosticHotkey();

    // Called once per presented frame. Drives discovery; no-op otherwise.
    void Tick();

    // Stores the processed head rotation (degrees) for the camera-update detour
    // to apply. Call every frame. No-op until the camera is hooked.
    void ApplyHeadRotation(float yaw, float pitch, float roll);

    // Stores the processed head position offset (meters: x=right, y=up,
    // z=forward) for the camera-update detour to apply. Call every frame.
    void ApplyHeadPosition(float x, float y, float z);

    // Yaw mode: true = world-space (horizon-locked) yaw, false = camera-local
    // yaw. Initialized from [General] WorldSpaceYaw at startup.
    void SetWorldSpaceYaw(bool world);
    bool IsWorldSpaceYaw() const;

    // Flips the yaw mode at runtime (YawModeKey), logs the new state and returns
    // it: true for world-space.
    bool ToggleYawMode();

    // Enables/disables injection (mirrors the tracking-enabled toggle). Disabling
    // zeroes the offset so the view returns to the game's normal follow camera.
    void SetInjectionActive(bool active);

    void Shutdown();

private:
    bool m_active = false;
    CameraMode m_mode = CameraMode::Normal;
    void* m_exeBase = nullptr;
    std::unique_ptr<cameraunlock::discovery::CameraDiscovery> m_discovery;
    bool m_discoveryDone = false;
};

}  // namespace metaphor

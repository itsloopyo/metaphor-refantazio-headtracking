#pragma once

#include <cstdint>
#include "cameraunlock/memory/pe_fingerprint.h"

namespace metaphor {

// A per-build offset profile. The PE fingerprint is the authoritative routing
// key; RVAs are pinned to a specific shipped METAPHOR.exe build. Append-only:
// when a patch breaks RVAs, ADD a new profile (never edit an existing one) so
// users on the old build keep matching their old profile. See the doctrine
// "Maintain compatibility across new patches".
struct BuildProfile {
    const char* Name;                          // e.g. "steam-win64-20241011"
    cameraunlock::memory::PeFingerprint Fingerprint;
    // RVAs/offsets are filled in once camera discovery lands. CameraUpdateRva
    // zero = unknown/not yet mapped; the mod stays dormant for camera work
    // until it is populated.
    std::uint32_t CameraUpdateRva;             // per-frame follow-camera update fn (module-relative)
    std::uint32_t YawFieldOffset;              // byte offset of yaw (radians) in the camera instance
    std::uint32_t PitchFieldOffset;            // byte offset of pitch (radians)
    std::uint32_t EyeWriterRva;                // fn that writes the camera eye (the real render input)
};

// Returns the profile matching the running EXE, or nullptr if none matches.
const BuildProfile* FindMatchingProfile(void* exeModuleBase);

// The diagnostic-primary profile (newest build the mod knows about). Used to
// word the "your game build is newer/older than this mod knows" log line.
const BuildProfile& DiagnosticPrimaryProfile();

}  // namespace metaphor

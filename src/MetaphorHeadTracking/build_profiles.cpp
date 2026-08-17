#include "build_profiles.h"

namespace metaphor {

using cameraunlock::memory::PeFingerprint;
using cameraunlock::memory::ReadPeFingerprint;

// Steam x64 build, EXE TimeDateStamp 0x67EBD812 (2025-04-01), SizeOfImage
// 0x14D49000, CheckSum 0x143FE34F. Camera mapped via in-game discovery + dump:
// fldTypeCamera_Follow::vfunc[6] at RVA 0x589060 is the per-frame overworld
// follow-camera update; yaw (radians) sits at instance +0x130, pitch at +0x134
// (FOV +0x140, distance +0x13C, aspect +0x14C confirm the struct).
extern const BuildProfile kSteamProfile_20250401 = {
    "steam-win64-20250401",
    PeFingerprint{ 0x67EBD812u, 0x14D49000u, 0x143FE34Fu },
    0x589060u,
    0x130u,
    0x134u,
    0x954D60u,  // eye-writer fn (found via HW write-watch on eye +0x60)
};

// Append-only. Newest build first (the diagnostic primary).
static const BuildProfile* const kKnownProfiles[] = {
    &kSteamProfile_20250401,
};

const BuildProfile* FindMatchingProfile(void* exeModuleBase) {
    PeFingerprint running{};
    if (!ReadPeFingerprint(exeModuleBase, running)) {
        return nullptr;
    }
    for (const BuildProfile* profile : kKnownProfiles) {
        if (profile->Fingerprint.Matches(running)) {
            return profile;
        }
    }
    return nullptr;
}

const BuildProfile& DiagnosticPrimaryProfile() {
    return *kKnownProfiles[0];
}

}  // namespace metaphor

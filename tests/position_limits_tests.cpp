// Locks the [Position] Limit key onto every bound the processor clamps against.
//
// A missed bound is silent in game: the camera still moves along the right axis
// and only the travel is wrong, so nothing about a playtest catches it. This mod
// runs SensitivityY=5, which turns a forgotten limit_y_down into the view
// refusing to follow the head down past a few centimetres.

#include <cstdio>

#include "position_limits.h"

#include "cameraunlock/data/position_settings.h"
#include "cameraunlock/math/vec3.h"
#include "cameraunlock/processing/position_processor.h"

using cameraunlock::PositionProcessor;
using cameraunlock::PositionSettings;
using cameraunlock::math::Vec3;

namespace {

int g_failures = 0;

void Check(bool cond, const char* name) {
    if (cond) {
        std::printf("[ PASS ] %s\n", name);
    } else {
        std::printf("[ FAIL ] %s\n", name);
        ++g_failures;
    }
}

Vec3 ClampWith(float limit, const Vec3& value) {
    PositionSettings settings;
    metaphor::ApplyPositionLimit(settings, limit);
    PositionProcessor processor;
    processor.SetSettings(settings);
    return processor.ClampToLimits(value);
}

}  // namespace

int main() {
    PositionSettings wide;
    metaphor::ApplyPositionLimit(wide, 1000.0f);
    Check(wide.limit_x == 1000.0f && wide.limit_y == 1000.0f && wide.limit_y_down == 1000.0f
              && wide.limit_z == 1000.0f && wide.limit_z_back == 1000.0f,
          "Limit reaches all five bounds, limit_y_down included");

    // The struct default for limit_y_down is 0.20, so the shipped envelope was
    // [-0.20, +1000] until Limit was applied to it as well.
    const Vec3 down = ClampWith(1000.0f, Vec3(0.0f, -5.0f, 0.0f));
    Check(down.y == -5.0f, "5m of downward travel survives a Limit of 1000");

    const Vec3 up = ClampWith(1000.0f, Vec3(0.0f, 5.0f, 0.0f));
    Check(up.y == 5.0f, "5m of upward travel survives a Limit of 1000");

    const Vec3 tightDown = ClampWith(0.05f, Vec3(0.0f, -1.0f, 0.0f));
    Check(tightDown.y == -0.05f, "a Limit of 0.05 clamps downward travel to 0.05m");

    const Vec3 tightUp = ClampWith(0.05f, Vec3(0.0f, 1.0f, 0.0f));
    Check(tightUp.y == 0.05f, "a Limit of 0.05 clamps upward travel to 0.05m");

    if (g_failures == 0) {
        std::printf("\nAll position-limit tests passed.\n");
        return 0;
    }
    std::printf("\n%d position-limit test(s) FAILED.\n", g_failures);
    return 1;
}

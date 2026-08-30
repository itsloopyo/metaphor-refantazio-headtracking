#pragma once

#include "cameraunlock/data/position_settings.h"

namespace metaphor {

// The INI exposes a single [Position] Limit for the whole envelope, so it has to
// reach every bound the processor clamps against. limit_y_down carries its own
// 0.20m default: leaving it unset pinned downward travel there while Limit widened
// every other bound, and at SensitivityY=5 the head only has to drop about 4cm
// before the view stops following it.
inline void ApplyPositionLimit(cameraunlock::PositionSettings& settings, float limit) {
    settings.limit_x = limit;
    settings.limit_y = limit;
    settings.limit_y_down = limit;
    settings.limit_z = limit;
    settings.limit_z_back = limit;
}

}  // namespace metaphor

// Untrusted-input boundary tests for the OpenTrack UDP packet parser.
//
// The receiver binds INADDR_ANY, so any host on the network can send these
// packets. These tests pin the parser's rejection of malformed and hostile
// input (short packets, NaN/inf doubles, and finite doubles that overflow the
// float range to +/-inf) so a regression there can't silently poison the
// camera math with NaN.

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>

#include "cameraunlock/protocol/opentrack_packet.h"
#include "cameraunlock/data/position_data.h"
#include "cameraunlock/data/tracking_pose.h"

using cameraunlock::OpenTrackPacket;
using cameraunlock::PositionData;
using cameraunlock::TrackingPose;

namespace {

int g_failures = 0;

void Check(bool cond, const std::string& name) {
    if (cond) {
        std::printf("[ PASS ] %s\n", name.c_str());
    } else {
        std::printf("[ FAIL ] %s\n", name.c_str());
        ++g_failures;
    }
}

// Build a canonical 48-byte OpenTrack packet: doubles at
// [x, y, z, yaw, pitch, roll] (positions in cm, angles in degrees).
void MakePacket(uint8_t buf[48], double x, double y, double z,
                double yaw, double pitch, double roll) {
    std::memcpy(buf + OpenTrackPacket::kPosXOffset, &x, sizeof(double));
    std::memcpy(buf + OpenTrackPacket::kPosYOffset, &y, sizeof(double));
    std::memcpy(buf + OpenTrackPacket::kPosZOffset, &z, sizeof(double));
    std::memcpy(buf + OpenTrackPacket::kYawOffset, &yaw, sizeof(double));
    std::memcpy(buf + OpenTrackPacket::kPitchOffset, &pitch, sizeof(double));
    std::memcpy(buf + OpenTrackPacket::kRollOffset, &roll, sizeof(double));
}

}  // namespace

int main() {
    const double inf = std::numeric_limits<double>::infinity();
    const double nan = std::numeric_limits<double>::quiet_NaN();

    // 1. Valid packet parses and converts cm -> m for position.
    {
        uint8_t buf[48];
        MakePacket(buf, 100.0 /*cm*/, -50.0, 25.0, 10.0, -20.0, 5.0);
        TrackingPose pose;
        PositionData pos;
        bool ok = OpenTrackPacket::TryParseAll(buf, sizeof(buf), pose, pos);
        Check(ok, "valid packet parses");
        Check(ok && std::fabs(pose.yaw - 10.0f) < 1e-4f, "yaw preserved");
        Check(ok && std::fabs(pose.roll - 5.0f) < 1e-4f, "roll preserved");
        Check(ok && std::fabs(pos.x - 1.0f) < 1e-4f, "position cm->m converted");
    }

    // 2. Short packets are rejected (no out-of-bounds read past the buffer).
    {
        uint8_t buf[47] = {};
        TrackingPose pose;
        PositionData pos;
        Check(!OpenTrackPacket::TryParseAll(buf, sizeof(buf), pose, pos),
              "47-byte packet rejected");
        Check(!OpenTrackPacket::TryParse(buf, 0, pose), "zero-length rejected");
        Check(!OpenTrackPacket::TryParse(nullptr, 48, pose), "null data rejected");
    }

    // 3. NaN/inf in rotation fields is rejected (would poison sin/cos).
    {
        uint8_t buf[48];
        MakePacket(buf, 0, 0, 0, nan, 0, 0);
        TrackingPose pose;
        Check(!OpenTrackPacket::TryParse(buf, sizeof(buf), pose), "NaN yaw rejected");

        MakePacket(buf, 0, 0, 0, 0, inf, 0);
        Check(!OpenTrackPacket::TryParse(buf, sizeof(buf), pose), "inf pitch rejected");
    }

    // 4. A finite double that overflows the float range narrows to inf and must
    //    be rejected (the regression the FiniteFloat guard exists for).
    {
        uint8_t buf[48];
        MakePacket(buf, 0, 0, 0, 1e300 /*finite double, > FLT_MAX*/, 0, 0);
        TrackingPose pose;
        Check(!OpenTrackPacket::TryParse(buf, sizeof(buf), pose),
              "finite double overflowing float rejected");
    }

    // 5. NaN/inf in position fields is rejected by TryParseAll.
    {
        uint8_t buf[48];
        MakePacket(buf, nan, 0, 0, 0, 0, 0);
        TrackingPose pose;
        PositionData pos;
        Check(!OpenTrackPacket::TryParseAll(buf, sizeof(buf), pose, pos),
              "NaN position rejected by TryParseAll");
    }

    if (g_failures == 0) {
        std::printf("\nAll packet-parser tests passed.\n");
        return 0;
    }
    std::printf("\n%d packet-parser test(s) FAILED.\n", g_failures);
    return 1;
}

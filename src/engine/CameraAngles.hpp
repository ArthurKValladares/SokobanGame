#pragma once

#include <cmath>

namespace sokoban {

struct CameraAngles {
    static constexpr float defaultPitchDegrees = 30.0f;
    static constexpr float defaultYawDegrees = 0.0f;
    static constexpr float maximumPitchDegrees = 89.0f;

    // Pitch is measured away from straight down. Yaw rotates the +Y
    // viewpoint toward +X around the vertical axis.
    float pitchDegrees = defaultPitchDegrees;
    float yawDegrees = defaultYawDegrees;

    [[nodiscard]] bool valid() const noexcept
    {
        return std::isfinite(pitchDegrees) && std::isfinite(yawDegrees) &&
            pitchDegrees >= 0.0f && pitchDegrees <= maximumPitchDegrees &&
            yawDegrees >= -180.0f && yawDegrees <= 180.0f;
    }

    bool operator==(const CameraAngles&) const = default;
};

} // namespace sokoban

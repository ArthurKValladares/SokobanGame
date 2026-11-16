#pragma once

#include "engine/Math.hpp"

#include <optional>

union SDL_Event;
struct SDL_Window;

namespace sokoban {

class InputState;
struct RenderFrameData;

// A temporary camera pose, independent of authored screen camera angles.
class DetachedCamera {
public:
    struct Settings {
        float moveSpeed = 5.0f;
        float fastMultiplier = 4.0f;
        float mouseSensitivityDegrees = 0.12f;
        float verticalFovDegrees = 60.0f;
    };

    void detach(Vec3 position, Vec3 forward, float verticalFovDegrees,
        std::optional<Vec3> right = std::nullopt);
    void attach(SDL_Window* window);
    void resetPose();
    [[nodiscard]] bool active() const { return active_; }
    [[nodiscard]] bool capturingMouse() const { return active_ && mouseCaptured_; }
    void releaseMouse(SDL_Window* window);
    [[nodiscard]] bool handleEvent(const SDL_Event& event, SDL_Window* window, bool allowBoardCapture);
    void update(float dt, const InputState& input);
    void apply(RenderFrameData& frame) const;

    [[nodiscard]] Settings& settings() { return settings_; }
    [[nodiscard]] Vec3& position() { return pose_.position; }

private:
    struct Pose {
        Vec3 position {};
        float yawRadians = 0.0f;
        float pitchRadians = 0.0f;
    };

    [[nodiscard]] Vec3 forward() const;
    [[nodiscard]] Vec3 right() const;
    void setMouseCapture(SDL_Window* window, bool captured);

    Pose pose_;
    Pose home_;
    Settings settings_;
    Vec2 pendingMouseDelta_ {};
    bool active_ = false;
    bool mouseCaptured_ = false;
};

} // namespace sokoban

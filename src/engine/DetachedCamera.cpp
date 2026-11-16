#include "engine/DetachedCamera.hpp"

#include "engine/Input.hpp"
#include "engine/Log.hpp"
#include "engine/render/RenderTypes.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>

namespace sokoban {

void DetachedCamera::detach(Vec3 position, Vec3 direction, float verticalFovDegrees,
    std::optional<Vec3> rightVector)
{
    direction = normalizeOr(direction, Vec3 { 0.0f, 1.0f, 0.0f });
    const Vec2 heading { direction.x, direction.y };
    float yaw = std::atan2(heading.x, heading.y);
    if (rightVector && lengthSquared(heading) <= normalizeEpsilonSquared &&
        lengthSquared(Vec2 { rightVector->x, rightVector->y }) > normalizeEpsilonSquared) {
        yaw = std::atan2(rightVector->y, -rightVector->x);
    }
    pose_ = {
        .position = position,
        .yawRadians = yaw,
        .pitchRadians = std::atan2(direction.z, length(heading)),
    };
    home_ = pose_;
    settings_.verticalFovDegrees = std::clamp(verticalFovDegrees, 5.0f, 120.0f);
    pendingMouseDelta_ = {};
    active_ = true;
}

void DetachedCamera::attach(SDL_Window* window)
{
    releaseMouse(window);
    active_ = false;
}

void DetachedCamera::resetPose()
{
    pose_ = home_;
    pendingMouseDelta_ = {};
}

Vec3 DetachedCamera::forward() const
{
    const float horizontal = std::max(0.0f, std::cos(pose_.pitchRadians));
    return normalizeOr(
        Vec3 {
            horizontal * std::sin(pose_.yawRadians),
            horizontal * std::cos(pose_.yawRadians),
            std::sin(pose_.pitchRadians),
        },
        Vec3 { 0.0f, 1.0f, 0.0f });
}

Vec3 DetachedCamera::right() const
{
    return { -std::cos(pose_.yawRadians), std::sin(pose_.yawRadians), 0.0f };
}

void DetachedCamera::setMouseCapture(SDL_Window* window, bool captured)
{
    if (mouseCaptured_ == captured) return;
    if (!SDL_SetWindowRelativeMouseMode(window, captured)) {
        log::warning(log::Category::Application)
            << "Could not " << (captured ? "capture" : "release")
            << " the detached-camera mouse: " << SDL_GetError();
        if (captured) return;
    }
    mouseCaptured_ = captured;
    pendingMouseDelta_ = {};
}

void DetachedCamera::releaseMouse(SDL_Window* window)
{
    setMouseCapture(window, false);
}

bool DetachedCamera::handleEvent(const SDL_Event& event, SDL_Window* window, bool allowBoardCapture)
{
    if (!active_) return false;
    if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST) {
        releaseMouse(window);
        return false;
    }
    if (mouseCaptured_) {
        if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat &&
            event.key.scancode == SDL_SCANCODE_ESCAPE) {
            releaseMouse(window);
            return true;
        }
        if (event.type == SDL_EVENT_MOUSE_MOTION) {
            pendingMouseDelta_ += { event.motion.xrel, event.motion.yrel };
            return true;
        }
        return event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
            event.type == SDL_EVENT_MOUSE_BUTTON_UP;
    }
    if (allowBoardCapture && event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
        event.button.button == SDL_BUTTON_LEFT) {
        setMouseCapture(window, true);
        return mouseCaptured_;
    }
    return false;
}

void DetachedCamera::update(float dt, const InputState& input)
{
    if (!capturingMouse()) {
        pendingMouseDelta_ = {};
        return;
    }

    pose_.yawRadians -= degreesToRadians(pendingMouseDelta_.x * settings_.mouseSensitivityDegrees);
    if (pendingMouseDelta_.y != 0.0f) {
        pose_.pitchRadians -= degreesToRadians(pendingMouseDelta_.y * settings_.mouseSensitivityDegrees);
        pose_.pitchRadians = std::clamp(
            pose_.pitchRadians, degreesToRadians(-89.0f), degreesToRadians(89.0f));
    }
    pendingMouseDelta_ = {};

    const Vec3 direction = forward();
    Vec3 movement {};
    if (input.keyDown(SDL_SCANCODE_W)) movement += direction;
    if (input.keyDown(SDL_SCANCODE_S)) movement -= direction;
    if (input.keyDown(SDL_SCANCODE_D)) movement += right();
    if (input.keyDown(SDL_SCANCODE_A)) movement -= right();
    if (input.keyDown(SDL_SCANCODE_SPACE) || input.keyDown(SDL_SCANCODE_E)) {
        movement.z += 1.0f;
    }
    if (input.keyDown(SDL_SCANCODE_LCTRL) || input.keyDown(SDL_SCANCODE_RCTRL) ||
        input.keyDown(SDL_SCANCODE_Q)) {
        movement.z -= 1.0f;
    }
    if (lengthSquared(movement) <= normalizeEpsilonSquared) return;

    float speed = settings_.moveSpeed;
    if (input.keyDown(SDL_SCANCODE_LSHIFT) || input.keyDown(SDL_SCANCODE_RSHIFT)) {
        speed *= settings_.fastMultiplier;
    }
    const float step = std::clamp(dt, 0.0f, 0.1f) * speed;
    pose_.position += normalize(movement) * step;
}

void DetachedCamera::apply(RenderFrameData& frame) const
{
    if (!active_ || frame.viewMode != RenderViewMode::Isometric3D) return;
    frame.cameraOverride = RenderFrameData::CameraOverride {
        .position = pose_.position,
        .forward = forward(),
        .verticalFovDegrees = settings_.verticalFovDegrees,
        .right = right(),
    };
}

} // namespace sokoban

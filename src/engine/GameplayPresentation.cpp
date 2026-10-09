#include "engine/GameplayPresentation.hpp"

#include "engine/ElevatorVisuals.hpp"
#include "engine/PresentationTransactionBuilder.hpp"
#include "engine/ParticleConfig.hpp"
#include "engine/render/AnimationConfig.hpp"
#include "engine/render/CameraConfig.hpp"
#include "engine/render/WaterGeometry.hpp"
#include "engine/render/WaterConfig.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <ranges>

namespace sokoban {
namespace {

Vec3 toVec3(GridPosition3 position)
{
    return {
        static_cast<float>(position.x),
        static_cast<float>(position.y),
        static_cast<float>(position.z),
    };
}

Vec3 movableRenderTarget(GridPosition3 position, bool fallen)
{
    Vec3 target = toVec3(position);
    if (fallen) {
        target.z -= config::waterDepthBelowGround;
    }
    return target;
}

Vec3 playerRenderTarget(GridPosition3 position, bool drowned)
{
    Vec3 target = toVec3(position);
    if (drowned) {
        target.z -= config::drownedPlayerDepthBelowGround;
    }
    return target;
}

EntityTarget playerTarget(const GameState::Player& player, std::size_t index)
{
    return {
        EntityKind::Player,
        resolvedEntityId(EntityKind::Player, player.id, index),
    };
}

EntityTarget movableTarget(const GameState::Movable& movable, std::size_t index)
{
    return {
        EntityKind::Movable,
        resolvedEntityId(EntityKind::Movable, movable.id, index),
    };
}

EntityTarget enemyTarget(const GameState::Enemy& enemy, std::size_t index)
{
    return {
        EntityKind::Enemy,
        resolvedEntityId(EntityKind::Enemy, enemy.id, index),
    };
}

EntityTarget elevatorTarget(std::size_t index)
{
    return {
        EntityKind::Elevator,
        resolvedEntityId(EntityKind::Elevator, invalidEntityId, index),
    };
}

EntityTarget minecartTarget(std::size_t index)
{
    return {
        EntityKind::Minecart,
        resolvedEntityId(EntityKind::Minecart, invalidEntityId, index),
    };
}

// A moving platform that changed stops during one action leg.
struct ElevatorMove {
    EntityKind kind = EntityKind::Elevator;
    std::size_t index = 0;
    GridPosition3 from {};
    GridPosition3 to {};
    // When it sets off, and how long it travels, within the leg.
    float startSeconds = 0.0f;
    float durationSeconds = 0.0f;
    // Minecarts follow every rail cell between stops. Elevators leave this
    // empty and use their ordinary straight vertical interpolation.
    std::vector<GridPosition3> path;
};

std::vector<GridPosition3> minecartPath(
    const Level& level,
    std::size_t index,
    const GameState::Minecart& before,
    const GameState::Minecart& after)
{
    if (index >= level.minecartRoutes().size()) {
        return { before.cell, after.cell };
    }
    const Level::MinecartRoute& route = level.minecartRoutes()[index];
    const std::size_t fromStop = rules::minecartStopIndex(route, before.phase);
    const std::size_t toStop = rules::minecartStopIndex(route, after.phase);
    if (fromStop >= route.stopCellIndices.size() ||
        toStop >= route.stopCellIndices.size() || route.cells.empty() ||
        route.stops[fromStop] != before.cell ||
        route.stops[toStop] != after.cell) {
        return { before.cell, after.cell };
    }

    std::vector<GridPosition3> path { before.cell };
    const std::size_t from = route.stopCellIndices[fromStop];
    const std::size_t to = route.stopCellIndices[toStop];
    if (to > from) {
        path.insert(
            path.end(),
            route.cells.begin() + static_cast<std::ptrdiff_t>(from + 1),
            route.cells.begin() + static_cast<std::ptrdiff_t>(to + 1));
    } else if (route.loop && fromStop + 1 == route.stops.size() &&
               toStop == 0) {
        path.insert(
            path.end(),
            route.cells.begin() + static_cast<std::ptrdiff_t>(from + 1),
            route.cells.end());
        path.push_back(route.cells.front());
    } else {
        for (std::size_t cell = from; cell > to; --cell) {
            path.push_back(route.cells[cell - 1]);
        }
    }
    return path.back() == after.cell
        ? path
        : std::vector<GridPosition3> { before.cell, after.cell };
}

// Platforms move once the leg's own movement has settled (the rules decide
// activation from where units end up), so their travel follows the leg's
// walking time, `stepSeconds`, instead of being squeezed into it.
std::vector<ElevatorMove> platformMoves(
    const GameState& before,
    const GameState& after,
    float stepSeconds,
    const Level* level)
{
    std::vector<ElevatorMove> moves;
    const std::size_t count =
        std::min(before.elevators.size(), after.elevators.size());
    for (std::size_t index = 0; index < count; ++index) {
        const GridPosition3 from = before.elevators[index].cell;
        const GridPosition3 to = after.elevators[index].cell;
        if (from == to) {
            continue;
        }
        moves.push_back({
            .kind = EntityKind::Elevator,
            .index = index,
            .from = from,
            .to = to,
            .startSeconds = stepSeconds,
            .durationSeconds = stepSeconds *
                config::elevatorSecondsPerLayerPerStep *
                static_cast<float>(std::abs(to.z - from.z)),
        });
    }
    const std::size_t minecartCount =
        std::min(before.minecarts.size(), after.minecarts.size());
    for (std::size_t index = 0; index < minecartCount; ++index) {
        const GridPosition3 from = before.minecarts[index].cell;
        const GridPosition3 to = after.minecarts[index].cell;
        if (from == to) {
            continue;
        }
        std::vector<GridPosition3> path = level != nullptr
            ? minecartPath(
                  *level, index, before.minecarts[index], after.minecarts[index])
            : std::vector<GridPosition3> { from, to };
        std::size_t distance = 0;
        for (std::size_t cell = 1; cell < path.size(); ++cell) {
            distance += static_cast<std::size_t>(
                std::abs(path[cell].x - path[cell - 1].x) +
                std::abs(path[cell].y - path[cell - 1].y) +
                std::abs(path[cell].z - path[cell - 1].z));
        }
        moves.push_back({
            .kind = EntityKind::Minecart,
            .index = index,
            .from = from,
            .to = to,
            .startSeconds = stepSeconds,
            .durationSeconds = stepSeconds *
                config::elevatorSecondsPerLayerPerStep *
                static_cast<float>(distance),
            .path = std::move(path),
        });
    }
    return moves;
}

void addPlatformPathMotion(
    PresentationTransactionBuilder& builder,
    EntityTarget target,
    const ElevatorMove& move,
    Vec3 from,
    Vec3 to)
{
    if (move.path.size() < 2) {
        builder.addMotion({
            .target = target,
            .from = from,
            .to = to,
            .startSeconds = move.startSeconds,
            .durationSeconds = move.durationSeconds,
        });
        return;
    }

    const Vec3 offset {
        from.x - static_cast<float>(move.from.x),
        from.y - static_cast<float>(move.from.y),
        from.z - static_cast<float>(move.from.z),
    };
    const float segmentSeconds = move.durationSeconds /
        static_cast<float>(move.path.size() - 1);
    for (std::size_t segment = 1; segment < move.path.size(); ++segment) {
        Vec3 segmentFrom = toVec3(move.path[segment - 1]);
        Vec3 segmentTo = toVec3(move.path[segment]);
        segmentFrom = segmentFrom + offset;
        segmentTo = segmentTo + offset;
        if (segment + 1 == move.path.size()) {
            segmentTo = to;
        }
        builder.addMotion({
            .target = target,
            .from = segmentFrom,
            .to = segmentTo,
            .startSeconds = move.startSeconds +
                segmentSeconds * static_cast<float>(segment - 1),
            .durationSeconds = segmentSeconds,
        });
    }
}

// How much longer than its walking time a leg runs while platforms travel.
float platformExtraSeconds(const std::vector<ElevatorMove>& moves)
{
    float extra = 0.0f;
    for (const ElevatorMove& move : moves) {
        extra = std::max(extra, move.durationSeconds);
    }
    return extra;
}

// The platform that carried a unit ending the leg in `after`, if one did: the
// unit is in the platform's column, either on a minecart in the same cell or
// in the stack above where it arrived, and its cell changed. Riders always
// move with their platform.
const ElevatorMove* rideOf(
    const std::vector<ElevatorMove>& moves,
    GridPosition3 before,
    GridPosition3 after)
{
    if (before == after) {
        return nullptr;
    }
    for (const ElevatorMove& move : moves) {
        const GridPosition3 delta {
            move.to.x - move.from.x,
            move.to.y - move.from.y,
            move.to.z - move.from.z,
        };
        const GridPosition3 boarded {
            after.x - delta.x,
            after.y - delta.y,
            after.z - delta.z,
        };
        if (boarded.x == move.from.x && boarded.y == move.from.y &&
            boarded.z >= move.from.z) {
            return &move;
        }
    }
    return nullptr;
}

// Where a rider boarded: its final cell, before the platform carried it.
GridPosition3 boardingCell(const ElevatorMove& move, GridPosition3 after)
{
    return {
        after.x - (move.to.x - move.from.x),
        after.y - (move.to.y - move.from.y),
        after.z - (move.to.z - move.from.z),
    };
}

// Recover the pose between walking onto a platform and riding it. Both control
// motion and sound edges need this intermediate occupancy, which endpoints
// alone lose when a rider presses and leaves a plate within one leg.
GameState boardingState(
    const GameState& before,
    const GameState& after,
    const std::vector<ElevatorMove>& moves)
{
    GameState settled = after;
    const auto boardingPositions = [&](auto& entities, const auto& starts) {
        for (std::size_t index = 0; index < std::min(entities.size(), starts.size()); ++index) {
            if (const auto* ride = rideOf(moves, starts[index].cell, entities[index].cell)) {
                entities[index].cell = boardingCell(*ride, entities[index].cell);
            }
        }
    };
    boardingPositions(settled.players, before.players);
    boardingPositions(settled.movables, before.movables);
    boardingPositions(settled.enemies, before.enemies);
    return settled;
}

void appendControlTracks(
    ActionPresentationTimeline& timeline,
    const GameplaySession::Action& action,
    const Level& level,
    const std::vector<ElevatorMove>& moves,
    bool buttonPulse)
{
    constexpr float pressureTravelSeconds = 0.12f;
    constexpr float leverTravelSeconds = 0.22f;
    constexpr float buttonPressSeconds = 0.07f;
    constexpr float buttonReleaseStartSeconds = 0.10f;
    constexpr float buttonReleaseSeconds = 0.12f;
    const float motionSeconds = std::max(action.durationSeconds, 0.0f);
    const float platformSeconds = platformExtraSeconds(moves);
    const GameState settled = boardingState(action.before, action.after, moves);
    const auto add = [&](GridPosition3 cell, float from, float to, float start, float duration) {
        timeline.controls.push_back({ cell, from, to, start, duration });
        timeline.durationSeconds = std::max(timeline.durationSeconds, start + duration);
    };
    const auto pressureEdge = [&](GridPosition3 cell, const GameState& from,
                                  const GameState& to, float start, float duration) {
        const bool wasPressed = rules::isPressurePlateActive(level, from, cell);
        const bool pressed = rules::isPressurePlateActive(level, to, cell);
        if (wasPressed == pressed) return;
        const float travel = duration > 0.0f
            ? std::min(pressureTravelSeconds, duration)
            : pressureTravelSeconds;
        // A departing foot releases early; an arriving foot presses near the
        // end of its movement. Platform travel is a separate phase.
        float edgeStart = start + (pressed ? std::max(duration - travel, 0.0f) : 0.0f);
        // Reflection has no mechanical travel time. Give its pressure edges
        // visible travel too, and keep an instantaneous boarding press/release
        // in order instead of putting both at time zero.
        for (const ActionControlTrack& previous : timeline.controls) {
            if (previous.cell == cell) {
                edgeStart = std::max(edgeStart, previous.startSeconds + previous.durationSeconds);
            }
        }
        add(cell, wasPressed ? 1.0f : 0.0f, pressed ? 1.0f : 0.0f,
            edgeStart, travel);
    };
    for (const GridPosition3 cell : level.pressurePlates()) {
        const TileType tile = level.plateAt(cell).value_or(TileType::Air);
        if (tileTypeIsButton(tile)) {
            // Each Activate is a fresh physical cycle, including consecutive
            // pulses whose logical before/after states are identical.
            if (buttonPulse && std::ranges::find(action.after.activeButtons, cell) !=
                    action.after.activeButtons.end()) {
                add(cell, 0.0f, 1.0f, 0.0f, buttonPressSeconds);
                add(cell, 1.0f, 0.0f, buttonReleaseStartSeconds, buttonReleaseSeconds);
            }
        } else if (tileTypeIsLever(tile)) {
            const bool wasOn = rules::isPressurePlateActive(level, action.before, cell);
            const bool on = rules::isPressurePlateActive(level, action.after, cell);
            if (wasOn != on) {
                add(cell, wasOn ? 1.0f : 0.0f, on ? 1.0f : 0.0f,
                    0.0f, leverTravelSeconds);
            }
        } else {
            pressureEdge(cell, action.before, settled, 0.0f, motionSeconds);
            pressureEdge(cell, settled, action.after, motionSeconds, platformSeconds);
        }
    }
}

AnimationUse playerRestAnimation(const GameState::Player& player)
{
    return player.dead ? AnimationUse::PlayerDeadIdle : AnimationUse::PlayerIdle;
}

float gridDistance(Vec3 from, Vec3 to)
{
    return std::abs(to.x - from.x) +
        std::abs(to.y - from.y) +
        std::abs(to.z - from.z);
}

Vec3 interpolateGridMotion(
    Vec3 from,
    Vec3 to,
    float elapsedSeconds,
    float secondsPerTile)
{
    const float distance = gridDistance(from, to);
    if (distance <= 0.0001f || secondsPerTile <= 0.0f) {
        return to;
    }

    float remaining = std::min(elapsedSeconds / secondsPerTile, distance);
    Vec3 result = from;
    auto travelAxis = [&](float target, float& value) {
        const float delta = target - value;
        const float step = std::min(std::abs(delta), remaining);
        if (step > 0.0f) {
            value += std::copysign(step, delta);
            remaining -= step;
        }
    };

    if (to.z > from.z) {
        travelAxis(to.z, result.z);
    }
    travelAxis(to.x, result.x);
    travelAxis(to.y, result.y);
    if (to.z <= from.z) {
        travelAxis(to.z, result.z);
    }
    return result;
}

uint32_t facingQuarterTurns(MoveDirection direction)
{
    switch (direction) {
    case MoveDirection::Down:
        return 0;
    case MoveDirection::Left:
        return 1;
    case MoveDirection::Up:
        return 2;
    case MoveDirection::Right:
        return 3;
    }
    return 0;
}

template <typename Visual>
void setRestAnimation(Visual& visual, AnimationUse use)
{
    visual.setClipTimeFor(visual.animationUse, visual.clipTimeSeconds);
    if (visual.animationUse != use) {
        visual.animationUse = use;
        visual.clipTimeSeconds = visual.clipTimeFor(use);
    }
    visual.animationFallbackUse.reset();
    visual.clipPlaybackRate = 1.0f;
    visual.animationLoops = true;
    visual.animationCrossfades = true;
}

void selectAnimationSample(
    GameplayPresentation::AnimatedActorVisual& visual,
    AnimationUse use,
    float timeSeconds)
{
    visual.setClipTimeFor(visual.animationUse, visual.clipTimeSeconds);
    visual.animationUse = use;
    visual.clipTimeSeconds = timeSeconds;
    visual.setClipTimeFor(use, timeSeconds);
}

} // namespace

GameplayPresentation::GameplayPresentation()
    : cameraPitchDegrees_(config::cameraPitchDegrees)
    , cameraPitchStartDegrees_(config::cameraPitchDegrees)
    , cameraPitchTargetDegrees_(config::cameraPitchDegrees)
    , cameraYawDegrees_(config::cameraYawDegrees)
    , cameraYawStartDegrees_(config::cameraYawDegrees)
    , cameraYawTargetDegrees_(config::cameraYawDegrees)
{
}

void GameplayPresentation::resetEntities(const GameState& state)
{
    reverseSourceStartSeconds_ = 0.0f;
    players_.clear();
    movables_.clear();
    enemies_.clear();
    elevators_.clear();
    minecarts_.clear();
    controls_.clear();
    turretRecoils_.clear();
    clearWaterRipples();
    syncToGameState(state);
}

void GameplayPresentation::advanceClocks(float dt, bool reversed)
{
    worldAnimationTimeSeconds_ += reversed ? -dt : dt;
    animationTransitionTimeSeconds_ += std::max(dt, 0.0f);
    for (PlayerVisual& player : players_) {
        player.clipTimeSeconds += dt * player.clipPlaybackRate;
        player.setClipTimeFor(player.animationUse, player.clipTimeSeconds);
    }
    for (EnemyVisual& enemy : enemies_) {
        enemy.clipTimeSeconds += dt * enemy.clipPlaybackRate;
        enemy.setClipTimeFor(enemy.animationUse, enemy.clipTimeSeconds);
    }
    const float recoilStep = std::max(dt, 0.0f);
    for (TurretRecoil& recoil : turretRecoils_) {
        recoil.ageSeconds += recoilStep;
    }
    std::erase_if(turretRecoils_, [](const TurretRecoil& recoil) {
        return recoil.ageSeconds >= config::turretRecoilDurationSeconds;
    });
}

void GameplayPresentation::triggerTurretShot(
    EntityId turretId,
    MoveDirection direction,
    float delaySeconds)
{
    if (turretId == invalidEntityId) {
        return;
    }
    turretRecoils_.push_back({
        .turretId = turretId,
        .direction = direction,
        .ageSeconds = -std::max(delaySeconds, 0.0f),
    });
}

void GameplayPresentation::scheduleWaterEntries(
    const Level& level,
    const GameplaySession::Action& action,
    const std::vector<GameState>& legs,
    float mechanicalDurationSeconds,
    float elapsedSeconds)
{
    if (action.reversed) {
        clearWaterRipples();
        return;
    }
    const std::size_t legCount = std::max<std::size_t>(legs.size(), 1);
    const float stepSeconds = std::max(mechanicalDurationSeconds, 0.0f) /
        static_cast<float>(legCount);
    float legStart = 0.0f;
    for (std::size_t leg = 0; leg < legCount; ++leg) {
        const GameState& before = leg == 0 ? action.before : legs[leg - 1];
        const GameState& after = legs.empty() ? action.after : legs[leg];
        const float legEnd = legStart + stepSeconds + platformExtraSeconds(
            platformMoves(before, after, stepSeconds, &level));
        const auto schedule = [&](EntityTarget target, GridPosition3 cell) {
            if (level.supportingTileAt(cell) != TileType::Water) {
                return;
            }
            const float elevation = static_cast<float>(cell.z) -
                config::waterDepthBelowGround;
            float contactSeconds = legEnd;
            // Portals split one entity's motion into physical tracks. Locate
            // the arrival track, then use the same axis order as grid motion:
            // horizontal travel precedes downward travel through the surface.
            for (const ActionMotionTrack& track : action.presentation.motions) {
                if (track.target != target ||
                    track.startSeconds + 0.0001f < legStart ||
                    track.startSeconds > legEnd + 0.0001f ||
                    track.startSeconds + track.durationSeconds > legEnd + 0.0001f ||
                    std::abs(track.to.x - static_cast<float>(cell.x)) > 0.0001f ||
                    std::abs(track.to.y - static_cast<float>(cell.y)) > 0.0001f ||
                    track.to.z > elevation + 0.0001f) {
                    continue;
                }
                const float distance = gridDistance(track.from, track.to);
                const float horizontalDistance =
                    std::abs(track.to.x - track.from.x) +
                    std::abs(track.to.y - track.from.y);
                const bool enteringAcrossSurface =
                    target.kind == EntityKind::Movable &&
                    horizontalDistance > 0.0001f &&
                    track.from.z <= static_cast<float>(cell.z) + 0.0001f;
                // A moving block starts disturbing the cell when its leading
                // half enters the water. Waiting until it finishes sinking
                // would fill an isolated water tile before any ring is drawn.
                // Drops from above still wait to reach the surface elevation.
                const float contactDistance = enteringAcrossSurface
                    ? std::max(horizontalDistance - 0.5f, 0.0f)
                    : horizontalDistance +
                        std::max(track.from.z - elevation, 0.0f);
                const float fraction = distance > 0.0001f
                    ? std::clamp(contactDistance / distance, 0.0f, 1.0f)
                    : 0.0f;
                contactSeconds = track.startSeconds +
                    track.durationSeconds * fraction;
            }
            if (waterRippleCount_ == waterRipples_.size()) {
                // A fixed budget keeps effects bounded even on crowded boards.
                // Retire the oldest admission when every slot is occupied.
                std::move(waterRipples_.begin() + 1, waterRipples_.end(),
                    waterRipples_.begin());
                --waterRippleCount_;
            }
            waterRipples_[waterRippleCount_++] = {
                .position = {
                    static_cast<float>(cell.x) + 0.5f,
                    static_cast<float>(cell.y) + 0.5f,
                    elevation,
                },
                .ageSeconds = elapsedSeconds - contactSeconds,
            };
        };
        // Stable identities also cover mirror copies added by this action.
        const auto entries = [&](const auto& from, const auto& to,
                                 auto targetOf, auto submerged) {
            for (std::size_t index = 0; index < to.size(); ++index) {
                if (!submerged(to[index])) {
                    continue;
                }
                const EntityTarget target = targetOf(to[index], index);
                bool wasSubmerged = false;
                if (index < from.size() && targetOf(from[index], index) == target) {
                    wasSubmerged = submerged(from[index]);
                } else {
                    for (std::size_t prior = 0; prior < from.size(); ++prior) {
                        if (targetOf(from[prior], prior) == target) {
                            wasSubmerged = submerged(from[prior]);
                            break;
                        }
                    }
                }
                if (!wasSubmerged) {
                    schedule(target, to[index].cell);
                }
            }
        };
        entries(before.players, after.players, playerTarget,
            [](const auto& player) { return player.drowned; });
        entries(before.movables, after.movables, movableTarget,
            [](const auto& movable) { return movable.fallen; });
        entries(before.enemies, after.enemies, enemyTarget,
            [](const auto& enemy) { return enemy.fallen; });
        legStart = legEnd;
    }
}

void GameplayPresentation::advanceWaterRipples(float dt)
{
    std::size_t retained = 0;
    for (std::size_t index = 0; index < waterRippleCount_; ++index) {
        auto ripple = waterRipples_[index];
        ripple.ageSeconds += std::max(dt, 0.0f);
        if (ripple.ageSeconds < config::waterImpactRippleLifetimeSeconds) {
            waterRipples_[retained++] = ripple;
        }
    }
    waterRippleCount_ = retained;
}

void GameplayPresentation::clearWaterRipples()
{
    waterRippleCount_ = 0;
}

void GameplayPresentation::appendWaterRippleRenderData(RenderFrameData& frame) const
{
    for (std::size_t index = 0; index < waterRippleCount_; ++index) {
        const auto& ripple = waterRipples_[index];
        if (ripple.ageSeconds >= 0.0f &&
            ripple.ageSeconds < config::waterImpactRippleLifetimeSeconds &&
            frame.waterRippleCount < frame.waterRipples.size()) {
            frame.waterRipples[frame.waterRippleCount++] = ripple;
        }
    }
}

Vec2 GameplayPresentation::turretRecoilOffset(EntityId turretId) const
{
    Vec2 offset {};
    for (const TurretRecoil& recoil : turretRecoils_) {
        if (recoil.turretId != turretId || recoil.ageSeconds < 0.0f) {
            continue;
        }
        const float progress = std::clamp(
            recoil.ageSeconds / config::turretRecoilDurationSeconds,
            0.0f,
            1.0f);
        constexpr float kickEnd = 0.18f;
        const float phase = progress < kickEnd
            ? progress / kickEnd
            : (progress - kickEnd) / (1.0f - kickEnd);
        const float smooth = phase * phase * (3.0f - 2.0f * phase);
        const float amount = progress < kickEnd ? smooth : 1.0f - smooth;
        const GridPosition direction = rules::directionOffset(recoil.direction);
        offset.x -= static_cast<float>(direction.x) *
            config::turretRecoilDistance * amount;
        offset.y -= static_cast<float>(direction.y) *
            config::turretRecoilDistance * amount;
    }
    return offset;
}

void GameplayPresentation::updateCameraPitch(
    float targetDegrees,
    float dt,
    float transitionSeconds)
{
    targetDegrees = std::clamp(
        targetDegrees, 0.0f, CameraAngles::maximumPitchDegrees);
    dt = std::max(dt, 0.0f);
    transitionSeconds = std::max(transitionSeconds, 0.0f);

    if (std::abs(targetDegrees - cameraPitchTargetDegrees_) > 0.0001f) {
        cameraPitchStartDegrees_ = cameraPitchDegrees_;
        cameraPitchTargetDegrees_ = targetDegrees;
        cameraPitchTransitionElapsed_ = 0.0f;
    }
    if (transitionSeconds <= 0.0f) {
        cameraPitchDegrees_ = cameraPitchTargetDegrees_;
        cameraPitchTransitionElapsed_ = 0.0f;
        return;
    }

    cameraPitchTransitionElapsed_ = std::min(
        cameraPitchTransitionElapsed_ + dt,
        transitionSeconds);
    const float progress = cameraPitchTransitionElapsed_ / transitionSeconds;
    const float eased = progress * progress * (3.0f - 2.0f * progress);
    cameraPitchDegrees_ = cameraPitchStartDegrees_ +
        (cameraPitchTargetDegrees_ - cameraPitchStartDegrees_) * eased;
}

void GameplayPresentation::updateCameraYaw(
    float targetDegrees,
    float dt,
    float transitionSeconds)
{
    targetDegrees = std::remainder(targetDegrees, 360.0f);
    dt = std::max(dt, 0.0f);
    transitionSeconds = std::max(transitionSeconds, 0.0f);
    if (std::abs(std::remainder(
            targetDegrees - cameraYawTargetDegrees_, 360.0f)) > 0.0001f) {
        cameraYawStartDegrees_ = cameraYawDegrees_;
        cameraYawTargetDegrees_ = targetDegrees;
        cameraYawTransitionElapsed_ = 0.0f;
    }
    if (transitionSeconds <= 0.0f) {
        cameraYawDegrees_ = cameraYawTargetDegrees_;
        cameraYawTransitionElapsed_ = 0.0f;
        return;
    }
    cameraYawTransitionElapsed_ = std::min(
        cameraYawTransitionElapsed_ + dt, transitionSeconds);
    const float progress = cameraYawTransitionElapsed_ / transitionSeconds;
    const float eased = progress * progress * (3.0f - 2.0f * progress);
    const float turn = std::remainder(
        cameraYawTargetDegrees_ - cameraYawStartDegrees_, 360.0f);
    cameraYawDegrees_ = std::remainder(
        cameraYawStartDegrees_ + turn * eased, 360.0f);
}

void GameplayPresentation::advanceAnimations(float dt, const GameState& state)
{
    for (std::size_t enemyIndex = 0;
         enemyIndex < enemies_.size() && enemyIndex < state.enemies.size();
         ++enemyIndex) {
        if (state.enemies[enemyIndex].fallen ||
            state.enemies[enemyIndex].dead) {
            continue;
        }
        const PlayerVisual* closest = nullptr;
        float closestDistance = 0.0f;
        for (std::size_t playerIndex = 0;
             playerIndex < players_.size() && playerIndex < state.players.size();
             ++playerIndex) {
            if (state.players[playerIndex].dead) {
                continue;
            }
            const float dx = players_[playerIndex].motion.renderPosition.x -
                enemies_[enemyIndex].motion.renderPosition.x;
            const float dy = players_[playerIndex].motion.renderPosition.y -
                enemies_[enemyIndex].motion.renderPosition.y;
            const float distance = dx * dx + dy * dy;
            if (closest == nullptr || distance < closestDistance) {
                closest = &players_[playerIndex];
                closestDistance = distance;
            }
        }
        if (closest == nullptr) {
            continue;
        }
        const float dx = closest->motion.renderPosition.x -
            enemies_[enemyIndex].motion.renderPosition.x;
        const float dy = closest->motion.renderPosition.y -
            enemies_[enemyIndex].motion.renderPosition.y;
        if (std::abs(dx) + std::abs(dy) <= 0.0001f) {
            continue;
        }
        // A rotator plate's turn holds until the next action begins (see
        // beginAction); otherwise the enemy faces the hero squarely.
        const Quat target = quatFromAxisAngle(
            { 0.0f, 0.0f, 1.0f },
            std::atan2(-dx, dy) +
                enemies_[enemyIndex].rotatorYawOffsetRadians);
        const float blend = config::enemyFacingSlerpSeconds <= 0.0f
            ? 1.0f
            : 1.0f - std::exp(
                  -4.0f * std::max(dt, 0.0f) /
                  config::enemyFacingSlerpSeconds);
        // The shared slerp does not clamp - that is the mathematical
        // operation, and the local copy this replaced clamped while the glTF
        // one did not. `blend` is 1 - exp(-k*dt), so it cannot leave [0, 1)
        // for a non-negative dt; the clamp is kept anyway to preserve exactly
        // what this call site used to guarantee for itself.
        enemies_[enemyIndex].orientation = slerp(
            enemies_[enemyIndex].orientation,
            target,
            std::clamp(blend, 0.0f, 1.0f));
    }
}

ActionPresentationTimeline GameplayPresentation::buildActionPresentation(
    const GameplaySession::Action& action,
    const std::vector<GameState>& legs,
    const Level* level,
    const std::vector<plans::PlannedAction::PortalCue>* cues) const
{
    const auto legTransits = [&](std::size_t leg) {
        std::vector<rules::PortalTransit> events;
        if (cues) {
            for (const auto& cue : *cues) {
                if (cue.legIndex == leg) events.push_back(cue.transit);
            }
        }
        return events;
    };
    if (legs.size() <= 1) {
        const auto events = legTransits(0);
        return buildActionPresentationLeg(
            action, level, cues ? &events : nullptr, legs.empty());
    }

    // A chained slide is one action spanning several world steps. Interpolating
    // once from start to finish would be right for a single block travelling in
    // a straight line, but wrong the moment a chain is involved: a block that
    // only starts moving on the fourth step would set off immediately.
    const float stepDuration =
        action.durationSeconds / static_cast<float>(legs.size());

    // Each leg is resolved as its own transaction and the results are laid end
    // to end. Motion alone used to be enough here, on the reasoning that only
    // the first leg is player-driven and only it can produce an animated event.
    // That is not true: a player crushed or drowned part-way through a slide
    // dies on leg four, and running the builder over leg one only gave it
    // correct motion and no clip at all.
    //
    // Push/pull effort is deliberately confined to the first leg. Later legs
    // are momentum spending itself out; carrying either flag through would
    // play an effort animation for the whole length of a slide.
    //
    // A leg in which a platform moves runs longer by its travel
    // time, and every later leg starts that much later.
    ActionPresentationTimeline timeline;
    float legStart = 0.0f;
    float platformSeconds = 0.0f;
    for (std::size_t leg = 0; leg < legs.size(); ++leg) {
        GameplaySession::Action legAction = action;
        legAction.before = leg == 0 ? action.before : legs[leg - 1];
        legAction.after = legs[leg];
        legAction.durationSeconds = stepDuration;
        legAction.playerPushing = leg == 0 && action.playerPushing;
        legAction.playerPulling = leg == 0 && action.playerPulling;

        const auto events = legTransits(leg);
        timeline = concatenateTimelines(
            std::move(timeline),
            buildActionPresentationLeg(legAction, level, cues ? &events : nullptr, false),
            legStart);
        const float extra = platformExtraSeconds(
            platformMoves(
                legAction.before, legAction.after, stepDuration, level));
        platformSeconds += extra;
        legStart += stepDuration + extra;
    }
    // The action's own duration is authoritative: rounding across legs must not
    // shorten or stretch it. Only platform travel lengthens it.
    timeline.durationSeconds = action.durationSeconds + platformSeconds;
    return timeline;
}

ActionPresentationTimeline GameplayPresentation::buildActionPresentation(
    const GameplaySession::Action& action,
    const Level* level,
    const std::vector<rules::PortalTransit>* transits) const
{
    return buildActionPresentationLeg(action, level, transits, true);
}

ActionPresentationTimeline GameplayPresentation::buildActionPresentationLeg(
    const GameplaySession::Action& action,
    const Level* level,
    const std::vector<rules::PortalTransit>* transits,
    bool buttonPulse) const
{
    PresentationTransactionBuilder builder(animationCatalog_);
    const float motionDuration = std::max(action.durationSeconds, 0.0f);
    const std::vector<ElevatorMove> elevators =
        platformMoves(action.before, action.after, motionDuration, level);
    // Carries `target` from its boarding cell to `after` once its platform
    // sets off; returns the cell the ordinary motion should end at.
    const auto addRide = [&](EntityTarget target,
                             GridPosition3 before,
                             GridPosition3 after,
                             bool fallen) {
        const ElevatorMove* ride = rideOf(elevators, before, after);
        if (ride == nullptr) {
            return after;
        }
        const GridPosition3 boarded = boardingCell(*ride, after);
        addPlatformPathMotion(
            builder,
            target,
            *ride,
            movableRenderTarget(boarded, fallen),
            movableRenderTarget(after, fallen));
        return boarded;
    };

    const auto addUnitMotion = [&](EntityTarget target,
                                   GridPosition3 before,
                                   GridPosition3 after,
                                   Vec3 from,
                                   Vec3 to) {
        if (level && transits) {
            std::vector<std::pair<Vec3, Vec3>> physicalLegs;
            Vec3 current = from;
            for (const auto& transit : *transits) {
                if (transit.target != target) continue;
                const GridPosition entry =
                    rules::directionOffset(transit.entryDirection);
                const GridPosition exit =
                    portalEdgeOffset(*level->plateAt(transit.exit));
                physicalLegs.push_back(
                    { current,
                      toVec3(transit.entrance) +
                          Vec3 { static_cast<float>(entry.x) * 0.5f, static_cast<float>(entry.y) * 0.5f, 0 } });
                current = toVec3(transit.exit) +
                    Vec3 { static_cast<float>(exit.x) * 0.5f, static_cast<float>(exit.y) * 0.5f, 0 };
            }
            if (!physicalLegs.empty()) {
                physicalLegs.push_back({ current, to });
                float distance = 0.0f;
                for (const auto& [start, end] : physicalLegs)
                    distance += length(end - start);
                float startSeconds = 0.0f;
                for (const auto& [start, end] : physicalLegs) {
                    const float duration = motionDuration *
                        length(end - start) / std::max(distance, 0.001f);
                    builder.addMotion({ .target = target,
                                        .from = start,
                                        .to = end,
                                        .startSeconds = startSeconds,
                                        .durationSeconds = duration });
                    startSeconds += duration;
                }
                return;
            }
        }
        if (level && !transits) {
            const auto exit = level->portalExit(before);
            if (exit && exit->x == after.x && exit->y == after.y &&
                after.z <= exit->z) {
                const GridPosition entryEdge =
                    portalEdgeOffset(*level->plateAt(before));
                const GridPosition exitEdge =
                    portalEdgeOffset(*level->plateAt(*exit));
                const Vec3 mouth = toVec3(before) +
                    Vec3 { static_cast<float>(entryEdge.x) * 0.5f, static_cast<float>(entryEdge.y) * 0.5f, 0 };
                const Vec3 emerged = toVec3(*exit) +
                    Vec3 { static_cast<float>(exitEdge.x) * 0.5f, static_cast<float>(exitEdge.y) * 0.5f, 0 };
                builder.addMotion({ .target = target,
                                    .from = from,
                                    .to = mouth,
                                    .durationSeconds = motionDuration * 0.5f });
                builder.addMotion({ .target = target,
                                    .from = emerged,
                                    .to = to,
                                    .startSeconds = motionDuration * 0.5f,
                                    .durationSeconds = motionDuration * 0.5f });
                return;
            }
        }
        builder.addMotion({ .target = target,
                            .from = from,
                            .to = to,
                            .durationSeconds = motionDuration });
    };

    const std::size_t playerCount = std::min(
        action.before.players.size(),
        action.after.players.size());
    for (std::size_t index = 0; index < playerCount; ++index) {
        const GameState::Player& before = action.before.players[index];
        const GameState::Player& after = action.after.players[index];
        const EntityTarget target = playerTarget(before, index);
        float initialClipTime = 0.0f;
        float movementClipTime = 0.0f;
        const auto visual = std::ranges::find_if(
            players_,
            [&](const PlayerVisual& candidate) {
                return candidate.motion.target == target;
            });
        if (visual != players_.end()) {
            initialClipTime = visual->clipTimeFor(
                playerRestAnimation(before));
        }
        builder.setInitialAnimation(
            target,
            playerRestAnimation(before),
            initialClipTime);
        // A hero a platform carries walks (if it did) to where it boarded,
        // then rides standing still.
        const ElevatorMove* ride = rideOf(elevators, before.cell, after.cell);
        const GridPosition3 walkedTo =
            ride != nullptr ? boardingCell(*ride, after.cell) : after.cell;
        const Vec3 from = playerRenderTarget(before.cell, before.drowned);
        const Vec3 to = playerRenderTarget(walkedTo, after.drowned);
        if (ride != nullptr) {
            addPlatformPathMotion(
                builder,
                target,
                *ride,
                to,
                playerRenderTarget(after.cell, after.drowned));
        }
        if (gridDistance(from, to) > 0.0001f) {
            const AnimationUse movementUse = action.playerPulling
                ? AnimationUse::PlayerPull
                : action.playerPushing
                    ? AnimationUse::PlayerPush
                    : AnimationUse::PlayerMove;
            if (visual != players_.end()) {
                movementClipTime = visual->clipTimeFor(movementUse);
            }
            addUnitMotion(target, before.cell, walkedTo, from, to);
            static_cast<void>(builder.addAnimation({
                .target = target,
                .use = movementUse,
                .completionUse = AnimationUse::PlayerIdle,
                .clipStartSeconds = movementClipTime,
                .durationSeconds = motionDuration,
                .loops = true,
            }));
        }
    }

    const std::size_t movableCount = std::min(
        action.before.movables.size(),
        action.after.movables.size());
    for (std::size_t index = 0; index < movableCount; ++index) {
        const GameState::Movable& before = action.before.movables[index];
        const GameState::Movable& after = action.after.movables[index];
        const GridPosition3 walkedTo = addRide(
            movableTarget(before, index), before.cell, after.cell, after.fallen);
        const Vec3 from = movableRenderTarget(before.cell, before.fallen);
        const Vec3 to = movableRenderTarget(walkedTo, after.fallen);
        if (gridDistance(from, to) > 0.0001f) {
            addUnitMotion(
                movableTarget(before, index), before.cell, walkedTo, from, to);
        }
    }

    for (const ElevatorMove& move : elevators) {
        addPlatformPathMotion(
            builder,
            move.kind == EntityKind::Minecart
                ? minecartTarget(move.index)
                : elevatorTarget(move.index),
            move,
            toVec3(move.from),
            toVec3(move.to));
    }

    using IntentId = PresentationTransactionBuilder::AnimationIntentId;
    const std::size_t enemyCount = std::min(
        action.before.enemies.size(),
        action.after.enemies.size());
    std::vector<std::vector<std::size_t>> attackedPlayers(enemyCount);
    std::vector<std::optional<IntentId>> attackIntents(enemyCount);
    for (std::size_t enemyIndex = 0; enemyIndex < enemyCount; ++enemyIndex) {
        const GameState::Enemy& before = action.before.enemies[enemyIndex];
        const GameState::Enemy& after = action.after.enemies[enemyIndex];
        const EntityTarget target = enemyTarget(before, enemyIndex);
        float initialClipTime = 0.0f;
        const auto visual = std::ranges::find_if(
            enemies_,
            [&](const EnemyVisual& candidate) {
                return candidate.motion.target == target;
            });
        if (visual != enemies_.end()) {
            initialClipTime = visual->clipTimeFor(AnimationUse::EnemyIdle);
        }
        builder.setInitialAnimation(
            target,
            AnimationUse::EnemyIdle,
            initialClipTime);
        const GridPosition3 walkedTo =
            addRide(target, before.cell, after.cell, after.fallen);
        const Vec3 from = movableRenderTarget(before.cell, before.fallen);
        const Vec3 to = movableRenderTarget(walkedTo, after.fallen);
        if (gridDistance(from, to) > 0.0001f) {
            addUnitMotion(target, before.cell, walkedTo, from, to);
        }

        for (std::size_t playerIndex = 0;
             playerIndex < playerCount;
             ++playerIndex) {
            const GameState::Player& playerBefore = action.before.players[playerIndex];
            const GameState::Player& playerAfter = action.after.players[playerIndex];
            if (playerBefore.dead || !playerAfter.dead || playerAfter.drowned ||
                after.fallen || after.dead ||
                playerAfter.cell.z != after.cell.z) {
                continue;
            }
            const int distance =
                std::abs(playerAfter.cell.x - after.cell.x) +
                std::abs(playerAfter.cell.y - after.cell.y);
            if (distance == 1) {
                attackedPlayers[enemyIndex].push_back(playerIndex);
            }
        }
        if (!attackedPlayers[enemyIndex].empty()) {
            attackIntents[enemyIndex] = builder.addAnimation({
                .target = target,
                .use = AnimationUse::EnemyAttack,
                .completionUse = AnimationUse::EnemyIdle,
                .fallbackUse = AnimationUse::EnemyIdle,
            });
        }
    }

    for (std::size_t playerIndex = 0;
         playerIndex < playerCount;
         ++playerIndex) {
        const GameState::Player& before = action.before.players[playerIndex];
        const GameState::Player& after = action.after.players[playerIndex];
        if (before.dead || !after.dead) {
            continue;
        }
        const IntentId deathIntent = builder.addAnimation({
            .target = playerTarget(before, playerIndex),
            .use = AnimationUse::PlayerDeath,
            .completionUse = AnimationUse::PlayerDeadIdle,
            .fallbackUse = AnimationUse::PlayerDeadIdle,
        });
        if (after.drowned) {
            continue;
        }
        for (std::size_t enemyIndex = 0;
             enemyIndex < attackedPlayers.size();
             ++enemyIndex) {
            if (!attackIntents[enemyIndex] ||
                std::ranges::find(attackedPlayers[enemyIndex], playerIndex) ==
                    attackedPlayers[enemyIndex].end()) {
                continue;
            }
            if (builder.startAfterCatalogEvent(
                    deathIntent,
                    *attackIntents[enemyIndex])) {
                break;
            }
        }
    }

    ActionPresentationTimeline timeline = builder.build();
    if (level) {
        appendControlTracks(timeline, action, *level, elevators, buttonPulse);
    }
    return timeline;
}

float GameplayPresentation::reverseDuration(
    const GameplaySession::Action& action) const
{
    return action.presentation.empty()
        ? std::max(action.durationSeconds, 0.0f)
        : action.presentation.durationSeconds;
}

void GameplayPresentation::beginAction(
    const GameplaySession::Action& action, const GameState& worldState)
{
    // Structural sync - creating visuals for players a mirror just made, and
    // dropping ones undo removed - comes from the world, not from this action's
    // snapshot of it. The two are the same value today, because state does not
    // advance until an action completes; they stop being the same the moment a
    // second action is in flight, and then rebuilding from one action's `before`
    // would snap the other action's entities back to where they started.
    syncToGameState(worldState);
    reverseSourceStartSeconds_ = action.reversed
        ? action.presentation.durationSeconds
        : 0.0f;
    // Face only the control groups participating in this action. Authored
    // heroes have distinct controller ids, while mirror copies inherit their
    // source's id. That keeps a blocked mirror copy visually tied to the copy
    // that moved without rotating unrelated, inactive heroes.
    if (action.facingDirection) {
        std::vector<EntityId> movingControllers;
        const std::size_t playerCount = std::min(
            action.before.players.size(), action.after.players.size());
        for (std::size_t index = 0; index < playerCount; ++index) {
            // A hero only carried by a platform did not walk anywhere.
            const GridPosition3 before = action.before.players[index].cell;
            const GridPosition3 after = action.after.players[index].cell;
            if (before == after ||
                rideOf(
                    platformMoves(action.before, action.after, 0.0f, nullptr),
                    before,
                    after) != nullptr) {
                continue;
            }
            const EntityId controller =
                rules::playerControllerId(action.before, index);
            if (std::ranges::find(movingControllers, controller) ==
                movingControllers.end()) {
                movingControllers.push_back(controller);
            }
        }

        for (std::size_t index = 0;
             index < action.before.players.size();
             ++index) {
            if (std::ranges::find(
                    movingControllers,
                    rules::playerControllerId(action.before, index)) ==
                movingControllers.end()) {
                continue;
            }
            const EntityTarget target = playerTarget(
                action.before.players[index], index);
            const auto visual = std::ranges::find_if(
                players_,
                [&](const PlayerVisual& candidate) {
                    return candidate.motion.target == target;
                });
            if (visual != players_.end()) {
                visual->facingQuarterTurns =
                    facingQuarterTurns(*action.facingDirection);
            }
        }
    }

    // Enemies are turned by rotators only until the board next changes: any
    // new action releases every held turn, so they go back to facing the
    // nearest hero. A forward action that turns an enemy then holds its new
    // turn. Undo never adds one; it just lets the enemy face the hero again.
    for (EnemyVisual& enemy : enemies_) {
        enemy.rotatorYawOffsetRadians = 0.0f;
    }
    if (!action.reversed) {
        const std::size_t turnedEnemyCount = std::min(
            action.before.enemies.size(), action.after.enemies.size());
        for (std::size_t index = 0; index < turnedEnemyCount; ++index) {
            const int turns =
                ((static_cast<int>(action.after.enemies[index].quarterTurns) -
                     static_cast<int>(
                         action.before.enemies[index].quarterTurns)) %
                        4 +
                    4) %
                4;
            if (turns == 0) {
                continue;
            }
            const EntityTarget target =
                enemyTarget(action.before.enemies[index], index);
            const auto visual = std::ranges::find_if(
                enemies_,
                [&](const EnemyVisual& candidate) {
                    return candidate.motion.target == target;
                });
            if (visual != enemies_.end()) {
                // 3 clockwise quarter turns read as one counter-clockwise.
                visual->rotatorYawOffsetRadians =
                    static_cast<float>(turns == 3 ? -1 : turns) * (pi * 0.5f);
            }
        }
    }

    // Heroes a rotator turns face their new direction from the start of the
    // action; the frame builder unwinds the turn until the action completes,
    // so the hero is seen turning rather than snapping.
    const std::size_t turnedPlayerCount = std::min(
        action.before.players.size(), action.after.players.size());
    for (std::size_t index = 0; index < turnedPlayerCount; ++index) {
        const int delta =
            static_cast<int>(action.after.players[index].quarterTurns) -
            static_cast<int>(action.before.players[index].quarterTurns);
        if (delta == 0) {
            continue;
        }
        const EntityTarget target = playerTarget(
            action.before.players[index], index);
        const auto visual = std::ranges::find_if(
            players_,
            [&](const PlayerVisual& candidate) {
                return candidate.motion.target == target;
            });
        if (visual != players_.end()) {
            visual->facingQuarterTurns = static_cast<uint32_t>(
                ((static_cast<int>(visual->facingQuarterTurns) + delta) % 4 +
                    4) % 4);
        }
    }
}

void GameplayPresentation::seekAction(
    const GameplaySession::Action& action,
    float elapsedSeconds)
{
    if (action.presentation.empty()) {
        return;
    }
    const ActionPresentationTimeline& timeline = action.presentation;
    const float elapsed = std::max(elapsedSeconds, 0.0f);
    const float sourceTime = std::clamp(
        action.reversed
            ? reverseSourceStartSeconds_ - elapsed
            : elapsed,
        0.0f,
        timeline.durationSeconds);
    for (std::size_t index = 0; index < timeline.controls.size(); ++index) {
        const ActionControlTrack& track = timeline.controls[index];
        std::size_t chosen = index;
        for (std::size_t other = 0; other < timeline.controls.size(); ++other) {
            const ActionControlTrack& candidate = timeline.controls[other];
            if (candidate.cell != track.cell) continue;
            const float current = timeline.controls[chosen].startSeconds;
            const bool candidateBegun = candidate.startSeconds <= sourceTime;
            const bool currentBegun = current <= sourceTime;
            if ((candidateBegun && (!currentBegun || candidate.startSeconds > current)) ||
                (!candidateBegun && !currentBegun && candidate.startSeconds < current)) {
                chosen = other;
            }
        }
        if (chosen != index) continue;
        const float progress = track.durationSeconds > 0.0f
            ? std::clamp((sourceTime - track.startSeconds) / track.durationSeconds, 0.0f, 1.0f)
            : (sourceTime >= track.startSeconds ? 1.0f : 0.0f);
        const float smooth = progress * progress * (3.0f - 2.0f * progress);
        const float activation = std::lerp(track.from, track.to, smooth);
        const auto visual = std::ranges::find(controls_, track.cell, &ControlVisual::cell);
        if (visual == controls_.end()) {
            controls_.push_back({ track.cell, activation });
        } else {
            visual->activation = activation;
        }
    }
    // Only the entities this action drives. Clearing every visual would be
    // fine while one action exists, but with two in flight whichever seeks last
    // would stop the other's entities dead every frame.
    //
    // Entities this action does not touch are not its business: either nothing
    // is moving them, or another action is, and that action clears and sets
    // them itself.
    for (const ActionMotionTrack& track : timeline.motions) {
        if (EntityVisual* visual = findMotionVisual(track.target)) {
            visual->moving = false;
        }
    }

    // One entity can own several motion tracks - a chained slide has one per
    // leg - and only the leg it is currently on may be applied.
    //
    // Applying all of them let the last one win, and a track whose leg has not
    // begun sets the entity to *that* leg's starting cell. So a block one tile
    // into a five-tile slide was drawn at the start of the final leg, which is
    // to say at its destination, for the entire slide. It then snapped back the
    // moment anything else re-synchronised it.
    const auto chosenTrackFor = [&](EntityTarget target) {
        std::size_t chosen = timeline.motions.size();
        for (std::size_t i = 0; i < timeline.motions.size(); ++i) {
            if (!(timeline.motions[i].target == target)) {
                continue;
            }
            if (chosen == timeline.motions.size()) {
                chosen = i;
                continue;
            }
            const float candidate = timeline.motions[i].startSeconds;
            const float current = timeline.motions[chosen].startSeconds;
            const bool candidateBegun = candidate <= sourceTime;
            const bool currentBegun = current <= sourceTime;
            // The latest leg that has begun; before any has, the earliest,
            // which is what holds the entity at the action's start pose.
            const bool isLatestBegun =
                candidateBegun && (!currentBegun || candidate > current);
            const bool isEarliestPending =
                !candidateBegun && !currentBegun && candidate < current;
            if (isLatestBegun || isEarliestPending) {
                chosen = i;
            }
        }
        return chosen;
    };

    for (std::size_t index = 0; index < timeline.motions.size(); ++index) {
        const ActionMotionTrack& track = timeline.motions[index];
        EntityVisual* visual = findMotionVisual(track.target);
        if (visual == nullptr) {
            continue;
        }
        if (chosenTrackFor(track.target) != index) {
            continue;
        }
        // Face along the physical leg on either side of a portal jump.
        // Ordinary continuous paths retain their control-group facing.
        if (track.target.kind == EntityKind::Player) {
            std::optional<Vec3> previousEnd;
            bool jumped = false;
            for (const auto& candidate : timeline.motions) {
                if (candidate.target != track.target) continue;
                jumped = jumped ||
                    (previousEnd &&
                     length(candidate.from - *previousEnd) > 0.001f);
                previousEnd = candidate.to;
            }
            if (jumped) {
                const auto player =
                    std::ranges::find_if(players_, [&](const auto& candidate) {
                        return candidate.motion.target == track.target;
                    });
                const Vec3 delta = track.to - track.from;
                if (player != players_.end() && std::abs(delta.x) > 0.001f) {
                    player->facingQuarterTurns = facingQuarterTurns(
                        delta.x > 0 ? MoveDirection::Right
                                    : MoveDirection::Left);
                } else if (
                    player != players_.end() && std::abs(delta.y) > 0.001f) {
                    player->facingQuarterTurns = facingQuarterTurns(
                        delta.y > 0 ? MoveDirection::Down : MoveDirection::Up);
                }
            }
        }
        const float end = track.startSeconds + track.durationSeconds;
        if (sourceTime < track.startSeconds ||
            (action.reversed && sourceTime <= track.startSeconds) ||
            track.durationSeconds <= 0.0f) {
            setImmediatePosition(*visual, track.from);
            continue;
        }
        if (sourceTime >= end) {
            setImmediatePosition(*visual, track.to);
            continue;
        }
        const float motionElapsed = sourceTime - track.startSeconds;
        const float distance = gridDistance(track.from, track.to);
        visual->animationStart = track.from;
        visual->animationEnd = track.to;
        visual->animationElapsed = motionElapsed;
        visual->animationDuration = track.durationSeconds;
        visual->animationSecondsPerTile = distance > 0.0001f
            ? track.durationSeconds / distance
            : 0.0f;
        visual->renderPosition = interpolateGridMotion(
            track.from,
            track.to,
            motionElapsed,
            visual->animationSecondsPerTile);
        visual->moving = distance > 0.0001f;
    }

    for (const ActionAnimationTrack& track : timeline.animations) {
        AnimatedActorVisual* visual = findAnimatedVisual(track.target);
        if (visual == nullptr) {
            continue;
        }
        selectAnimationSample(
            *visual,
            track.initialUse,
            track.initialClipTimeSeconds + sourceTime);
        visual->animationFallbackUse.reset();
        visual->animationLoops = true;
        visual->animationCrossfades = !action.reversed;
        visual->clipPlaybackRate = action.reversed ? -1.0f : 1.0f;

        for (const ActionAnimationSegment& segment : track.segments) {
            if (sourceTime < segment.startSeconds) {
                break;
            }
            const float end = segment.startSeconds + segment.durationSeconds;
            if (sourceTime < end) {
                selectAnimationSample(
                    *visual,
                    segment.use,
                    segment.clipStartSeconds +
                        sourceTime - segment.startSeconds);
                visual->animationFallbackUse = segment.fallbackUse;
                visual->animationLoops = segment.loops;
                continue;
            }
            // Commit the sampled phase even though the completion pose uses a
            // different clip. The next action can then continue this loop
            // instead of borrowing the idle clock and restarting at zero.
            visual->setClipTimeFor(
                segment.use,
                segment.clipStartSeconds + segment.durationSeconds);
            selectAnimationSample(
                *visual,
                segment.completionUse,
                sourceTime - end);
            visual->animationFallbackUse.reset();
            visual->animationLoops = true;
        }
    }
}

void GameplayPresentation::finishAction(const GameState& state)
{
    syncToGameState(state);
    controls_.clear();
    reverseSourceStartSeconds_ = 0.0f;
}

std::optional<float> GameplayPresentation::controlActivation(GridPosition3 cell) const
{
    const auto visual = std::ranges::find(controls_, cell, &ControlVisual::cell);
    return visual == controls_.end() ? std::nullopt
                                    : std::optional<float>(visual->activation);
}

void GameplayPresentation::syncToGameState(const GameState& state)
{
    std::vector<PlayerVisual> oldPlayers = std::move(players_);
    players_.clear();
    players_.reserve(state.players.size());
    for (std::size_t index = 0; index < state.players.size(); ++index) {
        const GameState::Player& player = state.players[index];
        const EntityTarget target = playerTarget(player, index);
        auto existing = std::ranges::find_if(
            oldPlayers,
            [&](const PlayerVisual& candidate) {
                return candidate.motion.target == target;
            });
        PlayerVisual visual;
        if (existing != oldPlayers.end()) {
            visual = *existing;
        } else {
            visual.facingQuarterTurns = players_.empty()
                ? facingQuarterTurns(MoveDirection::Down)
                : players_.front().facingQuarterTurns;
        }
        visual.motion.target = target;
        setImmediatePosition(
            visual.motion,
            playerRenderTarget(player.cell, player.drowned));
        setRestAnimation(visual, playerRestAnimation(player));
        players_.push_back(visual);
    }

    std::vector<EntityVisual> oldMovables = std::move(movables_);
    movables_.clear();
    movables_.reserve(state.movables.size());
    for (std::size_t index = 0; index < state.movables.size(); ++index) {
        const GameState::Movable& movable = state.movables[index];
        const EntityTarget target = movableTarget(movable, index);
        auto existing = std::ranges::find(
            oldMovables,
            target,
            &EntityVisual::target);
        EntityVisual visual;
        if (existing != oldMovables.end()) {
            visual = *existing;
        }
        visual.target = target;
        setImmediatePosition(
            visual,
            movableRenderTarget(movable.cell, movable.fallen));
        movables_.push_back(visual);
    }

    std::vector<EnemyVisual> oldEnemies = std::move(enemies_);
    enemies_.clear();
    enemies_.reserve(state.enemies.size());
    for (std::size_t index = 0; index < state.enemies.size(); ++index) {
        const GameState::Enemy& enemy = state.enemies[index];
        const EntityTarget target = enemyTarget(enemy, index);
        auto existing = std::ranges::find_if(
            oldEnemies,
            [&](const EnemyVisual& candidate) {
                return candidate.motion.target == target;
            });
        EnemyVisual visual;
        if (existing != oldEnemies.end()) {
            visual = *existing;
        }
        visual.motion.target = target;
        setImmediatePosition(
            visual.motion,
            movableRenderTarget(enemy.cell, enemy.fallen));
        setRestAnimation(visual, AnimationUse::EnemyIdle);
        enemies_.push_back(visual);
    }

    elevators_.resize(state.elevators.size());
    for (std::size_t index = 0; index < state.elevators.size(); ++index) {
        elevators_[index].target = elevatorTarget(index);
        setImmediatePosition(
            elevators_[index], toVec3(state.elevators[index].cell));
    }
    minecarts_.resize(state.minecarts.size());
    for (std::size_t index = 0; index < state.minecarts.size(); ++index) {
        minecarts_[index].target = minecartTarget(index);
        setImmediatePosition(
            minecarts_[index], toVec3(state.minecarts[index].cell));
    }
}

std::vector<GameplaySoundCue> GameplayPresentation::buildActionSoundCues(
    const Level& level,
    const GameplaySession::Action& action,
    const std::vector<GameState>& legs,
    const std::vector<plans::PlannedAction::PortalCue>& portals,
    float mechanicalDurationSeconds) const
{
    std::vector<GameplaySoundCue> cues;
    if (action.reversed) {
        return cues;
    }
    const std::size_t legCount = std::max<std::size_t>(legs.size(), 1);
    const float stepSeconds = mechanicalDurationSeconds / static_cast<float>(legCount);
    float legStart = 0.0f;
    for (std::size_t leg = 0; leg < legCount; ++leg) {
        const GameState& before = leg == 0 ? action.before : legs[leg - 1];
        const GameState& after = legs.empty() ? action.after : legs[leg];
        const auto moves = platformMoves(before, after, stepSeconds, &level);
        const float legEnd = legStart + stepSeconds + platformExtraSeconds(moves);
        // Gates fade toward the projected state while the leg moves. Start
        // their sound with that fade, using the effective state so inverted
        // gates and gates held open by blocks choose the correct effect.
        for (const auto& gate : level.gates()) {
            const bool wasOpen = rules::isGateOpen(level, before, gate);
            const bool open = rules::isGateOpen(level, after, gate);
            if (wasOpen != open) {
                cues.push_back({ open ? GameplaySound::GateOpen
                                     : GameplaySound::GateClose, legStart });
            }
        }
        // Rules resolve boarding and platform travel together. Recover the
        // boarding pose so a plate can press, start its elevator, and release
        // when the rider leaves it, all within the same world-step leg.
        const GameState settled = boardingState(before, after, moves);
        const auto plateEdges = [&](const GameState& from, const GameState& to, float time) {
            for (const auto plate : level.pressurePlates()) {
                const TileType tile = level.plateAt(plate).value_or(TileType::Air);
                if (tileTypeIsButton(tile) || tileTypeIsLever(tile)) {
                    continue;
                }
                const bool wasPressed = rules::isPressurePlateActive(level, from, plate);
                const bool pressed = rules::isPressurePlateActive(level, to, plate);
                if (wasPressed != pressed) {
                    cues.push_back({ pressed ? GameplaySound::PressurePlatePress
                                            : GameplaySound::PressurePlateRelease, time });
                }
            }
        };
        plateEdges(before, settled, legStart + stepSeconds);
        plateEdges(settled, after, legEnd);
        // Activation plans have no legs. Each pulse is a fresh press, even
        // when consecutive Activate actions leave activeButtons identical.
        const bool pulse = legs.empty() && !after.activeButtons.empty();
        GameState engagementBefore = before;
        if (pulse) {
            engagementBefore.activeButtons.clear();
            for (std::size_t button = 0; button < after.activeButtons.size(); ++button) {
                cues.push_back({ GameplaySound::ButtonPress, legStart });
            }
        }
        // A toggle has one mechanical click in either direction. Reuse the
        // existing control press sound without pressure-plate edge sounds.
        for (const auto source : level.pressurePlates()) {
            if (tileTypeIsLever(level.plateAt(source).value_or(TileType::Air)) &&
                rules::isPressurePlateActive(level, before, source) !=
                    rules::isPressurePlateActive(level, after, source)) {
                cues.push_back({ GameplaySound::ButtonPress, legStart });
            }
        }
        for (const auto& rotator : level.rotators()) {
            if (!rules::isRotatorEngaged(level, engagementBefore, rotator) &&
                rules::isRotatorEngaged(level, settled, rotator) &&
                rules::isPressurePlateActive(level, settled, rotator.cell)) {
                cues.push_back({ GameplaySound::RotatorTurn, legStart + stepSeconds });
            }
        }
        legStart = legEnd;
    }

    const auto& motions = action.presentation.motions;
    for (std::size_t index = 0; index < motions.size(); ++index) {
        const auto& motion = motions[index];
        if (motion.target.kind == EntityKind::Minecart) {
            const GridPosition3 destination {
                static_cast<int>(std::lround(motion.to.x)),
                static_cast<int>(std::lround(motion.to.y)),
                static_cast<int>(std::lround(motion.to.z)),
            };
            if (level.tileAt(destination.x, destination.y, destination.z) == TileType::MinecartGate &&
                length(motion.to - motion.from) > 0.001f) {
                // Match MinecartGateVisuals: opening begins when the cart
                // reaches 0.95 tiles from the barrier on its incoming segment.
                cues.push_back({ GameplaySound::MinecartGateOpen,
                    motion.startSeconds + motion.durationSeconds * 0.05f });
            }
        }
        if (portals.empty()) {
            continue;
        }
        const ActionMotionTrack* previous = nullptr;
        for (std::size_t prior = 0; prior < index; ++prior) {
            if (motions[prior].target == motion.target) {
                previous = &motions[prior];
            }
        }
        if (previous == nullptr) {
            continue;
        }
        for (const auto& portal : portals) {
            if (portal.transit.target != motion.target) {
                continue;
            }
            const auto entry = rules::directionOffset(portal.transit.entryDirection);
            const auto exit = portalEdgeOffset(*level.plateAt(portal.transit.exit));
            const auto mouth = toVec3(portal.transit.entrance) +
                Vec3 { static_cast<float>(entry.x) * 0.5f,
                       static_cast<float>(entry.y) * 0.5f, 0.0f };
            const auto emerged = toVec3(portal.transit.exit) +
                Vec3 { static_cast<float>(exit.x) * 0.5f,
                       static_cast<float>(exit.y) * 0.5f, 0.0f };
            if (length(previous->to - mouth) < 0.001f &&
                length(motion.from - emerged) < 0.001f) {
                cues.push_back({ GameplaySound::PortalTravel, motion.startSeconds });
                break;
            }
        }
    }
    std::ranges::stable_sort(cues, {}, &GameplaySoundCue::triggerSeconds);
    return cues;
}

float GameplayPresentation::conveyorBeltScrollOffset(
    float stepDurationSeconds) const
{
    if (stepDurationSeconds <= 0.0f) {
        return 0.0f;
    }
    return std::fmod(
        worldAnimationTimeSeconds_ / stepDurationSeconds,
        1.0f);
}

void GameplayPresentation::setImmediatePosition(
    EntityVisual& visual,
    Vec3 target)
{
    visual.renderPosition = target;
    visual.animationStart = target;
    visual.animationEnd = target;
    visual.animationElapsed = 0.0f;
    visual.animationDuration = 0.0f;
    visual.animationSecondsPerTile = 0.0f;
    visual.moving = false;
}

GameplayPresentation::EntityVisual* GameplayPresentation::findMotionVisual(
    EntityTarget target)
{
    if (target.kind == EntityKind::Player) {
        const auto found = std::ranges::find_if(
            players_,
            [&](const PlayerVisual& candidate) {
                return candidate.motion.target == target;
            });
        return found == players_.end() ? nullptr : &found->motion;
    }
    if (target.kind == EntityKind::Movable) {
        const auto found = std::ranges::find(
            movables_, target, &EntityVisual::target);
        return found == movables_.end() ? nullptr : &*found;
    }
    if (target.kind == EntityKind::Elevator) {
        const auto found = std::ranges::find(
            elevators_, target, &EntityVisual::target);
        return found == elevators_.end() ? nullptr : &*found;
    }
    if (target.kind == EntityKind::Minecart) {
        const auto found = std::ranges::find(
            minecarts_, target, &EntityVisual::target);
        return found == minecarts_.end() ? nullptr : &*found;
    }
    const auto found = std::ranges::find_if(
        enemies_,
        [&](const EnemyVisual& candidate) {
            return candidate.motion.target == target;
        });
    return found == enemies_.end() ? nullptr : &found->motion;
}

GameplayPresentation::AnimatedActorVisual*
GameplayPresentation::findAnimatedVisual(EntityTarget target)
{
    if (target.kind == EntityKind::Player) {
        const auto found = std::ranges::find_if(
            players_,
            [&](const PlayerVisual& candidate) {
                return candidate.motion.target == target;
            });
        return found == players_.end() ? nullptr : &*found;
    }
    if (target.kind == EntityKind::Enemy) {
        const auto found = std::ranges::find_if(
            enemies_,
            [&](const EnemyVisual& candidate) {
                return candidate.motion.target == target;
            });
        return found == enemies_.end() ? nullptr : &*found;
    }
    return nullptr;
}

} // namespace sokoban

#include "engine/PlayerProfile.hpp"

#include "engine/render/RenderResolution.hpp"
#include "engine/render/WaterConfig.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <utility>

// Serialization, strict parsing, and migrations for PlayerProfile. The model
// itself lives in PlayerProfile.cpp.
//
// Migration strategy: old formats are upgraded by forward JSON patches
// (migrate1to2 .. migrate16to17) applied in sequence, then a single strict
// current-format parse validates the fully migrated document. Patches only move
// fields and add defaults; unknown keys survive them and are rejected by the
// final parse, so schema strictness is preserved without every historical
// format keeping its own parser.
//
// Format 9 introduced optional top-level "progress" and "settings" sections.
// Format 10 added Mirror bindings and temporarily moved the default Undo key.
// Format 11 restores Undo to Z and moves the default Mirror action to F.
// Format 16 persists presentation timelines with undo actions.
// Format 17 introduces stable entity ids and generic presentation tracks.
// Save-slot files carry only progress and the shared settings.json
// carries only settings (ProfileSections selects the shape at serialize
// time); pre-split combined files simply contain both.

namespace sokoban {
namespace {

using Json = nlohmann::json;
using OrderedJson = nlohmann::ordered_json;

[[noreturn]] void fail(std::string_view context, const std::string& message)
{
    throw std::runtime_error(
        "player profile " + std::string(context) + ": " + message);
}

void requireObject(const Json& value, std::string_view context)
{
    if (!value.is_object()) {
        fail(context, "expected an object");
    }
}

void rejectUnknownProperties(
    const Json& object,
    std::initializer_list<std::string_view> allowed,
    std::string_view context)
{
    requireObject(object, context);
    for (const auto& [key, value] : object.items()) {
        (void)value;
        const bool known = std::ranges::any_of(allowed, [&](std::string_view candidate) {
            return key == candidate;
        });
        if (!known) {
            fail(context, "unknown property '" + key + "'");
        }
    }
}

const Json& requiredProperty(
    const Json& object,
    std::string_view key,
    std::string_view context)
{
    const auto found = object.find(std::string(key));
    if (found == object.end()) {
        fail(context, "missing required property '" + std::string(key) + "'");
    }
    return *found;
}

int integerProperty(
    const Json& object,
    std::string_view key,
    std::string_view context)
{
    const Json& value = requiredProperty(object, key, context);
    if (!value.is_number_integer()) {
        fail(context, "property '" + std::string(key) + "' must be an integer");
    }
    if (value.is_number_unsigned()) {
        const uint64_t number = value.get<uint64_t>();
        if (number > static_cast<uint64_t>(std::numeric_limits<int>::max())) {
            fail(context, "property '" + std::string(key) + "' is out of range");
        }
        return static_cast<int>(number);
    }
    const int64_t number = value.get<int64_t>();
    if (number < std::numeric_limits<int>::min() ||
        number > std::numeric_limits<int>::max()) {
        fail(context, "property '" + std::string(key) + "' is out of range");
    }
    return static_cast<int>(number);
}

int nonNegativeIntegerProperty(
    const Json& object,
    std::string_view key,
    std::string_view context)
{
    const int value = integerProperty(object, key, context);
    if (value < 0) {
        fail(context, "property '" + std::string(key) + "' must not be negative");
    }
    return value;
}

uint64_t unsignedIntegerProperty(
    const Json& object,
    std::string_view key,
    std::string_view context)
{
    const Json& value = requiredProperty(object, key, context);
    if (!value.is_number_integer()) {
        fail(context, "property '" + std::string(key) + "' must be an integer");
    }
    if (value.is_number_unsigned()) {
        return value.get<uint64_t>();
    }
    const int64_t number = value.get<int64_t>();
    if (number < 0) {
        fail(context, "property '" + std::string(key) + "' must not be negative");
    }
    return static_cast<uint64_t>(number);
}

std::optional<int> optionalNonNegativeInteger(
    const Json& object,
    std::string_view key,
    std::string_view context)
{
    if (!object.contains(std::string(key))) {
        return std::nullopt;
    }
    return nonNegativeIntegerProperty(object, key, context);
}

bool boolProperty(
    const Json& object,
    std::string_view key,
    std::string_view context)
{
    const Json& value = requiredProperty(object, key, context);
    if (!value.is_boolean()) {
        fail(context, "property '" + std::string(key) + "' must be a boolean");
    }
    return value.get<bool>();
}

float floatProperty(
    const Json& object,
    std::string_view key,
    std::string_view context)
{
    const Json& value = requiredProperty(object, key, context);
    if (!value.is_number()) {
        fail(context, "property '" + std::string(key) + "' must be a number");
    }
    const float result = value.get<float>();
    if (!std::isfinite(result)) {
        fail(context, "property '" + std::string(key) + "' must be finite");
    }
    return result;
}

std::optional<double> optionalNonNegativeDouble(
    const Json& object,
    std::string_view key,
    std::string_view context)
{
    const auto found = object.find(std::string(key));
    if (found == object.end()) {
        return std::nullopt;
    }
    if (!found->is_number()) {
        fail(context, "property '" + std::string(key) + "' must be a number");
    }
    const double result = found->get<double>();
    if (!std::isfinite(result) || result < 0.0) {
        fail(context, "property '" + std::string(key) + "' must be finite and non-negative");
    }
    return result;
}

std::string stringProperty(
    const Json& object,
    std::string_view key,
    std::string_view context)
{
    const Json& value = requiredProperty(object, key, context);
    if (!value.is_string()) {
        fail(context, "property '" + std::string(key) + "' must be a string");
    }
    std::string result = value.get<std::string>();
    if (result.empty()) {
        fail(context, "property '" + std::string(key) + "' must not be empty");
    }
    return result;
}

std::optional<MoveDirection> directionFromJson(
    const Json& value,
    std::string_view context)
{
    if (value.is_null()) {
        return std::nullopt;
    }
    if (!value.is_string()) {
        fail(context, "expected a direction string or null");
    }
    const std::string direction = value.get<std::string>();
    if (direction == "up") {
        return MoveDirection::Up;
    }
    if (direction == "down") {
        return MoveDirection::Down;
    }
    if (direction == "left") {
        return MoveDirection::Left;
    }
    if (direction == "right") {
        return MoveDirection::Right;
    }
    fail(context, "unknown direction '" + direction + "'");
}

const char* directionName(MoveDirection direction)
{
    switch (direction) {
    case MoveDirection::Up: return "up";
    case MoveDirection::Down: return "down";
    case MoveDirection::Left: return "left";
    case MoveDirection::Right: return "right";
    }
    throw std::runtime_error("unknown move direction");
}

GridPosition3 positionFromJson(const Json& value, std::string_view context)
{
    rejectUnknownProperties(value, { "x", "y", "z" }, context);
    return {
        integerProperty(value, "x", context),
        integerProperty(value, "y", context),
        integerProperty(value, "z", context),
    };
}

OrderedJson positionToJson(GridPosition3 position)
{
    return {
        { "x", position.x },
        { "y", position.y },
        { "z", position.z },
    };
}

TileType tileTypeFromName(std::string_view name, std::string_view context)
{
    for (const TileTypeDefinition& definition : tileTypeDefinitions()) {
        if (definition.name == name) {
            return definition.type;
        }
    }
    fail(context, "unknown tile type '" + std::string(name) + "'");
}

GameState gameStateFromJson(const Json& value, std::string_view context)
{
    rejectUnknownProperties(
        value,
        { "players", "movables", "enemies" },
        context);
    GameState state;
    const Json& players = requiredProperty(value, "players", context);
    if (!players.is_array() || players.empty()) {
        fail(context, "property 'players' must be a non-empty array");
    }
    for (std::size_t i = 0; i < players.size(); ++i) {
        const std::string playerContext =
            std::string(context) + ".players[" +
            std::to_string(i) + "]";
        const Json& item = players[i];
        rejectUnknownProperties(
            item,
            { "id", "cell", "dead", "drowned", "sliding" },
            playerContext);
        state.players.push_back({
            .id = unsignedIntegerProperty(item, "id", playerContext),
            .cell = positionFromJson(
                requiredProperty(item, "cell", playerContext),
                playerContext + ".cell"),
            .dead = boolProperty(item, "dead", playerContext),
            .drowned = boolProperty(item, "drowned", playerContext),
            .sliding = directionFromJson(
                requiredProperty(item, "sliding", playerContext),
                playerContext + ".sliding"),
        });
    }

    const Json& movables = requiredProperty(value, "movables", context);
    if (!movables.is_array()) {
        fail(context, "property 'movables' must be an array");
    }
    for (std::size_t i = 0; i < movables.size(); ++i) {
        const std::string movableContext =
            std::string(context) + ".movables[" + std::to_string(i) + "]";
        const Json& item = movables[i];
        rejectUnknownProperties(item, { "id", "type", "cell", "fallen", "sliding" }, movableContext);
        GameState::Movable movable;
        movable.id = unsignedIntegerProperty(item, "id", movableContext);
        movable.type = tileTypeFromName(
            stringProperty(item, "type", movableContext),
            movableContext + ".type");
        movable.cell = positionFromJson(
            requiredProperty(item, "cell", movableContext),
            movableContext + ".cell");
        movable.fallen = boolProperty(item, "fallen", movableContext);
        movable.sliding = directionFromJson(
            requiredProperty(item, "sliding", movableContext),
            movableContext + ".sliding");
        state.movables.push_back(std::move(movable));
    }
    const Json& enemies = requiredProperty(value, "enemies", context);
    if (!enemies.is_array()) {
        fail(context, "property 'enemies' must be an array");
    }
    for (std::size_t i = 0; i < enemies.size(); ++i) {
        const std::string enemyContext =
            std::string(context) + ".enemies[" + std::to_string(i) + "]";
        const Json& item = enemies[i];
        rejectUnknownProperties(item, { "id", "cell", "fallen" }, enemyContext);
        state.enemies.push_back({
            .id = unsignedIntegerProperty(item, "id", enemyContext),
            .cell = positionFromJson(
                requiredProperty(item, "cell", enemyContext),
                enemyContext + ".cell"),
            .fallen = boolProperty(item, "fallen", enemyContext),
        });
    }
    return state;
}

OrderedJson gameStateToJson(const GameState& state)
{
    OrderedJson players = OrderedJson::array();
    for (const GameState::Player& player : state.players) {
        players.push_back({
            { "id", player.id },
            { "cell", positionToJson(player.cell) },
            { "dead", player.dead },
            { "drowned", player.drowned },
            { "sliding", player.sliding
                ? OrderedJson(directionName(*player.sliding))
                : OrderedJson(nullptr) },
        });
    }
    OrderedJson movables = OrderedJson::array();
    for (const GameState::Movable& movable : state.movables) {
        movables.push_back({
            { "id", movable.id },
            { "type", tileTypeName(movable.type) },
            { "cell", positionToJson(movable.cell) },
            { "fallen", movable.fallen },
            { "sliding", movable.sliding
                ? OrderedJson(directionName(*movable.sliding))
                : OrderedJson(nullptr) },
        });
    }
    OrderedJson enemies = OrderedJson::array();
    for (const GameState::Enemy& enemy : state.enemies) {
        enemies.push_back({
            { "id", enemy.id },
            { "cell", positionToJson(enemy.cell) },
            { "fallen", enemy.fallen },
        });
    }
    return {
        { "players", std::move(players) },
        { "movables", std::move(movables) },
        { "enemies", std::move(enemies) },
    };
}

Vec3 vec3FromJson(const Json& value, std::string_view context)
{
    rejectUnknownProperties(value, { "x", "y", "z" }, context);
    return {
        floatProperty(value, "x", context),
        floatProperty(value, "y", context),
        floatProperty(value, "z", context),
    };
}

OrderedJson vec3ToJson(Vec3 value)
{
    return {
        { "x", value.x },
        { "y", value.y },
        { "z", value.z },
    };
}

EntityKind entityKindFromJson(const Json& value, std::string_view context)
{
    const std::string kind = stringProperty(value, "kind", context);
    if (kind == "player") {
        return EntityKind::Player;
    }
    if (kind == "movable") {
        return EntityKind::Movable;
    }
    if (kind == "enemy") {
        return EntityKind::Enemy;
    }
    fail(context, "unknown entity kind '" + kind + "'");
}

std::string_view entityKindName(EntityKind kind)
{
    switch (kind) {
    case EntityKind::Player:
        return "player";
    case EntityKind::Movable:
        return "movable";
    case EntityKind::Enemy:
        return "enemy";
    }
    return "player";
}

EntityTarget entityTargetFromJson(const Json& value, std::string_view context)
{
    rejectUnknownProperties(value, { "kind", "id" }, context);
    const EntityTarget target {
        .kind = entityKindFromJson(value, context),
        .id = unsignedIntegerProperty(value, "id", context),
    };
    if (target.id == invalidEntityId) {
        fail(context, "entity id must not be zero");
    }
    return target;
}

OrderedJson entityTargetToJson(EntityTarget target)
{
    return {
        { "kind", entityKindName(target.kind) },
        { "id", target.id },
    };
}

AnimationUse animationUseFromJson(
    const Json& value,
    std::string_view key,
    std::string_view context)
{
    const std::string id = stringProperty(value, key, context);
    const auto definitions = animationUseDefinitions();
    const auto found = std::ranges::find_if(
        definitions,
        [&](const AnimationUseDefinition& definition) {
            return definition.id == id;
        });
    if (found == definitions.end()) {
        fail(context, "unknown animation use '" + id + "'");
    }
    return found->use;
}

GameplaySession::Action undoActionFromJson(
    const Json& value,
    std::string_view context)
{
    rejectUnknownProperties(value, {
        "before", "after", "playerPushing", "moveCountBefore",
        "moveCountAfter", "presentation",
    }, context);
    GameplaySession::Action action;
    action.before = gameStateFromJson(
        requiredProperty(value, "before", context),
        std::string(context) + ".before");
    action.after = gameStateFromJson(
        requiredProperty(value, "after", context),
        std::string(context) + ".after");
    action.playerPushing = boolProperty(value, "playerPushing", context);
    action.playerMoveCountBefore =
        nonNegativeIntegerProperty(value, "moveCountBefore", context);
    action.playerMoveCountAfter =
        nonNegativeIntegerProperty(value, "moveCountAfter", context);

    const Json& presentation = requiredProperty(value, "presentation", context);
    const std::string presentationContext =
        std::string(context) + ".presentation";
    rejectUnknownProperties(presentation, {
        "durationSeconds", "motions", "animations",
    }, presentationContext);
    action.presentation.durationSeconds =
        floatProperty(presentation, "durationSeconds", presentationContext);
    if (action.presentation.durationSeconds < 0.0f) {
        fail(presentationContext, "timeline duration must be non-negative");
    }

    const Json& motions =
        requiredProperty(presentation, "motions", presentationContext);
    if (!motions.is_array()) {
        fail(presentationContext, "property 'motions' must be an array");
    }
    for (std::size_t i = 0; i < motions.size(); ++i) {
        const Json& encoded = motions[i];
        const std::string motionContext = presentationContext +
            ".motions[" + std::to_string(i) + "]";
        rejectUnknownProperties(encoded, {
            "target", "from", "to", "startSeconds", "durationSeconds",
        }, motionContext);
        ActionMotionTrack motion {
            .target = entityTargetFromJson(
                requiredProperty(encoded, "target", motionContext),
                motionContext + ".target"),
            .from = vec3FromJson(
                requiredProperty(encoded, "from", motionContext),
                motionContext + ".from"),
            .to = vec3FromJson(
                requiredProperty(encoded, "to", motionContext),
                motionContext + ".to"),
            .startSeconds = floatProperty(
                encoded, "startSeconds", motionContext),
            .durationSeconds = floatProperty(
                encoded, "durationSeconds", motionContext),
        };
        if (motion.startSeconds < 0.0f || motion.durationSeconds < 0.0f ||
            motion.startSeconds + motion.durationSeconds >
                action.presentation.durationSeconds + 0.0001f) {
            fail(motionContext, "motion track is outside the timeline");
        }
        action.presentation.motions.push_back(std::move(motion));
    }

    const Json& animations =
        requiredProperty(presentation, "animations", presentationContext);
    if (!animations.is_array()) {
        fail(presentationContext, "property 'animations' must be an array");
    }
    for (std::size_t i = 0; i < animations.size(); ++i) {
        const Json& encoded = animations[i];
        const std::string trackContext = presentationContext +
            ".animations[" + std::to_string(i) + "]";
        rejectUnknownProperties(encoded, {
            "target", "initialUse", "initialClipTimeSeconds", "segments",
        }, trackContext);
        ActionAnimationTrack track {
            .target = entityTargetFromJson(
                requiredProperty(encoded, "target", trackContext),
                trackContext + ".target"),
            .initialUse = animationUseFromJson(
                encoded, "initialUse", trackContext),
            .initialClipTimeSeconds = floatProperty(
                encoded, "initialClipTimeSeconds", trackContext),
        };
        if (track.initialClipTimeSeconds < 0.0f) {
            fail(trackContext, "initial clip time must be non-negative");
        }
        const Json& segments = requiredProperty(encoded, "segments", trackContext);
        if (!segments.is_array()) {
            fail(trackContext, "property 'segments' must be an array");
        }
        for (std::size_t segmentIndex = 0;
             segmentIndex < segments.size();
             ++segmentIndex) {
            const Json& segmentJson = segments[segmentIndex];
            const std::string segmentContext = trackContext +
                ".segments[" + std::to_string(segmentIndex) + "]";
            rejectUnknownProperties(segmentJson, {
                "use", "completionUse", "fallbackUse", "startSeconds",
                "durationSeconds", "clipStartSeconds", "loops",
            }, segmentContext);
            ActionAnimationSegment segment {
                .use = animationUseFromJson(
                    segmentJson, "use", segmentContext),
                .completionUse = animationUseFromJson(
                    segmentJson, "completionUse", segmentContext),
                .startSeconds = floatProperty(
                    segmentJson, "startSeconds", segmentContext),
                .durationSeconds = floatProperty(
                    segmentJson, "durationSeconds", segmentContext),
                .clipStartSeconds = floatProperty(
                    segmentJson, "clipStartSeconds", segmentContext),
                .loops = boolProperty(segmentJson, "loops", segmentContext),
            };
            const Json& fallback = requiredProperty(
                segmentJson, "fallbackUse", segmentContext);
            if (!fallback.is_null()) {
                segment.fallbackUse = animationUseFromJson(
                    segmentJson, "fallbackUse", segmentContext);
            }
            if (segment.startSeconds < 0.0f ||
                segment.durationSeconds < 0.0f ||
                segment.clipStartSeconds < 0.0f ||
                segment.startSeconds + segment.durationSeconds >
                    action.presentation.durationSeconds + 0.0001f) {
                fail(segmentContext, "animation segment is outside the timeline");
            }
            track.segments.push_back(std::move(segment));
        }
        action.presentation.animations.push_back(std::move(track));
    }
    return action;
}

OrderedJson undoActionToJson(const GameplaySession::Action& action)
{
    OrderedJson motions = OrderedJson::array();
    for (const ActionMotionTrack& motion : action.presentation.motions) {
        motions.push_back({
            { "target", entityTargetToJson(motion.target) },
            { "from", vec3ToJson(motion.from) },
            { "to", vec3ToJson(motion.to) },
            { "startSeconds", motion.startSeconds },
            { "durationSeconds", motion.durationSeconds },
        });
    }
    OrderedJson animations = OrderedJson::array();
    for (const ActionAnimationTrack& track : action.presentation.animations) {
        OrderedJson segments = OrderedJson::array();
        for (const ActionAnimationSegment& segment : track.segments) {
            segments.push_back({
                { "use", animationUseId(segment.use) },
                { "completionUse", animationUseId(segment.completionUse) },
                { "fallbackUse", segment.fallbackUse
                    ? OrderedJson(animationUseId(*segment.fallbackUse))
                    : OrderedJson(nullptr) },
                { "startSeconds", segment.startSeconds },
                { "durationSeconds", segment.durationSeconds },
                { "clipStartSeconds", segment.clipStartSeconds },
                { "loops", segment.loops },
            });
        }
        animations.push_back({
            { "target", entityTargetToJson(track.target) },
            { "initialUse", animationUseId(track.initialUse) },
            { "initialClipTimeSeconds", track.initialClipTimeSeconds },
            { "segments", std::move(segments) },
        });
    }
    return {
        { "before", gameStateToJson(action.before) },
        { "after", gameStateToJson(action.after) },
        { "playerPushing", action.playerPushing },
        { "moveCountBefore", action.playerMoveCountBefore },
        { "moveCountAfter", action.playerMoveCountAfter },
        { "presentation", {
            { "durationSeconds", action.presentation.durationSeconds },
            { "motions", std::move(motions) },
            { "animations", std::move(animations) },
        } },
    };
}

GameplaySession::Snapshot sessionSnapshotFromJson(
    const Json& value,
    std::string_view context)
{
    rejectUnknownProperties(
        value,
        { "state", "undoStack", "playerMoveCount", "automaticMotionPaused" },
        context);
    GameplaySession::Snapshot snapshot;
    snapshot.state = gameStateFromJson(
        requiredProperty(value, "state", context),
        std::string(context) + ".state");
    snapshot.playerMoveCount =
        nonNegativeIntegerProperty(value, "playerMoveCount", context);
    snapshot.automaticMotionPaused =
        boolProperty(value, "automaticMotionPaused", context);
    const Json& undoStack = requiredProperty(value, "undoStack", context);
    if (!undoStack.is_array()) {
        fail(context, "property 'undoStack' must be an array");
    }
    for (std::size_t i = 0; i < undoStack.size(); ++i) {
        snapshot.undoStack.push_back(undoActionFromJson(
            undoStack[i],
            std::string(context) + ".undoStack[" + std::to_string(i) + "]"));
    }
    return snapshot;
}

OrderedJson sessionSnapshotToJson(const GameplaySession::Snapshot& snapshot)
{
    OrderedJson undoStack = OrderedJson::array();
    for (const GameplaySession::Action& action : snapshot.undoStack) {
        undoStack.push_back(undoActionToJson(action));
    }
    return {
        { "state", gameStateToJson(snapshot.state) },
        { "undoStack", std::move(undoStack) },
        { "playerMoveCount", snapshot.playerMoveCount },
        { "automaticMotionPaused", snapshot.automaticMotionPaused },
    };
}

void setLegacyKeyboardBinding(
    InputBindings& bindings,
    InputAction action,
    std::string scancode)
{
    std::vector<InputBinding>& actionBindings = bindings.forAction(action);
    for (InputBinding& binding : actionBindings) {
        if (KeyboardBinding* keyboard = std::get_if<KeyboardBinding>(&binding)) {
            keyboard->scancode = std::move(scancode);
            return;
        }
    }
    actionBindings.insert(
        actionBindings.begin(),
        KeyboardBinding { std::move(scancode) });
}

InputBinding inputBindingFromJson(const Json& value, std::string_view context)
{
    requireObject(value, context);
    const std::string type = stringProperty(value, "type", context);
    if (type == "keyboard") {
        rejectUnknownProperties(value, { "type", "control" }, context);
        return KeyboardBinding { stringProperty(value, "control", context) };
    }
    if (type == "gamepadButton") {
        rejectUnknownProperties(value, { "type", "control" }, context);
        const std::string control = stringProperty(value, "control", context);
        if (!isKnownGamepadButtonName(control)) {
            fail(context, "unknown gamepad button '" + control + "'");
        }
        return GamepadButtonBinding { control };
    }
    if (type == "gamepadAxis") {
        rejectUnknownProperties(
            value,
            { "type", "control", "direction", "threshold" },
            context);
        AxisDirection direction;
        try {
            direction = axisDirectionFromName(
                stringProperty(value, "direction", context));
        } catch (const std::invalid_argument& error) {
            fail(context, error.what());
        }
        const float threshold = floatProperty(value, "threshold", context);
        if (threshold < 0.1f || threshold > 1.0f) {
            fail(context, "axis threshold must be between 0.1 and 1.0");
        }
        const std::string control = stringProperty(value, "control", context);
        if (!isKnownGamepadAxisName(control)) {
            fail(context, "unknown gamepad axis '" + control + "'");
        }
        return GamepadAxisBinding {
            .axis = control,
            .direction = direction,
            .threshold = threshold,
        };
    }
    fail(context, "unknown binding type '" + type + "'");
}

OrderedJson inputBindingToJson(const InputBinding& binding)
{
    return std::visit([](const auto& value) -> OrderedJson {
        using Binding = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<Binding, KeyboardBinding>) {
            if (value.scancode.empty()) {
                throw std::runtime_error("player profile keyboard binding is empty");
            }
            return {
                { "type", "keyboard" },
                { "control", value.scancode },
            };
        } else if constexpr (std::is_same_v<Binding, GamepadButtonBinding>) {
            if (!isKnownGamepadButtonName(value.button)) {
                throw std::runtime_error(
                    "player profile gamepad button binding is invalid");
            }
            return {
                { "type", "gamepadButton" },
                { "control", value.button },
            };
        } else {
            if (!isKnownGamepadAxisName(value.axis) ||
                !std::isfinite(value.threshold) ||
                value.threshold < 0.1f || value.threshold > 1.0f) {
                throw std::runtime_error(
                    "player profile gamepad axis binding is invalid");
            }
            return {
                { "type", "gamepadAxis" },
                { "control", value.axis },
                { "direction", axisDirectionName(value.direction) },
                { "threshold", value.threshold },
            };
        }
    }, binding);
}

InputBindings inputBindingsFromJson(
    const Json& value,
    std::string_view context,
    bool includeMenuConfirm = true)
{
    if (includeMenuConfirm) {
        rejectUnknownProperties(value, {
            "moveUp", "moveDown", "moveLeft", "moveRight",
            "mirror", "undo", "restart", "showTopDownView",
            "menuBack", "menuConfirm",
        }, context);
    } else {
        rejectUnknownProperties(value, {
            "moveUp", "moveDown", "moveLeft", "moveRight",
            "mirror", "undo", "restart", "showTopDownView", "menuBack",
        }, context);
    }
    InputBindings result;
    if (!includeMenuConfirm) {
        result.forAction(InputAction::MenuConfirm) =
            defaultInputBindings().forAction(InputAction::MenuConfirm);
    }
    const std::size_t actionCount = includeMenuConfirm
        ? inputActionCount
        : static_cast<std::size_t>(InputAction::MenuConfirm);
    for (std::size_t i = 0; i < actionCount; ++i) {
        const InputAction action = static_cast<InputAction>(i);
        const std::string actionName(inputActionName(action));
        const Json& bindings = requiredProperty(value, actionName, context);
        if (!bindings.is_array() || bindings.empty()) {
            fail(context, "property '" + actionName + "' must be a non-empty array");
        }
        std::vector<InputBinding>& parsed = result.forAction(action);
        for (std::size_t bindingIndex = 0; bindingIndex < bindings.size(); ++bindingIndex) {
            const std::string bindingContext = std::string(context) + "." +
                actionName + "[" + std::to_string(bindingIndex) + "]";
            InputBinding binding = inputBindingFromJson(bindings[bindingIndex], bindingContext);
            if (std::ranges::find(parsed, binding) != parsed.end()) {
                fail(bindingContext, "duplicate binding");
            }
            parsed.push_back(std::move(binding));
        }
    }
    return result;
}

OrderedJson inputBindingsToJson(const InputBindings& bindings)
{
    OrderedJson result = OrderedJson::object();
    for (std::size_t i = 0; i < inputActionCount; ++i) {
        const InputAction action = static_cast<InputAction>(i);
        OrderedJson actionBindings = OrderedJson::array();
        for (const InputBinding& binding : bindings.forAction(action)) {
            actionBindings.push_back(inputBindingToJson(binding));
        }
        if (actionBindings.empty()) {
            throw std::runtime_error(
                "player profile action '" + std::string(inputActionName(action)) +
                "' has no bindings");
        }
        result[std::string(inputActionName(action))] = std::move(actionBindings);
    }
    return result;
}

Json legacyInputDefaultsJson()
{
    return {
        { "moveUp", "W" },
        { "moveDown", "S" },
        { "moveLeft", "A" },
        { "moveRight", "D" },
        { "undo", "Z" },
        { "restart", "R" },
    };
}

// ---- Forward migrations ----------------------------------------------------

// Format 1 was a flat document; nest it into the progress/settings shape.
void migrate1to2(Json& root)
{
    Json progress = Json::object();
    progress["unlockedLevel"] =
        nonNegativeIntegerProperty(root, "unlockedLevel", "root");
    progress["currentLevel"] =
        nonNegativeIntegerProperty(root, "currentLevel", "root");
    Json levels = Json::array();
    const Json& completed = requiredProperty(root, "completedLevels", "root");
    if (!completed.is_array()) {
        fail("root", "property 'completedLevels' must be an array");
    }
    for (std::size_t i = 0; i < completed.size(); ++i) {
        Json wrapper = { { "level", completed[i] } };
        const int level =
            nonNegativeIntegerProperty(wrapper, "level", "completedLevels");
        for (const Json& existing : levels) {
            if (existing.at("level").get<int>() == level) {
                fail("completedLevels", "duplicate level " + std::to_string(level));
            }
        }
        levels.push_back({ { "level", level }, { "completed", true } });
    }
    progress["levels"] = std::move(levels);

    Json audio = Json::object();
    audio["masterVolume"] = requiredProperty(root, "masterVolume", "root");
    audio["musicVolume"] = requiredProperty(root, "musicVolume", "root");
    audio["soundVolume"] = root.contains("soundVolume")
        ? root["soundVolume"]
        : Json(1.0f);

    root.erase("unlockedLevel");
    root.erase("currentLevel");
    root.erase("completedLevels");
    root.erase("masterVolume");
    root.erase("musicVolume");
    root.erase("soundVolume");

    root["progress"] = std::move(progress);
    root["settings"] = {
        { "audio", std::move(audio) },
        { "video", { { "fullscreen", false }, { "vsync", false } } },
        { "input", legacyInputDefaultsJson() },
        { "accessibility", {
            { "reducedMotion", false },
            { "highContrast", false },
            { "largeText", false },
            { "subtitles", true },
            { "screenShake", true },
        } },
    };
}

void migrate2to3(Json& root)
{
    Json& progress = root["progress"];
    if (progress.is_object()) {
        if (!progress.contains("currentScreen")) {
            progress["currentScreen"] = 0;
        }
        if (!progress.contains("activeScreen")) {
            progress["activeScreen"] = nullptr;
        }
    }
}

// Format 4 replaced the six legacy keyboard strings with typed bindings.
void migrate3to4(Json& root)
{
    Json& settings = root["settings"];
    if (!settings.is_object() || !settings.contains("input")) {
        return; // the final parse reports the missing section precisely
    }
    const Json legacy = settings["input"];
    InputBindings bindings = defaultInputBindings();
    setLegacyKeyboardBinding(bindings, InputAction::MoveUp,
        stringProperty(legacy, "moveUp", "settings.input"));
    setLegacyKeyboardBinding(bindings, InputAction::MoveDown,
        stringProperty(legacy, "moveDown", "settings.input"));
    setLegacyKeyboardBinding(bindings, InputAction::MoveLeft,
        stringProperty(legacy, "moveLeft", "settings.input"));
    setLegacyKeyboardBinding(bindings, InputAction::MoveRight,
        stringProperty(legacy, "moveRight", "settings.input"));
    setLegacyKeyboardBinding(bindings, InputAction::Undo,
        stringProperty(legacy, "undo", "settings.input"));
    setLegacyKeyboardBinding(bindings, InputAction::Restart,
        stringProperty(legacy, "restart", "settings.input"));
    settings["input"] = inputBindingsToJson(bindings);
}

// Format 5 added window/AA video settings and menu bindings.
void migrate4to5(Json& root)
{
    const UserSettings defaultSettings;
    Json& settings = root["settings"];
    if (!settings.is_object()) {
        return;
    }
    Json& video = settings["video"];
    if (video.is_object()) {
        if (!video.contains("antiAliasingSamples")) {
            video["antiAliasingSamples"] =
                defaultSettings.video.antiAliasingSamples;
        }
        if (!video.contains("ambientOcclusion")) {
            video["ambientOcclusion"] =
                defaultSettings.video.ambientOcclusion;
        }
        if (!video.contains("windowWidth")) {
            video["windowWidth"] = defaultSettings.video.windowWidth;
        }
        if (!video.contains("windowHeight")) {
            video["windowHeight"] = defaultSettings.video.windowHeight;
        }
    }
    Json& input = settings["input"];
    if (input.is_object()) {
        const OrderedJson defaults = inputBindingsToJson(defaultInputBindings());
        for (std::size_t i = 0; i < inputActionCount; ++i) {
            const std::string name(
                inputActionName(static_cast<InputAction>(i)));
            if (!input.contains(name)) {
                input[name] = Json::parse(defaults.at(name).dump());
            }
        }
    }
}

void migrate5to6(Json& root)
{
    Json& settings = root["settings"];
    if (settings.is_object() && settings["video"].is_object() &&
        !settings["video"].contains("renderScalePercent")) {
        settings["video"]["renderScalePercent"] = 100;
    }
}

void migrate6to7(Json& root)
{
    Json& settings = root["settings"];
    if (settings.is_object() && settings["video"].is_object()) {
        Json& video = settings["video"];
        if (!video.contains("customRenderScale")) {
            video["customRenderScale"] = false;
        }
        if (!video.contains("customRenderScalePercent")) {
            video["customRenderScalePercent"] = 100;
        }
    }
}

void migrate7to8(Json&)
{
    // Format 8 added optional per-level reachedScreens counts; absent counts
    // already parse as zero.
}

void migrate8to9(Json&)
{
    // Format 9 made the progress/settings sections optional; a combined
    // format-8 document is already a valid format-9 document.
}

void migrate9to10(Json& root)
{
    if (!root.contains("settings")) {
        return;
    }
    Json& settings = root["settings"];
    if (!settings.is_object() || !settings.contains("input") ||
        !settings["input"].is_object()) {
        return;
    }

    Json& input = settings["input"];
    const OrderedJson defaults = inputBindingsToJson(defaultInputBindings());
    const Json oldDefaultUndo = Json::array({
        Json { { "type", "keyboard" }, { "control", "Z" } },
        Json { { "type", "gamepadButton" }, { "control", "west" } },
    });
    if (input.contains("undo") && input["undo"] == oldDefaultUndo) {
        input["undo"] = Json::parse(defaults.at("undo").dump());
    }

    // Keep the one-binding/one-action invariant when introducing the new
    // defaults into profiles whose bindings may have been customized.
    const Json mirrorDefaults = Json::parse(defaults.at("mirror").dump());
    for (auto& [action, bindings] : input.items()) {
        if (action == "mirror" || !bindings.is_array()) {
            continue;
        }
        for (const Json& mirrorBinding : mirrorDefaults) {
            bindings.erase(std::remove(bindings.begin(), bindings.end(), mirrorBinding),
                bindings.end());
        }
        if (bindings.empty() && defaults.contains(action)) {
            bindings = Json::parse(defaults.at(action).dump());
        }
    }
    input["mirror"] = mirrorDefaults;
}

void migrate10to11(Json& root)
{
    if (!root.contains("settings")) {
        return;
    }
    Json& settings = root["settings"];
    if (!settings.is_object() || !settings.contains("input") ||
        !settings["input"].is_object()) {
        return;
    }

    Json& input = settings["input"];
    const Json oldDefaultMirror = Json::array({
        Json { { "type", "keyboard" }, { "control", "Z" } },
        Json { { "type", "gamepadButton" }, { "control", "east" } },
    });
    const Json oldDefaultUndo = Json::array({
        Json { { "type", "keyboard" }, { "control", "X" } },
        Json { { "type", "gamepadButton" }, { "control", "west" } },
    });
    const bool migrateMirror =
        input.contains("mirror") && input["mirror"] == oldDefaultMirror;
    const bool migrateUndo =
        input.contains("undo") && input["undo"] == oldDefaultUndo;
    if (!migrateMirror && !migrateUndo) {
        return;
    }

    const OrderedJson defaults = inputBindingsToJson(defaultInputBindings());
    if (!input.contains("showTopDownView")) {
        input["showTopDownView"] =
            Json::parse(defaults.at("showTopDownView").dump());
    }

    InputBindings bindings =
        inputBindingsFromJson(input, "settings.input");
    if (migrateMirror) {
        bindings.forAction(InputAction::Mirror) = {
            GamepadButtonBinding { "east" },
        };
    }
    if (migrateUndo) {
        bindings.forAction(InputAction::Undo) = {
            GamepadButtonBinding { "west" },
        };
    }

    auto bindingUsedOutside = [&bindings](
                                  InputAction action,
                                  const InputBinding& candidate) {
        for (std::size_t i = 0; i < inputActionCount; ++i) {
            if (static_cast<InputAction>(i) == action) {
                continue;
            }
            const auto& actionBindings = bindings.actions[i];
            if (std::ranges::find(actionBindings, candidate) !=
                actionBindings.end()) {
                return true;
            }
        }
        return false;
    };
    if (migrateMirror) {
        const InputBinding key = KeyboardBinding { "F" };
        if (!bindingUsedOutside(InputAction::Mirror, key)) {
            bindings.forAction(InputAction::Mirror).insert(
                bindings.forAction(InputAction::Mirror).begin(), key);
        }
    }
    if (migrateUndo) {
        const InputBinding key = KeyboardBinding { "Z" };
        if (!bindingUsedOutside(InputAction::Undo, key)) {
            bindings.forAction(InputAction::Undo).insert(
                bindings.forAction(InputAction::Undo).begin(), key);
        }
    }
    input = inputBindingsToJson(bindings);
}

void migrate11to12(Json& root)
{
    if (!root.contains("settings")) {
        return;
    }
    Json& settings = root["settings"];
    if (!settings.is_object() || !settings.contains("input") ||
        !settings["input"].is_object()) {
        return;
    }

    Json& input = settings["input"];
    if (input.contains("showTopDownView")) {
        return;
    }

    const OrderedJson defaults = inputBindingsToJson(defaultInputBindings());
    const Json topDownDefaults =
        Json::parse(defaults.at("showTopDownView").dump());

    // A physical control drives only one action. The newly introduced
    // default owns T, while any action emptied by that transfer recovers its
    // own defaults.
    for (auto& [action, bindings] : input.items()) {
        if (!bindings.is_array()) {
            continue;
        }
        for (const Json& topDownBinding : topDownDefaults) {
            bindings.erase(
                std::remove(bindings.begin(), bindings.end(), topDownBinding),
                bindings.end());
        }
        if (bindings.empty() && defaults.contains(action)) {
            bindings = Json::parse(defaults.at(action).dump());
        }
    }
    input["showTopDownView"] = topDownDefaults;
}

void migrate12to13(Json& root)
{
    if (!root.contains("settings") || !root["settings"].is_object()) {
        return;
    }
    Json& video = root["settings"]["video"];
    if (video.is_object() && !video.contains("ambientOcclusionStrength")) {
        video["ambientOcclusionStrength"] =
            UserSettings {}.video.ambientOcclusionStrength;
    }
}

void migrate13to14(Json& root)
{
    // Format 14 deliberately replaces the primary-player checkpoint shape.
    // Keep profile progress/settings, but restart an in-flight screen instead
    // of carrying the old runtime schema into the new model.
    if (root.contains("progress") && root["progress"].is_object()) {
        root["progress"]["activeScreen"] = nullptr;
    }
}

void migrate14to15(Json& root)
{
    // Enemy entities and explicit death causes change the authoritative
    // checkpoint schema. Progress remains valid; restart the active screen.
    if (root.contains("progress") && root["progress"].is_object()) {
        root["progress"]["activeScreen"] = nullptr;
    }
}

void migrate15to16(Json& root)
{
    if (!root.contains("progress") || !root["progress"].is_object()) {
        return;
    }
    Json& activeScreen = root["progress"]["activeScreen"];
    if (!activeScreen.is_object() || !activeScreen.contains("session")) {
        return;
    }
    Json& session = activeScreen["session"];
    if (!session.is_object() || !session.contains("undoStack") ||
        !session["undoStack"].is_array()) {
        return;
    }
    for (Json& action : session["undoStack"]) {
        if (!action.is_object() || action.contains("presentation")) {
            continue;
        }
        action["presentation"] = {
            { "durationSeconds", 0.0f },
            { "motionStartSeconds", 0.0f },
            { "motionDurationSeconds", 0.0f },
            { "animations", Json::array() },
        };
    }
}

void migrate16to17(Json& root)
{
    if (!root.contains("progress") || !root["progress"].is_object()) {
        return;
    }
    Json& activeScreen = root["progress"]["activeScreen"];
    if (!activeScreen.is_object() || !activeScreen.contains("session") ||
        !activeScreen["session"].is_object()) {
        return;
    }

    auto assignIds = [](Json& state) {
        if (!state.is_object()) {
            return;
        }
        constexpr uint64_t movableBase = uint64_t { 1 } << 20;
        constexpr uint64_t enemyBase = uint64_t { 2 } << 20;
        for (const auto [name, base] : {
                 std::pair { "players", uint64_t { 1 } },
                 std::pair { "movables", movableBase },
                 std::pair { "enemies", enemyBase },
             }) {
            if (!state.contains(name) || !state[name].is_array()) {
                continue;
            }
            for (std::size_t i = 0; i < state[name].size(); ++i) {
                if (state[name][i].is_object()) {
                    state[name][i]["id"] = base + i;
                }
            }
        }
    };

    auto targetJson = [](std::string_view kind, std::size_t index) {
        uint64_t id = index + 1;
        if (kind == "movable") {
            id = (uint64_t { 1 } << 20) + index;
        } else if (kind == "enemy") {
            id = (uint64_t { 2 } << 20) + index;
        }
        return Json { { "kind", kind }, { "id", id } };
    };

    auto renderPosition = [](const Json& entity, std::string_view kind) {
        const Json& cell = entity["cell"];
        float z = cell.value("z", 0.0f);
        if (kind == "player" && entity.value("drowned", false)) {
            z -= config::drownedPlayerDepthBelowGround;
        } else if (kind != "player" && entity.value("fallen", false)) {
            z -= config::waterDepthBelowGround;
        }
        return Json {
            { "x", cell.value("x", 0.0f) },
            { "y", cell.value("y", 0.0f) },
            { "z", z },
        };
    };

    Json& session = activeScreen["session"];
    if (session.contains("state")) {
        assignIds(session["state"]);
    }
    if (!session.contains("undoStack") || !session["undoStack"].is_array()) {
        return;
    }
    for (Json& action : session["undoStack"]) {
        if (!action.is_object() || !action.contains("before") ||
            !action.contains("after")) {
            continue;
        }
        assignIds(action["before"]);
        assignIds(action["after"]);
        if (!action.contains("presentation") ||
            !action["presentation"].is_object()) {
            continue;
        }
        Json& old = action["presentation"];
        const float duration = old.value("durationSeconds", 0.0f);
        const float motionStart = old.value("motionStartSeconds", 0.0f);
        const float motionDuration = old.value("motionDurationSeconds", 0.0f);
        Json motions = Json::array();
        for (const auto [arrayName, kind] : {
                 std::pair { "players", "player" },
                 std::pair { "movables", "movable" },
                 std::pair { "enemies", "enemy" },
             }) {
            const Json& before = action["before"][arrayName];
            const Json& after = action["after"][arrayName];
            const std::size_t count = std::min(before.size(), after.size());
            for (std::size_t i = 0; i < count; ++i) {
                const Json from = renderPosition(before[i], kind);
                const Json to = renderPosition(after[i], kind);
                if (from == to) {
                    continue;
                }
                motions.push_back({
                    { "target", targetJson(kind, i) },
                    { "from", from },
                    { "to", to },
                    { "startSeconds", motionStart },
                    { "durationSeconds", motionDuration },
                });
            }
        }

        Json tracks = Json::array();
        if (old.contains("animations") && old["animations"].is_array()) {
            for (const Json& span : old["animations"]) {
                if (!span.is_object()) {
                    continue;
                }
                const std::string kind = span.value("actorKind", "player");
                const std::size_t index = span.value("actorIndex", 0U);
                const std::string use = span.value("use", "player.idle");
                const bool death = use == "player.death";
                const bool attack = use == "enemy.attack";
                const bool motion = use == "player.move" || use == "player.push";
                std::string initialUse = kind == "enemy"
                    ? "enemy.idle"
                    : "player.idle";
                if (kind == "player" &&
                    action["before"]["players"].size() > index &&
                    action["before"]["players"][index].value("dead", false)) {
                    initialUse = "player.dead-idle";
                }
                const std::string completionUse = death
                    ? "player.dead-idle"
                    : (attack ? "enemy.idle" : "player.idle");
                tracks.push_back({
                    { "target", targetJson(kind, index) },
                    { "initialUse", initialUse },
                    { "initialClipTimeSeconds", 0.0f },
                    { "segments", Json::array({ {
                        { "use", use },
                        { "completionUse", completionUse },
                        { "fallbackUse", death
                            ? Json("player.dead-idle")
                            : (attack ? Json("enemy.idle") : Json(nullptr)) },
                        { "startSeconds", span.value("startSeconds", 0.0f) },
                        { "durationSeconds", span.value("durationSeconds", 0.0f) },
                        { "clipStartSeconds", span.value("clipStartSeconds", 0.0f) },
                        { "loops", motion },
                    } }) },
                });
            }
        }
        old = {
            { "durationSeconds", duration },
            { "motions", std::move(motions) },
            { "animations", std::move(tracks) },
        };
    }
}

// ---- Strict current-format parse -------------------------------------------

void parseProgressSection(PlayerProfile& profile, const Json& progress)
{
    rejectUnknownProperties(
        progress,
        { "unlockedLevel", "currentLevel", "currentScreen", "levels", "activeScreen" },
        "progress");
    profile.unlockedLevel =
        nonNegativeIntegerProperty(progress, "unlockedLevel", "progress");
    profile.currentLevel =
        nonNegativeIntegerProperty(progress, "currentLevel", "progress");
    profile.currentScreen =
        nonNegativeIntegerProperty(progress, "currentScreen", "progress");

    const Json& levels = requiredProperty(progress, "levels", "progress");
    if (!levels.is_array()) {
        fail("progress", "property 'levels' must be an array");
    }
    for (std::size_t i = 0; i < levels.size(); ++i) {
        const std::string context = "progress.levels[" + std::to_string(i) + "]";
        const Json& item = levels[i];
        rejectUnknownProperties(
            item,
            { "level", "completed", "reachedScreens", "bestMoves", "bestTimeSeconds" },
            context);
        PlayerProfile::LevelProgress level;
        level.level = nonNegativeIntegerProperty(item, "level", context);
        level.completed = boolProperty(item, "completed", context);
        level.reachedScreens =
            optionalNonNegativeInteger(item, "reachedScreens", context).value_or(0);
        level.bestMoves = optionalNonNegativeInteger(item, "bestMoves", context);
        level.bestTimeSeconds =
            optionalNonNegativeDouble(item, "bestTimeSeconds", context);
        if (!level.completed && (level.bestMoves || level.bestTimeSeconds)) {
            fail(context, "incomplete levels cannot have completion bests");
        }
        if (std::ranges::any_of(profile.levels, [&](const auto& existing) {
                return existing.level == level.level;
            })) {
            fail(context, "duplicate level " + std::to_string(level.level));
        }
        profile.levels.push_back(std::move(level));
    }

    const Json& activeScreen =
        requiredProperty(progress, "activeScreen", "progress");
    if (!activeScreen.is_null()) {
        rejectUnknownProperties(activeScreen, {
            "level", "screen", "completedLevelMoveCount",
            "levelElapsedSeconds", "session",
        }, "progress.activeScreen");
        PlayerProfile::ActiveScreen checkpoint;
        checkpoint.level = nonNegativeIntegerProperty(
            activeScreen, "level", "progress.activeScreen");
        checkpoint.screen = nonNegativeIntegerProperty(
            activeScreen, "screen", "progress.activeScreen");
        checkpoint.completedLevelMoveCount = nonNegativeIntegerProperty(
            activeScreen, "completedLevelMoveCount", "progress.activeScreen");
        checkpoint.levelElapsedSeconds = optionalNonNegativeDouble(
            activeScreen, "levelElapsedSeconds", "progress.activeScreen").value_or(0.0);
        checkpoint.session = sessionSnapshotFromJson(
            requiredProperty(activeScreen, "session", "progress.activeScreen"),
            "progress.activeScreen.session");
        if (checkpoint.level != profile.currentLevel ||
            checkpoint.screen != profile.currentScreen) {
            fail("progress.activeScreen",
                "checkpoint does not match current level and screen");
        }
        profile.activeScreen = std::move(checkpoint);
    }
}

void parseSettingsSection(PlayerProfile& profile, const Json& settings)
{
    rejectUnknownProperties(
        settings,
        { "audio", "video", "input", "accessibility" },
        "settings");

    const Json& audio = requiredProperty(settings, "audio", "settings");
    rejectUnknownProperties(
        audio,
        { "masterVolume", "musicVolume", "soundVolume" },
        "settings.audio");
    profile.settings.audio.masterVolume =
        floatProperty(audio, "masterVolume", "settings.audio");
    profile.settings.audio.musicVolume =
        floatProperty(audio, "musicVolume", "settings.audio");
    profile.settings.audio.soundVolume =
        floatProperty(audio, "soundVolume", "settings.audio");

    const Json& video = requiredProperty(settings, "video", "settings");
    rejectUnknownProperties(video, {
        "fullscreen", "vsync", "antiAliasingSamples", "renderScalePercent",
        "customRenderScale", "customRenderScalePercent", "ambientOcclusion",
        "ambientOcclusionStrength", "windowWidth", "windowHeight",
    }, "settings.video");
    profile.settings.video.fullscreen =
        boolProperty(video, "fullscreen", "settings.video");
    profile.settings.video.vsync =
        boolProperty(video, "vsync", "settings.video");
    profile.settings.video.antiAliasingSamples = nonNegativeIntegerProperty(
        video, "antiAliasingSamples", "settings.video");
    profile.settings.video.renderScalePercent = nonNegativeIntegerProperty(
        video, "renderScalePercent", "settings.video");
    profile.settings.video.customRenderScale = boolProperty(
        video, "customRenderScale", "settings.video");
    profile.settings.video.customRenderScalePercent = nonNegativeIntegerProperty(
        video, "customRenderScalePercent", "settings.video");
    profile.settings.video.ambientOcclusion = boolProperty(
        video, "ambientOcclusion", "settings.video");
    profile.settings.video.ambientOcclusionStrength = floatProperty(
        video, "ambientOcclusionStrength", "settings.video");
    profile.settings.video.windowWidth = nonNegativeIntegerProperty(
        video, "windowWidth", "settings.video");
    profile.settings.video.windowHeight = nonNegativeIntegerProperty(
        video, "windowHeight", "settings.video");

    profile.settings.input = inputBindingsFromJson(
        requiredProperty(settings, "input", "settings"),
        "settings.input");

    const Json& accessibility = requiredProperty(settings, "accessibility", "settings");
    rejectUnknownProperties(
        accessibility,
        { "reducedMotion", "highContrast", "largeText", "subtitles", "screenShake" },
        "settings.accessibility");
    profile.settings.accessibility.reducedMotion =
        boolProperty(accessibility, "reducedMotion", "settings.accessibility");
    profile.settings.accessibility.highContrast =
        boolProperty(accessibility, "highContrast", "settings.accessibility");
    profile.settings.accessibility.largeText =
        boolProperty(accessibility, "largeText", "settings.accessibility");
    profile.settings.accessibility.subtitles =
        boolProperty(accessibility, "subtitles", "settings.accessibility");
    profile.settings.accessibility.screenShake =
        boolProperty(accessibility, "screenShake", "settings.accessibility");
}

PlayerProfile parseCurrent(const Json& root)
{
    rejectUnknownProperties(root, { "format", "progress", "settings" }, "root");
    PlayerProfile profile;
    if (root.contains("progress")) {
        parseProgressSection(profile, root["progress"]);
    }
    if (root.contains("settings")) {
        parseSettingsSection(profile, root["settings"]);
    }
    profile.normalize();
    return profile;
}

} // namespace

std::string PlayerProfile::serialize(ProfileSections sections) const
{
    PlayerProfile normalized = *this;
    normalized.normalize();

    OrderedJson root = {
        { "format", currentPlayerProfileFormat },
    };

    if (sections != ProfileSections::SettingsOnly) {
        if (normalized.activeScreen &&
            (normalized.activeScreen->level != normalized.currentLevel ||
                normalized.activeScreen->screen != normalized.currentScreen ||
                normalized.activeScreen->completedLevelMoveCount < 0 ||
                !std::isfinite(normalized.activeScreen->levelElapsedSeconds) ||
                normalized.activeScreen->levelElapsedSeconds < 0.0)) {
            throw std::runtime_error(
                "player profile active screen checkpoint is invalid");
        }

        OrderedJson levelItems = OrderedJson::array();
        for (const LevelProgress& level : normalized.levels) {
            OrderedJson item = {
                { "level", level.level },
                { "completed", level.completed },
                { "reachedScreens", level.reachedScreens },
            };
            if (level.bestMoves) {
                item["bestMoves"] = *level.bestMoves;
            }
            if (level.bestTimeSeconds) {
                item["bestTimeSeconds"] = *level.bestTimeSeconds;
            }
            levelItems.push_back(std::move(item));
        }

        OrderedJson activeScreenJson = nullptr;
        if (normalized.activeScreen) {
            activeScreenJson = {
                { "level", normalized.activeScreen->level },
                { "screen", normalized.activeScreen->screen },
                { "completedLevelMoveCount", normalized.activeScreen->completedLevelMoveCount },
                { "levelElapsedSeconds", normalized.activeScreen->levelElapsedSeconds },
                { "session", sessionSnapshotToJson(normalized.activeScreen->session) },
            };
        }

        root["progress"] = {
            { "unlockedLevel", normalized.unlockedLevel },
            { "currentLevel", normalized.currentLevel },
            { "currentScreen", normalized.currentScreen },
            { "levels", std::move(levelItems) },
            { "activeScreen", std::move(activeScreenJson) },
        };
    }

    if (sections != ProfileSections::ProgressOnly) {
        root["settings"] = {
            { "audio", {
                { "masterVolume", normalized.settings.audio.masterVolume },
                { "musicVolume", normalized.settings.audio.musicVolume },
                { "soundVolume", normalized.settings.audio.soundVolume },
            } },
            { "video", {
                { "fullscreen", normalized.settings.video.fullscreen },
                { "vsync", normalized.settings.video.vsync },
                { "antiAliasingSamples", normalized.settings.video.antiAliasingSamples },
                { "renderScalePercent", normalized.settings.video.renderScalePercent },
                { "customRenderScale", normalized.settings.video.customRenderScale },
                { "customRenderScalePercent", normalized.settings.video.customRenderScalePercent },
                { "ambientOcclusion", normalized.settings.video.ambientOcclusion },
                { "ambientOcclusionStrength", normalized.settings.video.ambientOcclusionStrength },
                { "windowWidth", normalized.settings.video.windowWidth },
                { "windowHeight", normalized.settings.video.windowHeight },
            } },
            { "input", inputBindingsToJson(normalized.settings.input) },
            { "accessibility", {
                { "reducedMotion", normalized.settings.accessibility.reducedMotion },
                { "highContrast", normalized.settings.accessibility.highContrast },
                { "largeText", normalized.settings.accessibility.largeText },
                { "subtitles", normalized.settings.accessibility.subtitles },
                { "screenShake", normalized.settings.accessibility.screenShake },
            } },
        };
    }

    return root.dump(2) + '\n';
}

DecodedPlayerProfile decodePlayerProfile(std::string_view text)
{
    Json root;
    try {
        root = Json::parse(text);
    } catch (const Json::parse_error& error) {
        throw std::runtime_error(
            "player profile JSON parse error at byte " +
            std::to_string(error.byte) + ": " + error.what());
    }
    requireObject(root, "root");
    const int format = nonNegativeIntegerProperty(root, "format", "root");
    if (format < 1 || format > currentPlayerProfileFormat) {
        fail("root", "unsupported format " + std::to_string(format));
    }

    using Migration = void (*)(Json&);
    constexpr Migration migrations[] = {
        migrate1to2,
        migrate2to3,
        migrate3to4,
        migrate4to5,
        migrate5to6,
        migrate6to7,
        migrate7to8,
        migrate8to9,
        migrate9to10,
        migrate10to11,
        migrate11to12,
        migrate12to13,
        migrate13to14,
        migrate14to15,
        migrate15to16,
        migrate16to17,
    };
    static_assert(std::size(migrations) == currentPlayerProfileFormat - 1);

    Json migrated = root;
    for (int from = format; from < currentPlayerProfileFormat; ++from) {
        migrations[from - 1](migrated);
    }
    return { .profile = parseCurrent(migrated), .sourceFormat = format };
}

} // namespace sokoban

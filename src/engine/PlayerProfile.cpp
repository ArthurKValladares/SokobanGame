#include "engine/PlayerProfile.hpp"

#include "engine/render/RenderResolution.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <utility>

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
        { "player", "playerDead", "playerSliding", "movables" },
        context);
    GameState state;
    state.player = positionFromJson(
        requiredProperty(value, "player", context),
        std::string(context) + ".player");
    state.playerDead = boolProperty(value, "playerDead", context);
    state.playerSliding = directionFromJson(
        requiredProperty(value, "playerSliding", context),
        std::string(context) + ".playerSliding");

    const Json& movables = requiredProperty(value, "movables", context);
    if (!movables.is_array()) {
        fail(context, "property 'movables' must be an array");
    }
    for (std::size_t i = 0; i < movables.size(); ++i) {
        const std::string movableContext =
            std::string(context) + ".movables[" + std::to_string(i) + "]";
        const Json& item = movables[i];
        rejectUnknownProperties(item, { "type", "cell", "fallen", "sliding" }, movableContext);
        GameState::Movable movable;
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
    return state;
}

OrderedJson gameStateToJson(const GameState& state)
{
    OrderedJson movables = OrderedJson::array();
    for (const GameState::Movable& movable : state.movables) {
        movables.push_back({
            { "type", tileTypeName(movable.type) },
            { "cell", positionToJson(movable.cell) },
            { "fallen", movable.fallen },
            { "sliding", movable.sliding
                ? OrderedJson(directionName(*movable.sliding))
                : OrderedJson(nullptr) },
        });
    }
    return {
        { "player", positionToJson(state.player) },
        { "playerDead", state.playerDead },
        { "playerSliding", state.playerSliding
            ? OrderedJson(directionName(*state.playerSliding))
            : OrderedJson(nullptr) },
        { "movables", std::move(movables) },
    };
}

GameplaySession::Action undoActionFromJson(
    const Json& value,
    std::string_view context)
{
    rejectUnknownProperties(
        value,
        { "before", "after", "playerPushing", "moveCountBefore", "moveCountAfter" },
        context);
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
    return action;
}

OrderedJson undoActionToJson(const GameplaySession::Action& action)
{
    return {
        { "before", gameStateToJson(action.before) },
        { "after", gameStateToJson(action.after) },
        { "playerPushing", action.playerPushing },
        { "moveCountBefore", action.playerMoveCountBefore },
        { "moveCountAfter", action.playerMoveCountAfter },
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
            "undo", "restart", "menuBack", "menuConfirm",
        }, context);
    } else {
        rejectUnknownProperties(value, {
            "moveUp", "moveDown", "moveLeft", "moveRight",
            "undo", "restart", "menuBack",
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

PlayerProfile parseFormat2(const Json& root)
{
    rejectUnknownProperties(root, { "format", "progress", "settings" }, "root");
    const Json& progress = requiredProperty(root, "progress", "root");
    rejectUnknownProperties(
        progress,
        { "unlockedLevel", "currentLevel", "levels" },
        "progress");

    PlayerProfile profile;
    profile.unlockedLevel = nonNegativeIntegerProperty(progress, "unlockedLevel", "progress");
    profile.currentLevel = nonNegativeIntegerProperty(progress, "currentLevel", "progress");
    const Json& levels = requiredProperty(progress, "levels", "progress");
    if (!levels.is_array()) {
        fail("progress", "property 'levels' must be an array");
    }
    for (std::size_t i = 0; i < levels.size(); ++i) {
        const std::string context = "progress.levels[" + std::to_string(i) + "]";
        const Json& item = levels[i];
        rejectUnknownProperties(
            item,
            { "level", "completed", "bestMoves", "bestTimeSeconds" },
            context);
        PlayerProfile::LevelProgress level;
        level.level = nonNegativeIntegerProperty(item, "level", context);
        level.completed = boolProperty(item, "completed", context);
        level.bestMoves = optionalNonNegativeInteger(item, "bestMoves", context);
        level.bestTimeSeconds = optionalNonNegativeDouble(item, "bestTimeSeconds", context);
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

    const Json& settings = requiredProperty(root, "settings", "root");
    rejectUnknownProperties(
        settings,
        { "audio", "video", "input", "accessibility" },
        "settings");

    const Json& audio = requiredProperty(settings, "audio", "settings");
    rejectUnknownProperties(
        audio,
        { "masterVolume", "musicVolume", "soundVolume" },
        "settings.audio");
    profile.audio.masterVolume = floatProperty(audio, "masterVolume", "settings.audio");
    profile.audio.musicVolume = floatProperty(audio, "musicVolume", "settings.audio");
    profile.audio.soundVolume = floatProperty(audio, "soundVolume", "settings.audio");

    const Json& video = requiredProperty(settings, "video", "settings");
    rejectUnknownProperties(video, { "fullscreen", "vsync" }, "settings.video");
    profile.video.fullscreen = boolProperty(video, "fullscreen", "settings.video");
    profile.video.vsync = boolProperty(video, "vsync", "settings.video");

    const Json& input = requiredProperty(settings, "input", "settings");
    rejectUnknownProperties(
        input,
        { "moveUp", "moveDown", "moveLeft", "moveRight", "undo", "restart" },
        "settings.input");
    setLegacyKeyboardBinding(profile.input, InputAction::MoveUp,
        stringProperty(input, "moveUp", "settings.input"));
    setLegacyKeyboardBinding(profile.input, InputAction::MoveDown,
        stringProperty(input, "moveDown", "settings.input"));
    setLegacyKeyboardBinding(profile.input, InputAction::MoveLeft,
        stringProperty(input, "moveLeft", "settings.input"));
    setLegacyKeyboardBinding(profile.input, InputAction::MoveRight,
        stringProperty(input, "moveRight", "settings.input"));
    setLegacyKeyboardBinding(profile.input, InputAction::Undo,
        stringProperty(input, "undo", "settings.input"));
    setLegacyKeyboardBinding(profile.input, InputAction::Restart,
        stringProperty(input, "restart", "settings.input"));

    const Json& accessibility = requiredProperty(settings, "accessibility", "settings");
    rejectUnknownProperties(
        accessibility,
        { "reducedMotion", "highContrast", "largeText", "subtitles", "screenShake" },
        "settings.accessibility");
    profile.accessibility.reducedMotion =
        boolProperty(accessibility, "reducedMotion", "settings.accessibility");
    profile.accessibility.highContrast =
        boolProperty(accessibility, "highContrast", "settings.accessibility");
    profile.accessibility.largeText =
        boolProperty(accessibility, "largeText", "settings.accessibility");
    profile.accessibility.subtitles =
        boolProperty(accessibility, "subtitles", "settings.accessibility");
    profile.accessibility.screenShake =
        boolProperty(accessibility, "screenShake", "settings.accessibility");
    profile.normalize();
    return profile;
}

PlayerProfile parseFormat3(const Json& root)
{
    rejectUnknownProperties(root, { "format", "progress", "settings" }, "root");
    const Json& progress = requiredProperty(root, "progress", "root");
    rejectUnknownProperties(
        progress,
        { "unlockedLevel", "currentLevel", "currentScreen", "levels", "activeScreen" },
        "progress");

    const int currentScreen =
        nonNegativeIntegerProperty(progress, "currentScreen", "progress");
    const Json& activeScreen = requiredProperty(progress, "activeScreen", "progress");

    Json format2Root = root;
    format2Root["progress"].erase("currentScreen");
    format2Root["progress"].erase("activeScreen");
    PlayerProfile profile = parseFormat2(format2Root);
    profile.currentScreen = currentScreen;

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
            fail("progress.activeScreen", "checkpoint does not match current level and screen");
        }
        profile.activeScreen = std::move(checkpoint);
    }
    profile.normalize();
    return profile;
}

PlayerProfile parseFormat4(const Json& root)
{
    rejectUnknownProperties(root, { "format", "progress", "settings" }, "root");
    const Json& settings = requiredProperty(root, "settings", "root");
    rejectUnknownProperties(
        settings,
        { "audio", "video", "input", "accessibility" },
        "settings");
    const InputBindings input = inputBindingsFromJson(
        requiredProperty(settings, "input", "settings"),
        "settings.input",
        false);

    Json legacyRoot = root;
    legacyRoot["settings"]["input"] = legacyInputDefaultsJson();
    PlayerProfile profile = parseFormat3(legacyRoot);
    profile.input = input;
    profile.normalize();
    return profile;
}

PlayerProfile parseFormat5(const Json& root)
{
    rejectUnknownProperties(root, { "format", "progress", "settings" }, "root");
    const Json& settings = requiredProperty(root, "settings", "root");
    rejectUnknownProperties(
        settings,
        { "audio", "video", "input", "accessibility" },
        "settings");

    const Json& video = requiredProperty(settings, "video", "settings");
    rejectUnknownProperties(video, {
        "fullscreen", "vsync", "antiAliasingSamples", "ambientOcclusion",
        "windowWidth", "windowHeight",
    }, "settings.video");
    PlayerProfile::VideoSettings videoSettings;
    videoSettings.fullscreen = boolProperty(video, "fullscreen", "settings.video");
    videoSettings.vsync = boolProperty(video, "vsync", "settings.video");
    videoSettings.antiAliasingSamples = nonNegativeIntegerProperty(
        video, "antiAliasingSamples", "settings.video");
    videoSettings.ambientOcclusion = boolProperty(
        video, "ambientOcclusion", "settings.video");
    videoSettings.windowWidth = nonNegativeIntegerProperty(
        video, "windowWidth", "settings.video");
    videoSettings.windowHeight = nonNegativeIntegerProperty(
        video, "windowHeight", "settings.video");
    const InputBindings input = inputBindingsFromJson(
        requiredProperty(settings, "input", "settings"),
        "settings.input");

    Json legacyRoot = root;
    legacyRoot["settings"]["video"].erase("antiAliasingSamples");
    legacyRoot["settings"]["video"].erase("ambientOcclusion");
    legacyRoot["settings"]["video"].erase("windowWidth");
    legacyRoot["settings"]["video"].erase("windowHeight");
    legacyRoot["settings"]["input"].erase("menuConfirm");
    PlayerProfile profile = parseFormat4(legacyRoot);
    profile.video = videoSettings;
    profile.input = input;
    profile.normalize();
    return profile;
}

PlayerProfile parseFormat6(const Json& root)
{
    rejectUnknownProperties(root, { "format", "progress", "settings" }, "root");
    const Json& settings = requiredProperty(root, "settings", "root");
    rejectUnknownProperties(
        settings,
        { "audio", "video", "input", "accessibility" },
        "settings");
    const Json& video = requiredProperty(settings, "video", "settings");
    rejectUnknownProperties(video, {
        "fullscreen", "vsync", "antiAliasingSamples", "renderScalePercent",
        "ambientOcclusion", "windowWidth", "windowHeight",
    }, "settings.video");
    const int renderScalePercent = nonNegativeIntegerProperty(
        video, "renderScalePercent", "settings.video");

    Json legacyRoot = root;
    legacyRoot["settings"]["video"].erase("renderScalePercent");
    PlayerProfile profile = parseFormat5(legacyRoot);
    profile.video.renderScalePercent = renderScalePercent;
    profile.normalize();
    return profile;
}

PlayerProfile parseFormat1(const Json& root)
{
    rejectUnknownProperties(root, {
        "format", "unlockedLevel", "currentLevel", "completedLevels",
        "masterVolume", "musicVolume", "soundVolume",
    }, "root");

    PlayerProfile profile;
    profile.unlockedLevel = nonNegativeIntegerProperty(root, "unlockedLevel", "root");
    profile.currentLevel = nonNegativeIntegerProperty(root, "currentLevel", "root");
    profile.audio.masterVolume = floatProperty(root, "masterVolume", "root");
    profile.audio.musicVolume = floatProperty(root, "musicVolume", "root");
    profile.audio.soundVolume = root.contains("soundVolume")
        ? floatProperty(root, "soundVolume", "root")
        : 1.0f;

    const Json& completed = requiredProperty(root, "completedLevels", "root");
    if (!completed.is_array()) {
        fail("root", "property 'completedLevels' must be an array");
    }
    for (std::size_t i = 0; i < completed.size(); ++i) {
        Json wrapper = { { "level", completed[i] } };
        const int level = nonNegativeIntegerProperty(wrapper, "level", "completedLevels");
        if (std::ranges::any_of(profile.levels, [&](const auto& item) {
                return item.level == level;
            })) {
            fail("completedLevels", "duplicate level " + std::to_string(level));
        }
        profile.levels.push_back({ .level = level, .completed = true });
    }
    profile.normalize();
    return profile;
}

} // namespace

void PlayerProfile::normalize()
{
    unlockedLevel = std::max(unlockedLevel, 0);
    currentLevel = std::clamp(currentLevel, 0, unlockedLevel);
    currentScreen = std::max(currentScreen, 0);
    audio.masterVolume = std::clamp(audio.masterVolume, 0.0f, 1.0f);
    audio.musicVolume = std::clamp(audio.musicVolume, 0.0f, 1.0f);
    audio.soundVolume = std::clamp(audio.soundVolume, 0.0f, 1.0f);
    if (video.antiAliasingSamples != 1 &&
        video.antiAliasingSamples != 2 &&
        video.antiAliasingSamples != 4 &&
        video.antiAliasingSamples != 8) {
        video.antiAliasingSamples = 8;
    }
    video.renderScalePercent = normalizedRenderScalePercent(
        video.renderScalePercent);
    video.windowWidth = std::clamp(video.windowWidth, 640, 7680);
    video.windowHeight = std::clamp(video.windowHeight, 480, 4320);
    std::ranges::sort(levels, {}, &LevelProgress::level);
    if (activeScreen &&
        (activeScreen->level != currentLevel || activeScreen->screen != currentScreen)) {
        activeScreen.reset();
    }
}

void PlayerProfile::setCurrentLevel(int level)
{
    currentLevel = std::clamp(level, 0, std::max(unlockedLevel, 0));
    currentScreen = 0;
    activeScreen.reset();
}

void PlayerProfile::setCurrentScreen(int level, int screen)
{
    const int normalizedLevel = std::clamp(level, 0, std::max(unlockedLevel, 0));
    const int normalizedScreen = std::max(screen, 0);
    if (currentLevel != normalizedLevel || currentScreen != normalizedScreen) {
        activeScreen.reset();
    }
    currentLevel = normalizedLevel;
    currentScreen = normalizedScreen;
}

void PlayerProfile::recordLevelCompletion(
    int level,
    int moves,
    std::optional<double> completionTimeSeconds,
    bool unlockNextLevel)
{
    if (level < 0 || moves < 0 ||
        (completionTimeSeconds &&
            (!std::isfinite(*completionTimeSeconds) || *completionTimeSeconds < 0.0))) {
        throw std::invalid_argument("invalid level completion metrics");
    }

    auto found = std::ranges::find(levels, level, &LevelProgress::level);
    if (found == levels.end()) {
        levels.push_back({ .level = level });
        found = std::prev(levels.end());
    }
    found->completed = true;
    if (!found->bestMoves || moves < *found->bestMoves) {
        found->bestMoves = moves;
    }
    if (completionTimeSeconds &&
        (!found->bestTimeSeconds || *completionTimeSeconds < *found->bestTimeSeconds)) {
        found->bestTimeSeconds = *completionTimeSeconds;
    }
    unlockedLevel = std::max(unlockedLevel, level + (unlockNextLevel ? 1 : 0));
    normalize();
}

const PlayerProfile::LevelProgress* PlayerProfile::progressForLevel(int level) const
{
    const auto found = std::ranges::find(levels, level, &LevelProgress::level);
    return found == levels.end() ? nullptr : &*found;
}

std::string PlayerProfile::serialize() const
{
    PlayerProfile normalized = *this;
    normalized.normalize();
    if (normalized.activeScreen &&
        (normalized.activeScreen->level != normalized.currentLevel ||
            normalized.activeScreen->screen != normalized.currentScreen ||
            normalized.activeScreen->completedLevelMoveCount < 0 ||
            !std::isfinite(normalized.activeScreen->levelElapsedSeconds) ||
            normalized.activeScreen->levelElapsedSeconds < 0.0)) {
        throw std::runtime_error("player profile active screen checkpoint is invalid");
    }

    OrderedJson levelItems = OrderedJson::array();
    for (const LevelProgress& level : normalized.levels) {
        OrderedJson item = {
            { "level", level.level },
            { "completed", level.completed },
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

    OrderedJson root = {
        { "format", currentPlayerProfileFormat },
        { "progress", {
            { "unlockedLevel", normalized.unlockedLevel },
            { "currentLevel", normalized.currentLevel },
            { "currentScreen", normalized.currentScreen },
            { "levels", std::move(levelItems) },
            { "activeScreen", std::move(activeScreenJson) },
        } },
        { "settings", {
            { "audio", {
                { "masterVolume", normalized.audio.masterVolume },
                { "musicVolume", normalized.audio.musicVolume },
                { "soundVolume", normalized.audio.soundVolume },
            } },
            { "video", {
                { "fullscreen", normalized.video.fullscreen },
                { "vsync", normalized.video.vsync },
                { "antiAliasingSamples", normalized.video.antiAliasingSamples },
                { "renderScalePercent", normalized.video.renderScalePercent },
                { "ambientOcclusion", normalized.video.ambientOcclusion },
                { "windowWidth", normalized.video.windowWidth },
                { "windowHeight", normalized.video.windowHeight },
            } },
            { "input", inputBindingsToJson(normalized.input) },
            { "accessibility", {
                { "reducedMotion", normalized.accessibility.reducedMotion },
                { "highContrast", normalized.accessibility.highContrast },
                { "largeText", normalized.accessibility.largeText },
                { "subtitles", normalized.accessibility.subtitles },
                { "screenShake", normalized.accessibility.screenShake },
            } },
        } },
    };
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
    if (format == 1) {
        return { .profile = parseFormat1(root), .sourceFormat = 1 };
    }
    if (format == 2) {
        return { .profile = parseFormat2(root), .sourceFormat = 2 };
    }
    if (format == 3) {
        return { .profile = parseFormat3(root), .sourceFormat = 3 };
    }
    if (format == 4) {
        return { .profile = parseFormat4(root), .sourceFormat = 4 };
    }
    if (format == 5) {
        return { .profile = parseFormat5(root), .sourceFormat = format };
    }
    if (format == currentPlayerProfileFormat) {
        return { .profile = parseFormat6(root), .sourceFormat = format };
    }
    fail("root", "unsupported format " + std::to_string(format));
}

} // namespace sokoban

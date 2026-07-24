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

// Serialization, strict parsing, and migrations for PlayerProfile. The model
// itself lives in PlayerProfile.cpp.
//
// Migration strategy: old formats are upgraded by forward JSON patches
// (migrate1to2 .. migrate8to9) applied in sequence, then a single strict
// format-9 parse validates the fully migrated document. Patches only move
// fields and add defaults; unknown keys survive them and are rejected by the
// final parse, so schema strictness is preserved without every historical
// format keeping its own parser.
//
// Format 9: the top-level "progress" and "settings" sections are each
// optional. Save-slot files carry only progress and the shared settings.json
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
    Json& settings = root["settings"];
    if (!settings.is_object()) {
        return;
    }
    Json& video = settings["video"];
    if (video.is_object()) {
        if (!video.contains("antiAliasingSamples")) {
            video["antiAliasingSamples"] = 8;
        }
        if (!video.contains("ambientOcclusion")) {
            video["ambientOcclusion"] = config::ambientOcclusionEnabled;
        }
        if (!video.contains("windowWidth")) {
            video["windowWidth"] = 1280;
        }
        if (!video.contains("windowHeight")) {
            video["windowHeight"] = 720;
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
        "windowWidth", "windowHeight",
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
    };
    static_assert(std::size(migrations) == currentPlayerProfileFormat - 1);

    Json migrated = root;
    for (int from = format; from < currentPlayerProfileFormat; ++from) {
        migrations[from - 1](migrated);
    }
    return { .profile = parseCurrent(migrated), .sourceFormat = format };
}

} // namespace sokoban

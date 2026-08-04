#include "engine/PlayerProfileMigrations.hpp"

#include "engine/PlayerProfile.hpp"
#include "engine/render/WaterConfig.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdint>
#include <ranges>
#include <string>
#include <utility>
#include <vector>

namespace sokoban {
namespace {

using Json = nlohmann::json;
using OrderedJson = nlohmann::ordered_json;

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
        playerProfileMigrationSupport::nonNegativeIntegerProperty(root, "unlockedLevel", "root");
    progress["currentLevel"] =
        playerProfileMigrationSupport::nonNegativeIntegerProperty(root, "currentLevel", "root");
    Json levels = Json::array();
    const Json& completed = playerProfileMigrationSupport::requiredProperty(root, "completedLevels", "root");
    if (!completed.is_array()) {
        playerProfileMigrationSupport::fail("root", "property 'completedLevels' must be an array");
    }
    for (std::size_t i = 0; i < completed.size(); ++i) {
        Json wrapper = { { "level", completed[i] } };
        const int level =
            playerProfileMigrationSupport::nonNegativeIntegerProperty(wrapper, "level", "completedLevels");
        for (const Json& existing : levels) {
            if (existing.at("level").get<int>() == level) {
                playerProfileMigrationSupport::fail("completedLevels", "duplicate level " + std::to_string(level));
            }
        }
        levels.push_back({ { "level", level }, { "completed", true } });
    }
    progress["levels"] = std::move(levels);

    Json audio = Json::object();
    audio["masterVolume"] = playerProfileMigrationSupport::requiredProperty(root, "masterVolume", "root");
    audio["musicVolume"] = playerProfileMigrationSupport::requiredProperty(root, "musicVolume", "root");
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
        playerProfileMigrationSupport::stringProperty(legacy, "moveUp", "settings.input"));
    setLegacyKeyboardBinding(bindings, InputAction::MoveDown,
        playerProfileMigrationSupport::stringProperty(legacy, "moveDown", "settings.input"));
    setLegacyKeyboardBinding(bindings, InputAction::MoveLeft,
        playerProfileMigrationSupport::stringProperty(legacy, "moveLeft", "settings.input"));
    setLegacyKeyboardBinding(bindings, InputAction::MoveRight,
        playerProfileMigrationSupport::stringProperty(legacy, "moveRight", "settings.input"));
    setLegacyKeyboardBinding(bindings, InputAction::Undo,
        playerProfileMigrationSupport::stringProperty(legacy, "undo", "settings.input"));
    setLegacyKeyboardBinding(bindings, InputAction::Restart,
        playerProfileMigrationSupport::stringProperty(legacy, "restart", "settings.input"));
    settings["input"] = playerProfileMigrationSupport::inputBindingsToJson(bindings);
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
        const OrderedJson defaults = playerProfileMigrationSupport::inputBindingsToJson(defaultInputBindings());
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
    const OrderedJson defaults = playerProfileMigrationSupport::inputBindingsToJson(defaultInputBindings());
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

    const OrderedJson defaults = playerProfileMigrationSupport::inputBindingsToJson(defaultInputBindings());
    if (!input.contains("showTopDownView")) {
        input["showTopDownView"] =
            Json::parse(defaults.at("showTopDownView").dump());
    }

    InputBindings bindings =
        playerProfileMigrationSupport::inputBindingsFromJson(input, "settings.input");
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
    input = playerProfileMigrationSupport::inputBindingsToJson(bindings);
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

    const OrderedJson defaults = playerProfileMigrationSupport::inputBindingsToJson(defaultInputBindings());
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

} // namespace

void migratePlayerProfileToCurrent(Json& root, int sourceFormat)
{
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

    for (int from = sourceFormat; from < currentPlayerProfileFormat; ++from) {
        migrations[from - 1](root);
    }
}

} // namespace sokoban

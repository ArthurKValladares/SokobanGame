#include "engine/PlayerProfile.hpp"

#include "engine/CrystalOriginJson.hpp"
#include "engine/Rules.hpp"
#include "engine/render/RenderResolution.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <new>
#include <stdexcept>
#include <type_traits>
#include <utility>

// Serialization and strict current-format parsing for PlayerProfile. Older
// formats are not migrated (see currentPlayerProfileFormat); the model itself
// lives in PlayerProfile.cpp.
//
// Save-slot files carry only progress and the shared settings.json
// carries only settings (ProfileSections selects the shape at serialize
// time); pre-split combined files simply contain both.

namespace sokoban {
namespace {

using ProfileJson = nlohmann::json;
using OrderedJson = nlohmann::ordered_json;

// Each undo action creates many state and presentation nodes. Reserve their
// known sizes and fill objects directly to avoid allocating temporary pairs.
OrderedJson objectWithCapacity(std::size_t capacity)
{
    OrderedJson result = OrderedJson::object();
    result.get_ref<OrderedJson::object_t&>().reserve(capacity);
    return result;
}

OrderedJson arrayWithCapacity(std::size_t capacity)
{
    OrderedJson result = OrderedJson::array();
    result.get_ref<OrderedJson::array_t&>().reserve(capacity);
    return result;
}

[[noreturn]] void fail(std::string_view context, const std::string& message)
{
    throw std::runtime_error(
        "player profile " + std::string(context) + ": " + message);
}

template <typename Json>
void requireObject(const Json& value, std::string_view context)
{
    if (!value.is_object()) {
        fail(context, "expected an object");
    }
}

template <typename Json>
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

template <typename Json>
const Json& requiredProperty(
    const Json& object,
    std::string_view key,
    std::string_view context)
{
    const auto found = object.find(key);
    if (found == object.end()) {
        fail(context, "missing required property '" + std::string(key) + "'");
    }
    return *found;
}

template <typename Json>
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
        const uint64_t number = value.template get<uint64_t>();
        if (number > static_cast<uint64_t>(std::numeric_limits<int>::max())) {
            fail(context, "property '" + std::string(key) + "' is out of range");
        }
        return static_cast<int>(number);
    }
    const int64_t number = value.template get<int64_t>();
    if (number < std::numeric_limits<int>::min() ||
        number > std::numeric_limits<int>::max()) {
        fail(context, "property '" + std::string(key) + "' is out of range");
    }
    return static_cast<int>(number);
}

template <typename Json>
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

template <typename Json>
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
        return value.template get<uint64_t>();
    }
    const int64_t number = value.template get<int64_t>();
    if (number < 0) {
        fail(context, "property '" + std::string(key) + "' must not be negative");
    }
    return static_cast<uint64_t>(number);
}

// Rotator quarter turns were added without a format change: absent means the
// unit was never turned, and the value is written only when non-zero, so
// documents for screens without rotators are byte-for-byte unchanged.
template <typename Json>
uint8_t quarterTurnsProperty(const Json& object, std::string_view context)
{
    if (!object.contains("quarterTurns")) {
        return 0;
    }
    const uint64_t value =
        unsignedIntegerProperty(object, "quarterTurns", context);
    if (value > 3) {
        fail(context, "property 'quarterTurns' must be between 0 and 3");
    }
    return static_cast<uint8_t>(value);
}

template <typename Json>
std::optional<int> optionalNonNegativeInteger(
    const Json& object,
    std::string_view key,
    std::string_view context)
{
    if (!object.contains(key)) {
        return std::nullopt;
    }
    return nonNegativeIntegerProperty(object, key, context);
}

template <typename Json>
bool boolProperty(
    const Json& object,
    std::string_view key,
    std::string_view context)
{
    const Json& value = requiredProperty(object, key, context);
    if (!value.is_boolean()) {
        fail(context, "property '" + std::string(key) + "' must be a boolean");
    }
    return value.template get<bool>();
}

template <typename Json>
float floatProperty(
    const Json& object,
    std::string_view key,
    std::string_view context)
{
    const Json& value = requiredProperty(object, key, context);
    if (!value.is_number()) {
        fail(context, "property '" + std::string(key) + "' must be a number");
    }
    const float result = value.template get<float>();
    if (!std::isfinite(result)) {
        fail(context, "property '" + std::string(key) + "' must be finite");
    }
    return result;
}

template <typename Json>
std::optional<double> optionalNonNegativeDouble(
    const Json& object,
    std::string_view key,
    std::string_view context)
{
    const auto found = object.find(key);
    if (found == object.end()) {
        return std::nullopt;
    }
    if (!found->is_number()) {
        fail(context, "property '" + std::string(key) + "' must be a number");
    }
    const double result = found->template get<double>();
    if (!std::isfinite(result) || result < 0.0) {
        fail(context, "property '" + std::string(key) + "' must be finite and non-negative");
    }
    return result;
}

template <typename Json>
std::string stringProperty(
    const Json& object,
    std::string_view key,
    std::string_view context)
{
    const Json& value = requiredProperty(object, key, context);
    if (!value.is_string()) {
        fail(context, "property '" + std::string(key) + "' must be a string");
    }
    std::string result = value.template get<std::string>();
    if (result.empty()) {
        fail(context, "property '" + std::string(key) + "' must not be empty");
    }
    return result;
}

template <typename Json>
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
    const std::string direction = value.template get<std::string>();
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

template <typename Json>
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
    OrderedJson result = objectWithCapacity(3);
    result["x"] = position.x;
    result["y"] = position.y;
    result["z"] = position.z;
    return result;
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

template <typename Json>
GameState gameStateFromJson(const Json& value, std::string_view context)
{
    rejectUnknownProperties(
        value,
        { "players", "movables", "enemies", "turnedMirrors", "elevators", "minecarts", "wardrobes", "activeButtons", "activeLevers" },
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
            { "id", "cell", "character", "controller", "dead", "drowned", "sliding", "quarterTurns", "abilityGrants", "facing" },
            playerContext);
        std::optional<CharacterType> character;
        const Json& encodedCharacter =
            requiredProperty(item, "character", playerContext);
        if (!encodedCharacter.is_null()) {
            if (!encodedCharacter.is_string()) {
                fail(playerContext,
                    "property 'character' must be a string or null");
            }
            character = characterTypeFromName(
                encodedCharacter.template get<std::string>());
            if (!character) {
                fail(playerContext, "unknown hero character");
            }
        }
        const uint64_t abilityGrants = item.contains("abilityGrants")
            ? unsignedIntegerProperty(item, "abilityGrants", playerContext) : 0;
        if (abilityGrants > CharacterAbilityGrants::maxPacked) {
            fail(playerContext, "property 'abilityGrants' has unknown ability bits");
        }
        state.players.push_back({
            .id = unsignedIntegerProperty(item, "id", playerContext),
            .cell = positionFromJson(
                requiredProperty(item, "cell", playerContext),
                playerContext + ".cell"),
            .character = character,
            .controller = unsignedIntegerProperty(
                item, "controller", playerContext),
            .dead = boolProperty(item, "dead", playerContext),
            .drowned = boolProperty(item, "drowned", playerContext),
            .sliding = directionFromJson(
                requiredProperty(item, "sliding", playerContext),
                playerContext + ".sliding"),
            .quarterTurns = quarterTurnsProperty(item, playerContext),
            .abilityGrants = CharacterAbilityGrants::unpack(static_cast<uint32_t>(abilityGrants)),
            .facing = item.contains("facing")
                ? directionFromJson(item.at("facing"), playerContext + ".facing") : std::nullopt,
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
        rejectUnknownProperties(
            item,
            { "id", "type", "cell", "fallen", "dead", "sliding", "quarterTurns", "crystalOrigin" },
            movableContext);
        GameState::Movable movable;
        movable.id = unsignedIntegerProperty(item, "id", movableContext);
        movable.type = tileTypeFromName(
            stringProperty(item, "type", movableContext),
            movableContext + ".type");
        movable.cell = positionFromJson(
            requiredProperty(item, "cell", movableContext),
            movableContext + ".cell");
        movable.fallen = boolProperty(item, "fallen", movableContext);
        // `dead` was added when turrets became destructible. Older profiles
        // predate that state and therefore load every movable alive.
        movable.dead = item.contains("dead")
            ? boolProperty(item, "dead", movableContext)
            : false;
        movable.sliding = directionFromJson(
            requiredProperty(item, "sliding", movableContext),
            movableContext + ".sliding");
        movable.quarterTurns = quarterTurnsProperty(item, movableContext);
        if (item.contains("crystalOrigin")) {
            movable.crystalOrigin = crystalOriginFromJson(item.at("crystalOrigin"), movable.type);
        }
        state.movables.push_back(movable);
    }
    const Json& enemies = requiredProperty(value, "enemies", context);
    if (!enemies.is_array()) {
        fail(context, "property 'enemies' must be an array");
    }
    for (std::size_t i = 0; i < enemies.size(); ++i) {
        const std::string enemyContext =
            std::string(context) + ".enemies[" + std::to_string(i) + "]";
        const Json& item = enemies[i];
        rejectUnknownProperties(
            item,
            { "id", "cell", "fallen", "dead", "sliding", "quarterTurns" },
            enemyContext);
        GameState::Enemy enemy {
            .id = unsignedIntegerProperty(item, "id", enemyContext),
            .cell = positionFromJson(
                requiredProperty(item, "cell", enemyContext),
                enemyContext + ".cell"),
            .fallen = boolProperty(item, "fallen", enemyContext),
            .dead = boolProperty(item, "dead", enemyContext),
        };
        if (item.contains("sliding")) {
            enemy.sliding = directionFromJson(
                item["sliding"], enemyContext + ".sliding");
        }
        enemy.quarterTurns = quarterTurnsProperty(item, enemyContext);
        state.enemies.push_back(enemy);
    }
    // Written only when a rotator has turned a mirror.
    if (value.contains("turnedMirrors")) {
        const Json& mirrors = value["turnedMirrors"];
        if (!mirrors.is_array()) {
            fail(context, "property 'turnedMirrors' must be an array");
        }
        for (std::size_t i = 0; i < mirrors.size(); ++i) {
            const std::string mirrorContext = std::string(context) +
                ".turnedMirrors[" + std::to_string(i) + "]";
            const Json& item = mirrors[i];
            rejectUnknownProperties(
                item, { "cell", "quarterTurns" }, mirrorContext);
            const GridPosition3 cell = positionFromJson(
                requiredProperty(item, "cell", mirrorContext),
                mirrorContext + ".cell");
            const uint8_t quarterTurns =
                quarterTurnsProperty(item, mirrorContext);
            if (quarterTurns == 0 ||
                rules::mirrorQuarterTurnsAt(state, cell) != 0) {
                fail(mirrorContext,
                    "turned mirrors need distinct cells and 1-3 quarter turns");
            }
            rules::setMirrorQuarterTurns(state, cell, quarterTurns);
        }
    }
    // Written only for screens with moving platforms: one entry per authored
    // platform, in level order. Restore validates the values by replay.
    if (value.contains("elevators")) {
        const Json& elevators = value["elevators"];
        if (!elevators.is_array()) {
            fail(context, "property 'elevators' must be an array");
        }
        for (std::size_t i = 0; i < elevators.size(); ++i) {
            const std::string elevatorContext = std::string(context) +
                ".elevators[" + std::to_string(i) + "]";
            const Json& item = elevators[i];
            rejectUnknownProperties(item, { "cell", "phase" }, elevatorContext);
            const uint64_t phase =
                unsignedIntegerProperty(item, "phase", elevatorContext);
            if (phase > 0xffU) {
                fail(elevatorContext, "property 'phase' must be below 256");
            }
            state.elevators.push_back({
                .cell = positionFromJson(
                    requiredProperty(item, "cell", elevatorContext),
                    elevatorContext + ".cell"),
                .phase = static_cast<uint8_t>(phase),
            });
        }
    }
    if (value.contains("minecarts")) {
        const Json& minecarts = value["minecarts"];
        if (!minecarts.is_array()) {
            fail(context, "property 'minecarts' must be an array");
        }
        for (std::size_t i = 0; i < minecarts.size(); ++i) {
            const std::string minecartContext = std::string(context) +
                ".minecarts[" + std::to_string(i) + "]";
            const Json& item = minecarts[i];
            rejectUnknownProperties(item, { "cell", "phase" }, minecartContext);
            const uint64_t phase =
                unsignedIntegerProperty(item, "phase", minecartContext);
            if (phase > 0xffffU) {
                fail(minecartContext, "property 'phase' must be below 65536");
            }
            state.minecarts.push_back({
                .cell = positionFromJson(
                    requiredProperty(item, "cell", minecartContext),
                    minecartContext + ".cell"),
                .phase = static_cast<uint16_t>(phase),
            });
        }
    }

    if (value.contains("wardrobes")) {
        const Json& wardrobes = value["wardrobes"];
        if (!wardrobes.is_array()) {
            fail(context, "property 'wardrobes' must be an array");
        }
        for (std::size_t i = 0; i < wardrobes.size(); ++i) {
            const std::string wardrobeContext = std::string(context) +
                ".wardrobes[" + std::to_string(i) + "]";
            const Json& item = wardrobes[i];
            rejectUnknownProperties(
                item, { "cell", "character" }, wardrobeContext);
            const std::optional<CharacterType> character =
                characterTypeFromName(stringProperty(
                    item, "character", wardrobeContext));
            if (!character) {
                fail(wardrobeContext, "unknown wardrobe character");
            }
            state.wardrobes.push_back({
                .cell = positionFromJson(
                    requiredProperty(item, "cell", wardrobeContext),
                    wardrobeContext + ".cell"),
                .character = *character,
            });
        }
    }
    if (value.contains("activeButtons")) {
        const Json& buttons = value["activeButtons"];
        if (!buttons.is_array()) {
            fail(context, "property 'activeButtons' must be an array");
        }
        for (const Json& button : buttons) {
            const GridPosition3 cell = positionFromJson(button, context);
            if (std::ranges::find(state.activeButtons, cell) != state.activeButtons.end()) {
                fail(context, "duplicate active button");
            }
            state.activeButtons.push_back(cell);
        }
    }
    if (value.contains("activeLevers")) {
        const Json& levers = value["activeLevers"];
        if (!levers.is_array()) {
            fail(context, "property 'activeLevers' must be an array");
        }
        for (const Json& lever : levers) {
            const GridPosition3 cell = positionFromJson(lever, context);
            if (std::ranges::find(state.activeLevers, cell) != state.activeLevers.end()) {
                fail(context, "duplicate active lever");
            }
            state.activeLevers.push_back(cell);
        }
    }
    return state;
}

OrderedJson gameStateToJson(const GameState& state)
{
    OrderedJson result = objectWithCapacity(9);
    OrderedJson& players = result["players"] = arrayWithCapacity(state.players.size());
    for (const GameState::Player& player : state.players) {
        OrderedJson item = objectWithCapacity(10);
        item["id"] = player.id;
        item["cell"] = positionToJson(player.cell);
        item["character"] = player.character
            ? OrderedJson(characterTypeName(*player.character))
            : OrderedJson(nullptr);
        item["controller"] = player.controller;
        item["dead"] = player.dead;
        item["drowned"] = player.drowned;
        item["sliding"] = player.sliding
            ? OrderedJson(directionName(*player.sliding))
            : OrderedJson(nullptr);
        if (player.quarterTurns != 0) {
            item["quarterTurns"] = player.quarterTurns;
        }
        if (const auto grants = player.abilityGrants.packed(); grants != 0) {
            item["abilityGrants"] = grants;
        }
        if (player.facing) {
            item["facing"] = directionName(*player.facing);
        }
        players.push_back(std::move(item));
    }
    OrderedJson& movables = result["movables"] =
        arrayWithCapacity(state.movables.size());
    for (const GameState::Movable& movable : state.movables) {
        OrderedJson item = objectWithCapacity(8);
        item["id"] = movable.id;
        item["type"] = tileTypeName(movable.type);
        item["cell"] = positionToJson(movable.cell);
        item["fallen"] = movable.fallen;
        item["dead"] = movable.dead;
        item["sliding"] = movable.sliding
            ? OrderedJson(directionName(*movable.sliding))
            : OrderedJson(nullptr);
        if (movable.quarterTurns != 0) {
            item["quarterTurns"] = movable.quarterTurns;
        }
        if (movable.crystalOrigin) {
            item["crystalOrigin"] = crystalOriginToJson<OrderedJson>(*movable.crystalOrigin);
        }
        movables.push_back(std::move(item));
    }
    OrderedJson& enemies = result["enemies"] = arrayWithCapacity(state.enemies.size());
    for (const GameState::Enemy& enemy : state.enemies) {
        OrderedJson item = objectWithCapacity(6);
        item["id"] = enemy.id;
        item["cell"] = positionToJson(enemy.cell);
        item["fallen"] = enemy.fallen;
        item["dead"] = enemy.dead;
        item["sliding"] = enemy.sliding
            ? OrderedJson(directionName(*enemy.sliding))
            : OrderedJson(nullptr);
        if (enemy.quarterTurns != 0) {
            item["quarterTurns"] = enemy.quarterTurns;
        }
        enemies.push_back(std::move(item));
    }
    if (!state.turnedMirrors.empty()) {
        OrderedJson& mirrors = result["turnedMirrors"] =
            arrayWithCapacity(state.turnedMirrors.size());
        for (const GameState::TurnedMirror& mirror : state.turnedMirrors) {
            OrderedJson item = objectWithCapacity(2);
            item["cell"] = positionToJson(mirror.cell);
            item["quarterTurns"] = mirror.quarterTurns;
            mirrors.push_back(std::move(item));
        }
    }
    if (!state.elevators.empty()) {
        OrderedJson& elevators = result["elevators"] =
            arrayWithCapacity(state.elevators.size());
        for (const GameState::Elevator& elevator : state.elevators) {
            OrderedJson item = objectWithCapacity(2);
            item["cell"] = positionToJson(elevator.cell);
            item["phase"] = elevator.phase;
            elevators.push_back(std::move(item));
        }
    }
    if (!state.minecarts.empty()) {
        OrderedJson& minecarts = result["minecarts"] =
            arrayWithCapacity(state.minecarts.size());
        for (const GameState::Minecart& minecart : state.minecarts) {
            OrderedJson item = objectWithCapacity(2);
            item["cell"] = positionToJson(minecart.cell);
            item["phase"] = minecart.phase;
            minecarts.push_back(std::move(item));
        }
    }
    if (!state.wardrobes.empty()) {
        OrderedJson& wardrobes = result["wardrobes"] =
            arrayWithCapacity(state.wardrobes.size());
        for (const GameState::Wardrobe& wardrobe : state.wardrobes) {
            OrderedJson item = objectWithCapacity(2);
            item["cell"] = positionToJson(wardrobe.cell);
            item["character"] = characterTypeName(wardrobe.character);
            wardrobes.push_back(std::move(item));
        }
    }
    if (!state.activeButtons.empty()) {
        OrderedJson& buttons = result["activeButtons"] =
            arrayWithCapacity(state.activeButtons.size());
        for (const GridPosition3 cell : state.activeButtons) {
            buttons.push_back(positionToJson(cell));
        }
    }
    if (!state.activeLevers.empty()) {
        OrderedJson& levers = result["activeLevers"] =
            arrayWithCapacity(state.activeLevers.size());
        for (const GridPosition3 cell : state.activeLevers) {
            levers.push_back(positionToJson(cell));
        }
    }
    return result;
}

template <typename Json>
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
    OrderedJson result = objectWithCapacity(3);
    result["x"] = value.x;
    result["y"] = value.y;
    result["z"] = value.z;
    return result;
}

template <typename Json>
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
    if (kind == "elevator") {
        return EntityKind::Elevator;
    }
    if (kind == "minecart") {
        return EntityKind::Minecart;
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
    case EntityKind::Elevator:
        return "elevator";
    case EntityKind::Minecart:
        return "minecart";
    }
    return "player";
}

template <typename Json>
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
    OrderedJson result = objectWithCapacity(2);
    result["kind"] = entityKindName(target.kind);
    result["id"] = target.id;
    return result;
}

template <typename Json>
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

template <typename Json>
GameplaySession::Action undoActionFromJson(
    const Json& value,
    std::string_view context,
    GameState before)
{
    rejectUnknownProperties(value, {
        "after", "playerPushing", "playerPulling", "moveCountBefore",
        "moveCountAfter", "presentation",
    }, context);
    GameplaySession::Action action;
    action.before = std::move(before);
    action.after = gameStateFromJson(
        requiredProperty(value, "after", context),
        std::string(context) + ".after");
    action.playerPushing = boolProperty(value, "playerPushing", context);
    action.playerPulling = boolProperty(value, "playerPulling", context);
    action.playerMoveCountBefore =
        nonNegativeIntegerProperty(value, "moveCountBefore", context);
    action.playerMoveCountAfter =
        nonNegativeIntegerProperty(value, "moveCountAfter", context);

    const Json& presentation = requiredProperty(value, "presentation", context);
    const std::string presentationContext =
        std::string(context) + ".presentation";
    rejectUnknownProperties(presentation, {
        "durationSeconds", "motions", "animations", "controls",
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
            "target", "from", "to", "startSeconds", "durationSeconds", "arcHeight",
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
        if (encoded.contains("arcHeight")) {
            motion.arcHeight = floatProperty(encoded, "arcHeight", motionContext);
            if (motion.arcHeight < 0.0f) {
                fail(motionContext, "motion arc height must be non-negative");
            }
        }
        if (motion.startSeconds < 0.0f || motion.durationSeconds < 0.0f ||
            motion.startSeconds + motion.durationSeconds >
                action.presentation.durationSeconds + 0.0001f) {
            fail(motionContext, "motion track is outside the timeline");
        }
        action.presentation.motions.push_back(motion);
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
            track.segments.push_back(segment);
        }
        action.presentation.animations.push_back(std::move(track));
    }
    // Older checkpoints have no control tracks. Their exact settled states
    // still restore, and their recorded actor timelines remain usable.
    if (presentation.contains("controls")) {
        const Json& controls = presentation["controls"];
        if (!controls.is_array()) {
            fail(presentationContext, "property 'controls' must be an array");
        }
        for (std::size_t i = 0; i < controls.size(); ++i) {
            const Json& encoded = controls[i];
            const std::string controlContext = presentationContext +
                ".controls[" + std::to_string(i) + "]";
            rejectUnknownProperties(encoded, {
                "cell", "from", "to", "startSeconds", "durationSeconds",
            }, controlContext);
            ActionControlTrack control {
                .cell = positionFromJson(
                    requiredProperty(encoded, "cell", controlContext), controlContext + ".cell"),
                .from = floatProperty(encoded, "from", controlContext),
                .to = floatProperty(encoded, "to", controlContext),
                .startSeconds = floatProperty(encoded, "startSeconds", controlContext),
                .durationSeconds = floatProperty(encoded, "durationSeconds", controlContext),
            };
            if (control.from < 0.0f || control.from > 1.0f ||
                control.to < 0.0f || control.to > 1.0f) {
                fail(controlContext, "control activation must be between zero and one");
            }
            if (control.startSeconds < 0.0f || control.durationSeconds < 0.0f ||
                control.startSeconds + control.durationSeconds >
                    action.presentation.durationSeconds + 0.0001f) {
                fail(controlContext, "control track is outside the timeline");
            }
            action.presentation.controls.push_back(control);
        }
    }
    return action;
}

OrderedJson undoActionToJson(const GameplaySession::Action& action)
{
    OrderedJson result = objectWithCapacity(6);
    result["after"] = gameStateToJson(action.after);
    result["playerPushing"] = action.playerPushing;
    result["playerPulling"] = action.playerPulling;
    result["moveCountBefore"] = action.playerMoveCountBefore;
    result["moveCountAfter"] = action.playerMoveCountAfter;
    OrderedJson& presentation = result["presentation"] = objectWithCapacity(4);
    presentation["durationSeconds"] = action.presentation.durationSeconds;

    OrderedJson& motions = presentation["motions"] =
        arrayWithCapacity(action.presentation.motions.size());
    for (const ActionMotionTrack& motion : action.presentation.motions) {
        OrderedJson item = objectWithCapacity(6);
        item["target"] = entityTargetToJson(motion.target);
        item["from"] = vec3ToJson(motion.from);
        item["to"] = vec3ToJson(motion.to);
        item["startSeconds"] = motion.startSeconds;
        item["durationSeconds"] = motion.durationSeconds;
        if (motion.arcHeight != 0.0f) {
            item["arcHeight"] = motion.arcHeight;
        }
        motions.push_back(std::move(item));
    }
    OrderedJson& animations = presentation["animations"] =
        arrayWithCapacity(action.presentation.animations.size());
    for (const ActionAnimationTrack& track : action.presentation.animations) {
        OrderedJson item = objectWithCapacity(4);
        item["target"] = entityTargetToJson(track.target);
        item["initialUse"] = animationUseId(track.initialUse);
        item["initialClipTimeSeconds"] = track.initialClipTimeSeconds;
        OrderedJson& segments = item["segments"] =
            arrayWithCapacity(track.segments.size());
        for (const ActionAnimationSegment& segment : track.segments) {
            OrderedJson entry = objectWithCapacity(7);
            entry["use"] = animationUseId(segment.use);
            entry["completionUse"] = animationUseId(segment.completionUse);
            entry["fallbackUse"] = segment.fallbackUse
                ? OrderedJson(animationUseId(*segment.fallbackUse))
                : OrderedJson(nullptr);
            entry["startSeconds"] = segment.startSeconds;
            entry["durationSeconds"] = segment.durationSeconds;
            entry["clipStartSeconds"] = segment.clipStartSeconds;
            entry["loops"] = segment.loops;
            segments.push_back(std::move(entry));
        }
        animations.push_back(std::move(item));
    }
    OrderedJson& controls = presentation["controls"] =
        arrayWithCapacity(action.presentation.controls.size());
    for (const ActionControlTrack& control : action.presentation.controls) {
        OrderedJson item = objectWithCapacity(5);
        item["cell"] = positionToJson(control.cell);
        item["from"] = control.from;
        item["to"] = control.to;
        item["startSeconds"] = control.startSeconds;
        item["durationSeconds"] = control.durationSeconds;
        controls.push_back(std::move(item));
    }
    return result;
}

template <bool RetainHistory, typename Json>
GameplaySession::Snapshot sessionSnapshotFromJson(
    const Json& value,
    std::string_view context)
{
    rejectUnknownProperties(
        value,
        { "state", "undoBaseState", "undoStack", "playerMoveCount",
          "automaticMotionPaused", "activeHeroController" },
        context);
    GameplaySession::Snapshot snapshot;
    snapshot.state = gameStateFromJson(
        requiredProperty(value, "state", context),
        std::string(context) + ".state");
    snapshot.playerMoveCount =
        nonNegativeIntegerProperty(value, "playerMoveCount", context);
    snapshot.automaticMotionPaused =
        boolProperty(value, "automaticMotionPaused", context);
    snapshot.activeHeroController = unsignedIntegerProperty(
        value, "activeHeroController", context);
    const Json& undoStack = requiredProperty(value, "undoStack", context);
    if (!undoStack.is_array()) {
        fail(context, "property 'undoStack' must be an array");
    }
    const Json& undoBaseState =
        requiredProperty(value, "undoBaseState", context);
    if (undoStack.empty() != undoBaseState.is_null()) {
        fail(context,
            "property 'undoBaseState' must be null exactly when the undo stack is empty");
    }
    GameState before;
    if (!undoBaseState.is_null()) {
        before = gameStateFromJson(
            undoBaseState, std::string(context) + ".undoBaseState");
    }
    if constexpr (RetainHistory) {
        snapshot.undoStack.reserve(undoStack.size());
    }
    for (std::size_t i = 0; i < undoStack.size(); ++i) {
        const std::string actionContext =
            std::string(context) + ".undoStack[" + std::to_string(i) + "]";
        if constexpr (RetainHistory) {
            GameplaySession::Action action = undoActionFromJson(
                undoStack[i], actionContext, std::move(before));
            before = action.after;
            snapshot.undoStack.push_back(std::move(action));
        } else {
            // Validated and discarded. Only a retained action keeps the state
            // it started from, so this pass gives it none, rather than one
            // the previous iteration already moved out of.
            (void)undoActionFromJson(undoStack[i], actionContext, GameState {});
        }
    }
    return snapshot;
}

OrderedJson sessionSnapshotToJson(const GameplaySession::Snapshot& snapshot)
{
    OrderedJson result = objectWithCapacity(6);
    result["state"] = gameStateToJson(snapshot.state);
    result["undoBaseState"] = snapshot.undoStack.empty()
        ? OrderedJson(nullptr)
        : gameStateToJson(snapshot.undoStack.front().before);
    OrderedJson& undoStack = result["undoStack"] =
        arrayWithCapacity(snapshot.undoStack.size());
    for (const GameplaySession::Action& action : snapshot.undoStack) {
        undoStack.push_back(undoActionToJson(action));
    }
    result["playerMoveCount"] = snapshot.playerMoveCount;
    result["automaticMotionPaused"] = snapshot.automaticMotionPaused;
    result["activeHeroController"] = snapshot.activeHeroController;
    return result;
}

template <typename Json>
InputBinding inputBindingFromJson(const Json& value, std::string_view context)
{
    requireObject(value, context);
    const std::string type = stringProperty(value, "type", context);
    if (type == "keyboard") {
        rejectUnknownProperties(
            value, { "type", "control", "modifiers" }, context);
        KeyboardBinding binding { stringProperty(value, "control", context) };
        if (binding.scancode.empty()) {
            fail(context, "keyboard control must not be empty");
        }
        if (value.contains("modifiers")) {
            const Json& modifiers = value["modifiers"];
            if (!modifiers.is_array() || modifiers.empty()) {
                fail(context, "'modifiers' must be a non-empty array");
            }
            for (const Json& modifier : modifiers) {
                if (!modifier.is_string()) {
                    fail(context, "modifiers must be strings");
                }
                const std::string name = modifier.template get<std::string>();
                std::uint8_t bit = keyModifierNone;
                for (const KeyModifier candidate :
                     { keyModifierCtrl, keyModifierShift, keyModifierAlt }) {
                    if (keyModifierName(candidate) == name) {
                        bit = candidate;
                    }
                }
                if (bit == keyModifierNone) {
                    fail(context, "unknown key modifier '" + name + "'");
                }
                if ((binding.modifiers & bit) != 0U) {
                    fail(context, "duplicate key modifier '" + name + "'");
                }
                binding.modifiers = static_cast<std::uint8_t>(
                    binding.modifiers | bit);
            }
        }
        return binding;
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
            if (value.scancode.empty() ||
                (value.modifiers & ~keyModifierAll) != 0U) {
                throw std::runtime_error(
                    "player profile keyboard binding is invalid");
            }
            OrderedJson result {
                { "type", "keyboard" },
                { "control", value.scancode },
            };
            if (value.modifiers != keyModifierNone) {
                OrderedJson modifiers = OrderedJson::array();
                for (const KeyModifier modifier :
                     { keyModifierCtrl, keyModifierShift, keyModifierAlt }) {
                    if ((value.modifiers & modifier) != 0U) {
                        modifiers.push_back(keyModifierName(modifier));
                    }
                }
                result["modifiers"] = std::move(modifiers);
            }
            return result;
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

template <typename Json>
InputBindings inputBindingsFromJson(
    const Json& value,
    std::string_view context)
{
    requireObject(value, context);
    for (const auto& [key, bindings] : value.items()) {
        (void)bindings;
        try {
            (void)inputActionFromName(key);
        } catch (const std::invalid_argument&) {
            fail(context, "unknown property '" + key + "'");
        }
    }
    InputBindings result;
    for (std::size_t i = 0; i < inputActionCount; ++i) {
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
    if (!restoreMenuBindings(result)) {
        fail(context, "no unused control available for menu navigation");
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

// ---- Strict current-format parse -------------------------------------------

template <bool RetainHistory, typename Json>
void parseProgressSection(PlayerProfile& profile, const Json& progress)
{
    rejectUnknownProperties(
        progress,
        { "unlockedLevel", "currentLevel", "currentScreen", "levels",
          "screens", "activeScreen", "overworldCheckpoint",
          "overworldDiscovery", "worldContext" },
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
        profile.levels.push_back(level);
    }

    const Json& screens = requiredProperty(progress, "screens", "progress");
    if (!screens.is_array()) {
        fail("progress", "property 'screens' must be an array");
    }
    for (std::size_t i = 0; i < screens.size(); ++i) {
        const std::string context =
            "progress.screens[" + std::to_string(i) + "]";
        const Json& item = screens[i];
        rejectUnknownProperties(
            item,
            { "level", "screen", "completed", "bestMoves",
              "bestTimeSeconds" },
            context);
        PlayerProfile::ScreenProgress screen;
        screen.level = nonNegativeIntegerProperty(item, "level", context);
        screen.screen = nonNegativeIntegerProperty(item, "screen", context);
        screen.completed = boolProperty(item, "completed", context);
        screen.bestMoves = optionalNonNegativeInteger(
            item, "bestMoves", context);
        screen.bestTimeSeconds = optionalNonNegativeDouble(
            item, "bestTimeSeconds", context);
        if (!screen.completed &&
            (screen.bestMoves || screen.bestTimeSeconds)) {
            fail(context, "incomplete screens cannot have completion bests");
        }
        if (std::ranges::any_of(
                profile.screens,
                [&](const PlayerProfile::ScreenProgress& existing) {
                    return existing.level == screen.level &&
                        existing.screen == screen.screen;
                })) {
            fail(context,
                "duplicate screen " + std::to_string(screen.level) + ":" +
                std::to_string(screen.screen));
        }
        profile.screens.push_back(screen);
    }

    const Json& overworldDiscovery = requiredProperty(
        progress, "overworldDiscovery", "progress");
    rejectUnknownProperties(
        overworldDiscovery,
        { "topologyFingerprint", "screens" },
        "progress.overworldDiscovery");
    profile.overworldDiscovery.topologyFingerprint = unsignedIntegerProperty(
        overworldDiscovery,
        "topologyFingerprint",
        "progress.overworldDiscovery");
    const Json& discoveredScreens = requiredProperty(
        overworldDiscovery, "screens", "progress.overworldDiscovery");
    if (!discoveredScreens.is_array()) {
        fail("progress.overworldDiscovery", "property 'screens' must be an array");
    }
    for (std::size_t i = 0; i < discoveredScreens.size(); ++i) {
        const std::string context = "progress.overworldDiscovery.screens[" +
            std::to_string(i) + "]";
        Json wrapper = { { "screen", discoveredScreens[i] } };
        const uint64_t screen = unsignedIntegerProperty(
            wrapper, "screen", context);
        if (screen == 0 || screen > std::numeric_limits<uint32_t>::max()) {
            fail(context, "must be a positive 32-bit integer");
        }
        const uint32_t id = static_cast<uint32_t>(screen);
        if (std::ranges::find(
                profile.overworldDiscovery.screens, id) !=
            profile.overworldDiscovery.screens.end()) {
            fail(context, "duplicate screen " + std::to_string(id));
        }
        profile.overworldDiscovery.screens.push_back(id);
    }

    const std::string worldContext =
        stringProperty(progress, "worldContext", "progress");
    if (worldContext == "overworld") {
        profile.worldContext = PlayerProfile::WorldContext::Overworld;
    } else if (worldContext == "puzzle") {
        profile.worldContext = PlayerProfile::WorldContext::Puzzle;
    } else {
        fail("progress", "property 'worldContext' must be 'overworld' or 'puzzle'");
    }

    const Json& overworldCheckpoint =
        requiredProperty(progress, "overworldCheckpoint", "progress");
    if (!overworldCheckpoint.is_null()) {
        rejectUnknownProperties(
            overworldCheckpoint,
            { "topologyFingerprint", "activeScreen", "session" },
            "progress.overworldCheckpoint");
        PlayerProfile::OverworldCheckpoint checkpoint;
        checkpoint.topologyFingerprint = unsignedIntegerProperty(
            overworldCheckpoint,
            "topologyFingerprint",
            "progress.overworldCheckpoint");
        const uint64_t activeScreen = unsignedIntegerProperty(
            overworldCheckpoint,
            "activeScreen",
            "progress.overworldCheckpoint");
        if (activeScreen == 0 ||
            activeScreen > std::numeric_limits<uint32_t>::max()) {
            fail(
                "progress.overworldCheckpoint",
                "property 'activeScreen' must be a positive 32-bit integer");
        }
        checkpoint.activeScreen = static_cast<uint32_t>(activeScreen);
        checkpoint.session = sessionSnapshotFromJson<RetainHistory>(
            requiredProperty(
                overworldCheckpoint,
                "session",
                "progress.overworldCheckpoint"),
            "progress.overworldCheckpoint.session");
        profile.overworldCheckpoint = std::move(checkpoint);
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
        checkpoint.session = sessionSnapshotFromJson<RetainHistory>(
            requiredProperty(activeScreen, "session", "progress.activeScreen"),
            "progress.activeScreen.session");
        if (checkpoint.level != profile.currentLevel ||
            checkpoint.screen != profile.currentScreen) {
            fail("progress.activeScreen",
                "checkpoint does not match current level and screen");
        }
        profile.activeScreen = std::move(checkpoint);
    }
    if (profile.worldContext == PlayerProfile::WorldContext::Puzzle &&
        !profile.activeScreen) {
        fail("progress", "puzzle world context requires an active screen checkpoint");
    }
    if (profile.worldContext == PlayerProfile::WorldContext::Overworld &&
        profile.activeScreen) {
        fail("progress", "overworld context cannot have an active screen checkpoint");
    }
}

template <typename Json>
void parseSettingsSection(PlayerProfile& profile, const Json& settings)
{
    rejectUnknownProperties(
        settings,
        { "gameplay", "audio", "video", "input" },
        "settings");

    // Older settings documents from this same development format predate the
    // gameplay section. They retain the default 1x simulation speed.
    if (settings.contains("gameplay")) {
        const Json& gameplay = requiredProperty(
            settings, "gameplay", "settings");
        rejectUnknownProperties(
            gameplay,
            { "simulationSpeed", "simulationSpeedPercent" },
            "settings.gameplay");
        if (gameplay.contains("simulationSpeed")) {
            profile.settings.gameplay.simulationSpeed = floatProperty(
                gameplay,
                "simulationSpeed",
                "settings.gameplay");
        } else if (gameplay.contains("simulationSpeedPercent")) {
            // Compatibility with the first simulation-speed settings build.
            profile.settings.gameplay.simulationSpeed =
                static_cast<float>(nonNegativeIntegerProperty(
                    gameplay,
                    "simulationSpeedPercent",
                    "settings.gameplay")) /
                100.0f;
        }
    }

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
        "fullscreen", "vsync", "allowTearing", "frameRateLimit",
        "antiAliasingSamples", "renderScalePercent",
        "customRenderScale", "customRenderScalePercent", "ambientOcclusion",
        "ambientOcclusionStrength", "exposureEv", "windowWidth", "windowHeight",
        "windowMaximized",
    }, "settings.video");
    profile.settings.video.fullscreen =
        boolProperty(video, "fullscreen", "settings.video");
    profile.settings.video.vsync =
        boolProperty(video, "vsync", "settings.video");
    profile.settings.video.allowTearing =
        boolProperty(video, "allowTearing", "settings.video");
    profile.settings.video.frameRateLimit = nonNegativeIntegerProperty(
        video, "frameRateLimit", "settings.video");
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
    profile.settings.video.exposureEv = floatProperty(
        video, "exposureEv", "settings.video");
    profile.settings.video.windowWidth = nonNegativeIntegerProperty(
        video, "windowWidth", "settings.video");
    profile.settings.video.windowHeight = nonNegativeIntegerProperty(
        video, "windowHeight", "settings.video");
    // Settings written before maximized window support preserve their saved
    // fixed size; only a genuinely fresh profile adopts the new default.
    profile.settings.video.windowMaximized = video.contains("windowMaximized")
        ? boolProperty(video, "windowMaximized", "settings.video")
        : false;

    profile.settings.input = inputBindingsFromJson(
        requiredProperty(settings, "input", "settings"),
        "settings.input");

}

template <bool RetainHistory, typename Json>
PlayerProfile parseCurrent(const Json& root)
{
    rejectUnknownProperties(root, { "format", "progress", "settings" }, "root");
    PlayerProfile profile;
    if (root.contains("progress")) {
        parseProgressSection<RetainHistory>(profile, root["progress"]);
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
    return serialize(sections, false);
}

std::string PlayerProfile::serializeForSave(ProfileSections sections) const
{
    return serialize(sections, true);
}

std::string PlayerProfile::serialize(ProfileSections sections, bool validate) const
{
    // Normalize only the small sections that serialization may rewrite. The
    // gameplay checkpoints are immutable here and can be projected directly;
    // copying them can duplicate an entire long undo history.
    PlayerProfile normalized;
    if (sections != ProfileSections::SettingsOnly) {
        normalized.unlockedLevel = unlockedLevel;
        normalized.currentLevel = currentLevel;
        normalized.currentScreen = currentScreen;
        normalized.levels = levels;
        normalized.screens = screens;
        normalized.overworldDiscovery = overworldDiscovery;
        normalized.worldContext = worldContext;
    }
    if (sections != ProfileSections::ProgressOnly) {
        normalized.settings = settings;
    }
    normalized.normalize();

    const ActiveScreen* normalizedActiveScreen = nullptr;
    if (sections != ProfileSections::SettingsOnly && activeScreen &&
        activeScreen->level == normalized.currentLevel &&
        activeScreen->screen == normalized.currentScreen) {
        normalizedActiveScreen = &*activeScreen;
    }

    OrderedJson root = objectWithCapacity(3);
    root["format"] = currentPlayerProfileFormat;

    if (sections != ProfileSections::SettingsOnly) {
        if (normalizedActiveScreen &&
            (normalizedActiveScreen->completedLevelMoveCount < 0 ||
                !std::isfinite(normalizedActiveScreen->levelElapsedSeconds) ||
                normalizedActiveScreen->levelElapsedSeconds < 0.0)) {
            throw std::runtime_error(
                "player profile active screen checkpoint is invalid");
        }
        if (normalized.worldContext == WorldContext::Puzzle &&
            normalizedActiveScreen == nullptr) {
            throw std::runtime_error(
                "puzzle world context requires an active screen checkpoint");
        }
        if (normalized.worldContext == WorldContext::Overworld &&
            normalizedActiveScreen != nullptr) {
            throw std::runtime_error(
                "overworld context cannot have an active screen checkpoint");
        }
        if (overworldCheckpoint && overworldCheckpoint->activeScreen == 0) {
            throw std::runtime_error(
                "player profile overworld checkpoint has no active screen");
        }

        OrderedJson& progress = root["progress"] = objectWithCapacity(9);
        progress["unlockedLevel"] = normalized.unlockedLevel;
        progress["currentLevel"] = normalized.currentLevel;
        progress["currentScreen"] = normalized.currentScreen;

        OrderedJson& levelItems = progress["levels"] =
            arrayWithCapacity(normalized.levels.size());
        for (const LevelProgress& level : normalized.levels) {
            OrderedJson item = objectWithCapacity(5);
            item["level"] = level.level;
            item["completed"] = level.completed;
            item["reachedScreens"] = level.reachedScreens;
            if (level.bestMoves) {
                item["bestMoves"] = *level.bestMoves;
            }
            if (level.bestTimeSeconds) {
                item["bestTimeSeconds"] = *level.bestTimeSeconds;
            }
            levelItems.push_back(std::move(item));
        }

        OrderedJson& screenItems = progress["screens"] =
            arrayWithCapacity(normalized.screens.size());
        for (const ScreenProgress& screen : normalized.screens) {
            OrderedJson item = objectWithCapacity(5);
            item["level"] = screen.level;
            item["screen"] = screen.screen;
            item["completed"] = screen.completed;
            if (screen.bestMoves) {
                item["bestMoves"] = *screen.bestMoves;
            }
            if (screen.bestTimeSeconds) {
                item["bestTimeSeconds"] = *screen.bestTimeSeconds;
            }
            screenItems.push_back(std::move(item));
        }

        OrderedJson& activeScreenJson = progress["activeScreen"];
        if (normalizedActiveScreen) {
            activeScreenJson = objectWithCapacity(5);
            activeScreenJson["level"] = normalizedActiveScreen->level;
            activeScreenJson["screen"] = normalizedActiveScreen->screen;
            activeScreenJson["completedLevelMoveCount"] =
                normalizedActiveScreen->completedLevelMoveCount;
            activeScreenJson["levelElapsedSeconds"] =
                normalizedActiveScreen->levelElapsedSeconds;
            activeScreenJson["session"] =
                sessionSnapshotToJson(normalizedActiveScreen->session);
        }

        OrderedJson& overworldCheckpointJson = progress["overworldCheckpoint"];
        if (overworldCheckpoint) {
            overworldCheckpointJson = objectWithCapacity(3);
            overworldCheckpointJson["topologyFingerprint"] =
                overworldCheckpoint->topologyFingerprint;
            overworldCheckpointJson["activeScreen"] = overworldCheckpoint->activeScreen;
            overworldCheckpointJson["session"] =
                sessionSnapshotToJson(overworldCheckpoint->session);
        }

        progress["overworldDiscovery"] = {
            { "topologyFingerprint", normalized.overworldDiscovery.topologyFingerprint },
            { "screens", normalized.overworldDiscovery.screens },
        };
        progress["worldContext"] =
            normalized.worldContext == WorldContext::Overworld ? "overworld" : "puzzle";
    }

    if (sections != ProfileSections::ProgressOnly) {
        root["settings"] = {
            { "gameplay", {
                { "simulationSpeed",
                  normalized.settings.gameplay.simulationSpeed },
            } },
            { "audio", {
                { "masterVolume", normalized.settings.audio.masterVolume },
                { "musicVolume", normalized.settings.audio.musicVolume },
                { "soundVolume", normalized.settings.audio.soundVolume },
            } },
            { "video", {
                { "fullscreen", normalized.settings.video.fullscreen },
                { "vsync", normalized.settings.video.vsync },
                { "allowTearing", normalized.settings.video.allowTearing },
                { "frameRateLimit", normalized.settings.video.frameRateLimit },
                { "antiAliasingSamples", normalized.settings.video.antiAliasingSamples },
                { "renderScalePercent", normalized.settings.video.renderScalePercent },
                { "customRenderScale", normalized.settings.video.customRenderScale },
                { "customRenderScalePercent", normalized.settings.video.customRenderScalePercent },
                { "ambientOcclusion", normalized.settings.video.ambientOcclusion },
                { "ambientOcclusionStrength", normalized.settings.video.ambientOcclusionStrength },
                { "exposureEv", normalized.settings.video.exposureEv },
                { "windowWidth", normalized.settings.video.windowWidth },
                { "windowHeight", normalized.settings.video.windowHeight },
                { "windowMaximized", normalized.settings.video.windowMaximized },
            } },
            { "input", inputBindingsToJson(normalized.settings.input) },
        };
    }

    // The writer already has the entire document in memory. Run the same
    // checks as the reader before dumping it to UTF-8. No second undo history
    // is needed because the resulting profile is discarded.
    if (validate) {
        (void)parseCurrent<false>(root);
    }

    // Save files can contain a complete undo history. Compact JSON materially
    // reduces the string retained for asynchronous disk writes without
    // changing the versioned document structure.
    return root.dump() + '\n';
}

UnsupportedPlayerProfileFormat::UnsupportedPlayerProfileFormat(int format)
    : std::runtime_error(
          "player profile root: unsupported format " + std::to_string(format))
    , format_(format)
{
}

ObsoletePlayerProfileFormat::ObsoletePlayerProfileFormat(int format)
    : std::runtime_error(
          "player profile root: format " + std::to_string(format) +
          " is from an older build and is no longer read (current format " +
          std::to_string(currentPlayerProfileFormat) + ")")
    , format_(format)
{
}

DecodedPlayerProfile decodePlayerProfile(std::string_view text)
{
    try {
        ProfileJson root;
        try {
            root = ProfileJson::parse(text);
        } catch (const ProfileJson::parse_error& error) {
            throw std::runtime_error(
                "player profile JSON parse error at byte " +
                std::to_string(error.byte) + ": " + error.what());
        }
        requireObject(root, "root");
        const int format = nonNegativeIntegerProperty(root, "format", "root");
        if (format < 1 || format > currentPlayerProfileFormat) {
            throw UnsupportedPlayerProfileFormat(format);
        }
        if (format < currentPlayerProfileFormat) {
            throw ObsoletePlayerProfileFormat(format);
        }
        return { .profile = parseCurrent<true>(root) };
    } catch (const UnsupportedPlayerProfileFormat&) {
        throw;
    } catch (const ObsoletePlayerProfileFormat&) {
        throw;
    } catch (const std::bad_alloc&) {
        throw;
    } catch (const std::exception& error) {
        throw InvalidPlayerProfileData(error.what());
    }
}

} // namespace sokoban

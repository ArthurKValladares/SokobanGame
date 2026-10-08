#include "engine/Level.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>

namespace sokoban {
namespace {

constexpr std::string_view layerPrefix = "@layer ";
constexpr std::string_view waterPrefix = "@water ";
constexpr std::string_view cameraPrefix = "@camera ";
constexpr std::string_view characterPrefix = "@character ";
constexpr std::string_view decorationPrefix = "@decoration ";
constexpr std::string_view lecternPrefix = "@lectern ";
constexpr std::string_view selectorPrefix = "@selector ";
constexpr std::string_view gatePrefix = "@gate ";
constexpr std::string_view rotatorPrefix = "@rotator ";
constexpr std::string_view lockPlatePrefix = "@lockplate ";
constexpr std::string_view elevatorPrefix = "@elevator ";
constexpr std::string_view minecartPrefix = "@minecart ";
constexpr std::string_view portalPrefix = "@portal ";
constexpr std::string_view objectLinkPrefix = "@objectlink ";
constexpr std::string_view linkColorPrefix = "@linkcolor ";
// Far more stops than any board has layers; it keeps an elevator's cycle
// phase (2 * stops - 2 values) inside GameState's 8-bit field.
constexpr std::size_t maximumElevatorStops = 64;
constexpr std::string_view platePrefix = "@plate ";
constexpr std::string_view groundSplatPrefix = "@groundsplat ";
constexpr std::string_view groundPaintPrefix = "@groundpaint ";

using Json = nlohmann::json;

void validateCameraAngles(
    const CameraAngles& angles, std::string_view sourceName)
{
    if (!angles.valid()) {
        throw std::runtime_error(
            "Camera angles must be finite, with pitch in [0, 89] and yaw in "
            "[-180, 180]: " + std::string(sourceName));
    }
}

CameraAngles parseCameraAngles(
    std::string_view payload, std::string_view sourceName)
{
    try {
        const Json object = Json::parse(payload);
        if (!object.is_object() || !object.contains("pitch") ||
            !object.at("pitch").is_number() || !object.contains("yaw") ||
            !object.at("yaw").is_number()) {
            throw std::runtime_error(
                "Camera metadata requires numeric 'pitch' and 'yaw': " +
                std::string(sourceName));
        }
        const CameraAngles angles {
            .pitchDegrees = object.at("pitch").get<float>(),
            .yawDegrees = object.at("yaw").get<float>(),
        };
        validateCameraAngles(angles, sourceName);
        return angles;
    } catch (const Json::exception& error) {
        throw std::runtime_error(
            "Invalid camera JSON in " + std::string(sourceName) +
            ": " + error.what());
    }
}

std::runtime_error unknownLevelCharacter(char value)
{
    return std::runtime_error(std::string("Unknown level tile character: '") + value + "'");
}

std::optional<uint32_t> parseLayerHeader(std::string_view line)
{
    if (!line.starts_with(layerPrefix)) {
        return std::nullopt;
    }

    uint32_t layer = 0;
    const char* begin = line.data() + layerPrefix.size();
    const char* end = line.data() + line.size();
    const auto result = std::from_chars(begin, end, layer);
    if (result.ec != std::errc {} || result.ptr != end) {
        return std::nullopt;
    }
    return layer;
}

std::optional<uint32_t> parseWaterHeader(std::string_view line)
{
    if (!line.starts_with(waterPrefix)) {
        return std::nullopt;
    }

    uint32_t layer = 0;
    const char* begin = line.data() + waterPrefix.size();
    const char* end = line.data() + line.size();
    const auto result = std::from_chars(begin, end, layer);
    if (result.ec != std::errc {} || result.ptr != end) {
        return std::nullopt;
    }
    return layer;
}

Vec3 parseDecorationVec3(
    const Json& object,
    std::string_view field,
    std::string_view sourceName)
{
    const auto found = object.find(field);
    if (found == object.end() || !found->is_array() || found->size() != 3) {
        throw std::runtime_error(
            "Decoration '" + std::string(field) +
            "' must be an array of three numbers: " +
            std::string(sourceName));
    }

    Vec3 value;
    float* components[] { &value.x, &value.y, &value.z };
    for (size_t i = 0; i < 3; ++i) {
        if (!(*found)[i].is_number()) {
            throw std::runtime_error(
                "Decoration '" + std::string(field) +
                "' must contain only numbers: " +
                std::string(sourceName));
        }
        *components[i] = (*found)[i].get<float>();
        if (!std::isfinite(*components[i])) {
            throw std::runtime_error(
                "Decoration '" + std::string(field) +
                "' must contain finite numbers: " +
                std::string(sourceName));
        }
    }
    return value;
}

void validateDecoration(
    const Level::Decoration& decoration,
    std::string_view sourceName)
{
    if (decoration.model.empty()) {
        throw std::runtime_error(
            "Decoration model must not be empty: " +
            std::string(sourceName));
    }
    const auto finite = [](Vec3 value) {
        return std::isfinite(value.x) &&
            std::isfinite(value.y) &&
            std::isfinite(value.z);
    };
    if (!finite(decoration.position) ||
        !finite(decoration.rotationDegrees) ||
        !finite(decoration.scale)) {
        throw std::runtime_error(
            "Decoration transforms must contain finite numbers: " +
            std::string(sourceName));
    }
    if (decoration.scale.x <= 0.0f ||
        decoration.scale.y <= 0.0f ||
        decoration.scale.z <= 0.0f) {
        throw std::runtime_error(
            "Decoration scale components must be greater than zero: " +
            std::string(sourceName));
    }
    if (decoration.pointLight) {
        const Level::Decoration::PointLight& light = *decoration.pointLight;
        if (!finite(light.offset) || !finite(light.color) ||
            !std::isfinite(light.intensity) || !std::isfinite(light.range) ||
            !std::isfinite(light.shadowBias) ||
            !std::isfinite(light.shadowOpacity)) {
            throw std::runtime_error(
                "Decoration point-light settings must contain finite values: " +
                std::string(sourceName));
        }
        if (light.color.x < 0.0f || light.color.y < 0.0f ||
            light.color.z < 0.0f || light.intensity < 0.0f ||
            light.range <= 0.0f || light.shadowBias < 0.0f ||
            light.shadowOpacity < 0.0f || light.shadowOpacity > 1.0f) {
            throw std::runtime_error(
                "Decoration point-light color, intensity, range, and shadow settings are out of range: " +
                std::string(sourceName));
        }
    }
}

Level::Decoration parseDecoration(
    std::string_view payload,
    std::string_view sourceName)
{
    try {
        const Json object = Json::parse(payload);
        if (!object.is_object()) {
            throw std::runtime_error("decoration payload is not an object");
        }
        const auto model = object.find("model");
        if (model == object.end() || !model->is_string()) {
            throw std::runtime_error("decoration 'model' must be a string");
        }
        Level::Decoration decoration {
            .model = model->get<std::string>(),
            .position = parseDecorationVec3(
                object, "position", sourceName),
            .rotationDegrees = parseDecorationVec3(
                object, "rotation", sourceName),
            .scale = parseDecorationVec3(
                object, "scale", sourceName),
        };
        if (const auto light = object.find("light");
            light != object.end() && !light->is_null()) {
            if (!light->is_object()) {
                throw std::runtime_error(
                    "decoration 'light' must be an object or null");
            }
            const auto number = [&](std::string_view field) {
                const auto value = light->find(field);
                if (value == light->end() || !value->is_number()) {
                    throw std::runtime_error(
                        "decoration light '" + std::string(field) +
                        "' must be a number");
                }
                return value->get<float>();
            };
            const auto castsShadows = light->find("castsShadows");
            if (castsShadows == light->end() || !castsShadows->is_boolean()) {
                throw std::runtime_error(
                    "decoration light 'castsShadows' must be a boolean");
            }
            decoration.pointLight = Level::Decoration::PointLight {
                .offset = parseDecorationVec3(*light, "offset", sourceName),
                .color = parseDecorationVec3(*light, "color", sourceName),
                .intensity = number("intensity"),
                .range = number("range"),
                .castsShadows = castsShadows->get<bool>(),
                .shadowBias = number("shadowBias"),
                .shadowOpacity = number("shadowOpacity"),
            };
        }
        validateDecoration(decoration, sourceName);
        return decoration;
    } catch (const nlohmann::json::exception& error) {
        throw std::runtime_error(
            "Invalid decoration JSON in " + std::string(sourceName) +
            ": " + error.what());
    } catch (const std::runtime_error& error) {
        throw std::runtime_error(
            "Invalid decoration in " + std::string(sourceName) +
            ": " + error.what());
    }
}

std::string serializeDecoration(const Level::Decoration& decoration)
{
    validateDecoration(decoration, "serialized level");
    Json object {
        { "model", decoration.model },
        { "position", {
              decoration.position.x,
              decoration.position.y,
              decoration.position.z,
          } },
        { "rotation", {
              decoration.rotationDegrees.x,
              decoration.rotationDegrees.y,
              decoration.rotationDegrees.z,
          } },
        { "scale", {
              decoration.scale.x,
              decoration.scale.y,
              decoration.scale.z,
        } },
    };
    if (decoration.pointLight) {
        const Level::Decoration::PointLight& light = *decoration.pointLight;
        object["light"] = {
            { "offset", { light.offset.x, light.offset.y, light.offset.z } },
            { "color", { light.color.x, light.color.y, light.color.z } },
            { "intensity", light.intensity },
            { "range", light.range },
            { "castsShadows", light.castsShadows },
            { "shadowBias", light.shadowBias },
            { "shadowOpacity", light.shadowOpacity },
        };
    }
    return std::string(decorationPrefix) + object.dump();
}

int parseSelectorInteger(
    const Json& object,
    std::string_view field,
    std::string_view sourceName)
{
    const auto found = object.find(field);
    if (found == object.end() || !found->is_number_integer()) {
        throw std::runtime_error(
            "Selector '" + std::string(field) +
            "' must be an integer: " + std::string(sourceName));
    }
    const int value = found->get<int>();
    if (value < 0) {
        throw std::runtime_error(
            "Selector '" + std::string(field) +
            "' must not be negative: " + std::string(sourceName));
    }
    return value;
}

GridPosition3 parseSelectorCell(
    const Json& object,
    std::string_view sourceName)
{
    const auto found = object.find("cell");
    if (found == object.end() || !found->is_array() || found->size() != 3) {
        throw std::runtime_error(
            "Selector 'cell' must be an array of three integers: " +
            std::string(sourceName));
    }
    GridPosition3 cell;
    int* components[] { &cell.x, &cell.y, &cell.z };
    for (size_t i = 0; i < 3; ++i) {
        if (!(*found)[i].is_number_integer()) {
            throw std::runtime_error(
                "Selector 'cell' must contain only integers: " +
                std::string(sourceName));
        }
        *components[i] = (*found)[i].get<int>();
        if (*components[i] < 0) {
            throw std::runtime_error(
                "Selector cell coordinates must not be negative: " +
                std::string(sourceName));
        }
    }
    return cell;
}

void validateSelectorRecord(
    const Level::ScreenSelector& selector,
    std::string_view sourceName)
{
    if (selector.id == 0) {
        throw std::runtime_error(
            "Selector id must be greater than zero: " +
            std::string(sourceName));
    }
    if (selector.cell.x < 0 || selector.cell.y < 0 || selector.cell.z < 0) {
        throw std::runtime_error(
            "Selector cell coordinates must not be negative: " +
            std::string(sourceName));
    }
    if (selector.target &&
        (selector.target->level < 0 || selector.target->screen < 0)) {
        throw std::runtime_error(
            "Selector target coordinates must not be negative: " +
            std::string(sourceName));
    }
}

Level::ScreenSelector parseSelector(
    std::string_view payload,
    std::string_view sourceName)
{
    try {
        const Json object = Json::parse(payload);
        if (!object.is_object()) {
            throw std::runtime_error("selector payload is not an object");
        }
        const int id = parseSelectorInteger(object, "id", sourceName);
        if (id == 0) {
            throw std::runtime_error("selector 'id' must be greater than zero");
        }
        const auto target = object.find("target");
        if (target == object.end()) {
            throw std::runtime_error("selector 'target' is required");
        }
        std::optional<LevelLocation> location;
        if (!target->is_null()) {
            if (!target->is_object()) {
                throw std::runtime_error(
                    "selector 'target' must be an object or null");
            }
            location = LevelLocation {
                .level = parseSelectorInteger(*target, "level", sourceName),
                .screen = parseSelectorInteger(*target, "screen", sourceName),
            };
        }
        Level::ScreenSelector selector {
            .id = static_cast<uint32_t>(id),
            .cell = parseSelectorCell(object, sourceName),
            .target = location,
        };
        validateSelectorRecord(selector, sourceName);
        return selector;
    } catch (const nlohmann::json::exception& error) {
        throw std::runtime_error(
            "Invalid selector JSON in " + std::string(sourceName) +
            ": " + error.what());
    } catch (const std::runtime_error& error) {
        throw std::runtime_error(
            "Invalid selector in " + std::string(sourceName) +
            ": " + error.what());
    }
}

std::string serializeSelector(const Level::ScreenSelector& selector)
{
    validateSelectorRecord(selector, "serialized level");
    Json target = nullptr;
    if (selector.target) {
        target = {
            { "level", selector.target->level },
            { "screen", selector.target->screen },
        };
    }
    const Json object {
        { "id", selector.id },
        { "cell", {
              selector.cell.x,
              selector.cell.y,
              selector.cell.z,
          } },
        { "target", std::move(target) },
    };
    return std::string(selectorPrefix) + object.dump();
}

// Gates and rotators are both linked to pressure plates by an authored
// metadata record with the same shape: the device cell, the plates that drive
// it, and the color the device and its plates share. `kind` is the capitalized
// record name used in diagnostics ("Gate", "Rotator").
std::string lowercaseKind(std::string_view kind)
{
    std::string lowered(kind);
    for (char& character : lowered) {
        character = static_cast<char>(
            std::tolower(static_cast<unsigned char>(character)));
    }
    return lowered;
}

GridPosition3 parseLinkedCell(
    const Json& value,
    std::string_view field,
    std::string_view sourceName,
    std::string_view kind)
{
    if (!value.is_array() || value.size() != 3) {
        throw std::runtime_error(
            std::string(kind) + " '" + std::string(field) +
            "' must be an array of three integers: " +
            std::string(sourceName));
    }
    GridPosition3 cell;
    int* components[] { &cell.x, &cell.y, &cell.z };
    for (std::size_t i = 0; i < 3; ++i) {
        if (!value[i].is_number_integer()) {
            throw std::runtime_error(
                std::string(kind) + " '" + std::string(field) +
                "' must contain only integers: " +
                std::string(sourceName));
        }
        *components[i] = value[i].get<int>();
        if (*components[i] < 0) {
            throw std::runtime_error(
                std::string(kind) + " cell coordinates must not be negative: " +
                std::string(sourceName));
        }
    }
    return cell;
}

Level::Lectern parseLectern(std::string_view payload, std::string_view sourceName,
    std::optional<TileType>& legacyDirection)
{
    try {
        const Json object = Json::parse(payload);
        if (!object.is_object() || !object.contains("cell") ||
            !object.contains("text") || !object["text"].is_string()) {
            throw std::runtime_error("expected an object with cell and text");
        }
        Level::Lectern lectern {
            .cell = parseLinkedCell(object["cell"], "cell", sourceName, "Lectern"),
            .text = object["text"].get<std::string>(),
        };
        if (const auto direction = object.find("direction"); direction != object.end()) {
            if (!direction->is_number_integer() || *direction < 0 || *direction > 3) {
                throw std::runtime_error("lectern 'direction' must be an integer from 0 to 3");
            }
            constexpr std::array directions { TileType::LecternNorth, TileType::LecternEast,
                TileType::LecternSouth, TileType::LecternWest };
            legacyDirection = directions[direction->get<std::size_t>()];
        }
        return lectern;
    } catch (const std::exception& error) {
        throw std::runtime_error("Invalid lectern in " + std::string(sourceName) +
            ": " + error.what());
    }
}

template <typename Record>
void validateLinkedRecord(
    const Record& record,
    std::string_view sourceName,
    std::string_view kind)
{
    if (record.cell.x < 0 || record.cell.y < 0 || record.cell.z < 0) {
        throw std::runtime_error(
            std::string(kind) + " cell coordinates must not be negative: " +
            std::string(sourceName));
    }
    const auto validColor = [](float component) {
        return std::isfinite(component) && component >= 0.0f &&
            component <= 1.0f;
    };
    if (!validColor(record.color.x) || !validColor(record.color.y) ||
        !validColor(record.color.z)) {
        throw std::runtime_error(
            std::string(kind) +
            " color components must be finite values from zero to one: " +
            std::string(sourceName));
    }
    if constexpr (requires { record.levels; }) {
        if (record.levels.empty()) {
            throw std::runtime_error(
                std::string(kind) + " must list at least one stop level: " +
                std::string(sourceName));
        }
        if (record.levels.size() > maximumElevatorStops) {
            throw std::runtime_error(
                std::string(kind) + " lists more than " +
                std::to_string(maximumElevatorStops) +
                " stop levels: " + std::string(sourceName));
        }
        for (std::size_t i = 0; i < record.levels.size(); ++i) {
            if (record.levels[i] < 0) {
                throw std::runtime_error(
                    std::string(kind) + " stop levels must not be negative: " +
                    std::string(sourceName));
            }
            if (std::ranges::find(
                    record.levels.begin(),
                    record.levels.begin() + static_cast<std::ptrdiff_t>(i),
                    record.levels[i]) !=
                record.levels.begin() + static_cast<std::ptrdiff_t>(i)) {
                throw std::runtime_error(
                    std::string(kind) +
                    " lists the same stop level more than once: " +
                    std::string(sourceName));
            }
        }
        if (std::ranges::find(record.levels, record.cell.z) ==
            record.levels.end()) {
            throw std::runtime_error(
                std::string(kind) +
                " stop levels must include the layer its tile is on: " +
                std::string(sourceName));
        }
    }
    if constexpr (requires { record.initialDirection; }) {
        if (record.initialDirection > 3) {
            throw std::runtime_error(
                std::string(kind) +
                " initial direction must be 0 (north), 1 (east), 2 (south), or 3 (west): " +
                std::string(sourceName));
        }
    }
    for (std::size_t i = 0; i < record.pressurePlates.size(); ++i) {
        const GridPosition3 plate = record.pressurePlates[i];
        if (plate.x < 0 || plate.y < 0 || plate.z < 0) {
            throw std::runtime_error(
                std::string(kind) +
                " pressure-plate coordinates must not be negative: " +
                std::string(sourceName));
        }
        if (std::ranges::find(
                record.pressurePlates.begin(),
                record.pressurePlates.begin() +
                    static_cast<std::ptrdiff_t>(i),
                plate) != record.pressurePlates.begin() +
                    static_cast<std::ptrdiff_t>(i)) {
            throw std::runtime_error(
                std::string(kind) +
                " contains the same pressure plate more than once: " +
                std::string(sourceName));
        }
    }
}

template <typename Record>
Record parseLinkedRecord(
    std::string_view payload,
    std::string_view sourceName,
    std::string_view kind)
{
    const std::string lower = lowercaseKind(kind);
    try {
        const Json object = Json::parse(payload);
        if (!object.is_object()) {
            throw std::runtime_error(lower + " payload is not an object");
        }
        const auto cell = object.find("cell");
        const auto plates = object.find("plates");
        const auto color = object.find("color");
        if (cell == object.end()) {
            throw std::runtime_error(lower + " 'cell' is required");
        }
        if (plates == object.end() || !plates->is_array()) {
            throw std::runtime_error(lower + " 'plates' must be an array");
        }
        if (color == object.end() || !color->is_array() ||
            color->size() != 3) {
            throw std::runtime_error(
                lower + " 'color' must be an array of three numbers");
        }
        Record record {
            .cell = parseLinkedCell(*cell, "cell", sourceName, kind),
        };
        record.pressurePlates.reserve(plates->size());
        for (const Json& plate : *plates) {
            record.pressurePlates.push_back(
                parseLinkedCell(plate, "plates", sourceName, kind));
        }
        float* components[] {
            &record.color.x, &record.color.y, &record.color.z,
        };
        for (std::size_t i = 0; i < 3; ++i) {
            if (!(*color)[i].is_number()) {
                throw std::runtime_error(
                    lower + " 'color' must contain only numbers");
            }
            *components[i] = (*color)[i].get<float>();
        }
        if constexpr (requires { record.levels; }) {
            const auto levels = object.find("levels");
            if (levels == object.end() || !levels->is_array()) {
                throw std::runtime_error(
                    lower + " 'levels' must be an array of layer numbers");
            }
            for (const Json& level : *levels) {
                if (!level.is_number_integer()) {
                    throw std::runtime_error(
                        lower + " 'levels' must contain only integers");
                }
                record.levels.push_back(level.get<int>());
            }
        }
        if constexpr (requires { record.startEnabled; }) {
            const auto enabled = object.find("startEnabled");
            if (enabled != object.end()) {
                if (!enabled->is_boolean()) {
                    throw std::runtime_error(
                        lower + " 'startEnabled' must be true or false");
                }
                record.startEnabled = enabled->get<bool>();
            }
        }
        if constexpr (requires { record.startOpen; }) {
            const auto startOpen = object.find("startOpen");
            if (startOpen != object.end()) {
                if (!startOpen->is_boolean()) {
                    throw std::runtime_error(
                        lower + " 'startOpen' must be true or false");
                }
                record.startOpen = startOpen->get<bool>();
            }
        }
        if constexpr (requires { record.initialDirection; }) {
            const auto direction = object.find("direction");
            if (direction == object.end() || !direction->is_number_integer()) {
                throw std::runtime_error(
                    lower + " 'direction' must be an integer from 0 to 3");
            }
            const int value = direction->get<int>();
            if (value < 0 || value > 3) {
                throw std::runtime_error(
                    lower + " 'direction' must be an integer from 0 to 3");
            }
            record.initialDirection = static_cast<uint8_t>(value);
        }
        validateLinkedRecord(record, sourceName, kind);
        return record;
    } catch (const nlohmann::json::exception& error) {
        throw std::runtime_error(
            "Invalid " + lower + " JSON in " + std::string(sourceName) +
            ": " + error.what());
    } catch (const std::runtime_error& error) {
        throw std::runtime_error(
            "Invalid " + lower + " in " + std::string(sourceName) +
            ": " + error.what());
    }
}

template <typename Record>
std::string serializeLinkedRecord(
    const Record& record,
    std::string_view prefix,
    std::string_view kind)
{
    validateLinkedRecord(record, "serialized level", kind);
    Json plates = Json::array();
    for (GridPosition3 plate : record.pressurePlates) {
        plates.push_back({ plate.x, plate.y, plate.z });
    }
    Json object {
        { "cell", { record.cell.x, record.cell.y, record.cell.z } },
        { "plates", std::move(plates) },
        { "color", { record.color.x, record.color.y, record.color.z } },
    };
    if constexpr (requires { record.levels; }) {
        object["levels"] = record.levels;
    }
    if constexpr (requires { record.startEnabled; }) {
        if (record.startEnabled) {
            object["startEnabled"] = true;
        }
    }
    if constexpr (requires { record.startOpen; }) {
        // Omitted when false so existing screens serialize unchanged.
        if (record.startOpen) {
            object["startOpen"] = true;
        }
    }
    if constexpr (requires { record.initialDirection; }) {
        object["direction"] = record.initialDirection;
    }
    return std::string(prefix) + object.dump();
}

// Sorts records into canonical z/y/x order and rejects invalid or duplicate
// cells, so parsing, serialization and runtime levels agree on one order.
template <typename Record>
void canonicalizeLinkedRecords(
    std::vector<Record>& records,
    std::string_view sourceName,
    std::string_view kind)
{
    std::ranges::sort(records, {}, [](const Record& record) {
        return std::array { record.cell.z, record.cell.y, record.cell.x };
    });
    for (std::size_t i = 0; i < records.size(); ++i) {
        validateLinkedRecord(records[i], sourceName, kind);
        if (i > 0 && records[i - 1].cell == records[i].cell) {
            throw std::runtime_error(
                "Level contains more than one " + lowercaseKind(kind) +
                " record at the same cell: " + std::string(sourceName));
        }
    }
}

Level::Plate parsePlate(std::string_view payload, std::string_view sourceName)
{
    try {
        const Json object = Json::parse(payload);
        if (!object.is_object()) {
            throw std::runtime_error("plate payload is not an object");
        }
        const auto cell = object.find("cell");
        const auto tile = object.find("tile");
        if (cell == object.end()) {
            throw std::runtime_error("plate 'cell' is required");
        }
        if (tile == object.end() || !tile->is_string()) {
            throw std::runtime_error("plate 'tile' must be a tile name");
        }
        const std::optional<TileType> type =
            tileTypeFromName(tile->get<std::string>());
        if (!type || (!tileTypeIsPlate(*type) && !tileTypeIsRail(*type))) {
            throw std::runtime_error(
                "plate 'tile' must name a plate tile (Pressure, End, Portal, "
                "Rotator or Rail)");
        }
        return Level::Plate {
            .cell = parseLinkedCell(*cell, "cell", sourceName, "Plate"),
            .tile = *type,
        };
    } catch (const nlohmann::json::exception& error) {
        throw std::runtime_error(
            "Invalid plate JSON in " + std::string(sourceName) +
            ": " + error.what());
    } catch (const std::runtime_error& error) {
        throw std::runtime_error(
            "Invalid plate in " + std::string(sourceName) +
            ": " + error.what());
    }
}

std::string serializePlate(const Level::Plate& plate)
{
    if (!tileTypeIsPlate(plate.tile) && !tileTypeIsRail(plate.tile)) {
        throw std::runtime_error("Cannot serialize a plate record for a non-plate tile");
    }
    const Json object {
        { "cell", { plate.cell.x, plate.cell.y, plate.cell.z } },
        { "tile", std::string(tileTypeName(plate.tile)) },
    };
    return std::string(platePrefix) + object.dump();
}

void canonicalizePlates(
    std::vector<Level::Plate>& plates, std::string_view sourceName)
{
    std::ranges::sort(plates, {}, [](const Level::Plate& plate) {
        return std::array { plate.cell.z, plate.cell.y, plate.cell.x };
    });
    for (std::size_t i = 0; i < plates.size(); ++i) {
        const GridPosition3 cell = plates[i].cell;
        if (cell.x < 0 || cell.y < 0 || cell.z < 0) {
            throw std::runtime_error(
                "Plate cell coordinates must not be negative: " +
                std::string(sourceName));
        }
        if (!tileTypeIsPlate(plates[i].tile) && !tileTypeIsRail(plates[i].tile)) {
            throw std::runtime_error(
                "Plate records must name a plate tile: " +
                std::string(sourceName));
        }
        if (i > 0 && plates[i - 1].cell == cell) {
            throw std::runtime_error(
                "Level contains more than one plate record at the same cell: " +
                std::string(sourceName));
        }
    }
}

Level::LinkColor parseLinkColor(
    std::string_view payload, std::string_view sourceName)
{
    try {
        const Json object = Json::parse(payload);
        if (!object.is_object()) {
            throw std::runtime_error("link color payload is not an object");
        }
        const auto cell = object.find("cell");
        const auto color = object.find("color");
        if (cell == object.end()) {
            throw std::runtime_error("link color 'cell' is required");
        }
        if (color == object.end() || !color->is_array() ||
            color->size() != 3) {
            throw std::runtime_error(
                "link color 'color' must be an array of three numbers");
        }
        Level::LinkColor record {
            .cell = parseLinkedCell(*cell, "cell", sourceName, "Link color"),
        };
        float* components[] {
            &record.color.x, &record.color.y, &record.color.z,
        };
        for (std::size_t i = 0; i < 3; ++i) {
            if (!(*color)[i].is_number()) {
                throw std::runtime_error(
                    "link color 'color' must contain only numbers");
            }
            *components[i] = (*color)[i].get<float>();
        }
        return record;
    } catch (const nlohmann::json::exception& error) {
        throw std::runtime_error(
            "Invalid link color JSON in " + std::string(sourceName) +
            ": " + error.what());
    } catch (const std::runtime_error& error) {
        throw std::runtime_error(
            "Invalid link color in " + std::string(sourceName) +
            ": " + error.what());
    }
}

std::string serializeLinkColor(const Level::LinkColor& record)
{
    const Json object {
        { "cell", { record.cell.x, record.cell.y, record.cell.z } },
        { "color", { record.color.x, record.color.y, record.color.z } },
    };
    return std::string(linkColorPrefix) + object.dump();
}

void canonicalizeLinkColors(
    std::vector<Level::LinkColor>& records, std::string_view sourceName)
{
    std::ranges::sort(records, {}, [](const Level::LinkColor& record) {
        return std::array { record.cell.z, record.cell.y, record.cell.x };
    });
    const auto validColor = [](float component) {
        return std::isfinite(component) && component >= 0.0f &&
            component <= 1.0f;
    };
    for (std::size_t i = 0; i < records.size(); ++i) {
        const Level::LinkColor& record = records[i];
        if (record.cell.x < 0 || record.cell.y < 0 || record.cell.z < 0) {
            throw std::runtime_error(
                "Link color cell coordinates must not be negative: " +
                std::string(sourceName));
        }
        if (!validColor(record.color.x) || !validColor(record.color.y) ||
            !validColor(record.color.z)) {
            throw std::runtime_error(
                "Link color components must be finite values from zero to one: " +
                std::string(sourceName));
        }
        if (i > 0 && records[i - 1].cell == record.cell) {
            throw std::runtime_error(
                "Level contains more than one link color at the same cell: " +
                std::string(sourceName));
        }
    }
}

Level::ObjectLink parseObjectLink(
    std::string_view payload, std::string_view sourceName)
{
    const Level::LinkColor parsed = parseLinkColor(payload, sourceName);
    return { .cell = parsed.cell, .color = parsed.color };
}

std::string serializeObjectLink(const Level::ObjectLink& record)
{
    const Json object {
        { "cell", { record.cell.x, record.cell.y, record.cell.z } },
        { "color", { record.color.x, record.color.y, record.color.z } },
    };
    return std::string(objectLinkPrefix) + object.dump();
}

void canonicalizeObjectLinks(
    std::vector<Level::ObjectLink>& records, std::string_view sourceName)
{
    std::ranges::sort(records, {}, [](const Level::ObjectLink& record) {
        return std::array { record.cell.z, record.cell.y, record.cell.x };
    });
    const auto validColor = [](float component) {
        return std::isfinite(component) && component >= 0.0f &&
            component <= 1.0f;
    };
    for (std::size_t i = 0; i < records.size(); ++i) {
        const Level::ObjectLink& record = records[i];
        if (record.cell.x < 0 || record.cell.y < 0 || record.cell.z < 0) {
            throw std::runtime_error(
                "Object link cell coordinates must not be negative: " +
                std::string(sourceName));
        }
        if (!validColor(record.color.x) || !validColor(record.color.y) ||
            !validColor(record.color.z)) {
            throw std::runtime_error(
                "Object link color components must be finite values from zero to one: " +
                std::string(sourceName));
        }
        if (i > 0 && records[i - 1].cell == record.cell) {
            throw std::runtime_error(
                "Level contains more than one object link at the same cell: " +
                std::string(sourceName));
        }
    }
}

std::array<int, 3> objectLinkColorKey(Vec3 color)
{
    const auto channel = [](float value) {
        return static_cast<int>(std::lround(
            std::clamp(value, 0.0f, 1.0f) * 255.0f));
    };
    return { channel(color.x), channel(color.y), channel(color.z) };
}

size_t tileIndex(uint32_t x, uint32_t y, uint32_t z, uint32_t width, uint32_t height)
{
    return (static_cast<size_t>(z) * height + y) * width + x;
}

bool hasAdjacentGround(const Level& level, GridPosition3 position)
{
    constexpr std::array<GridPosition, 4> offsets {
        GridPosition { 0, -1 },
        GridPosition { 1, 0 },
        GridPosition { 0, 1 },
        GridPosition { -1, 0 },
    };

    for (GridPosition offset : offsets) {
        const GridPosition3 neighbor {
            position.x + offset.x,
            position.y + offset.y,
            position.z,
        };
        if (!level.inBounds(neighbor)) {
            continue;
        }
        if (tileTypeIsGround(level.tileAt(
                static_cast<uint32_t>(neighbor.x),
                static_cast<uint32_t>(neighbor.y),
                static_cast<uint32_t>(neighbor.z)))) {
            return true;
        }
    }

    return false;
}

} // namespace

namespace {

uint8_t railDirectionBit(uint8_t direction)
{
    return static_cast<uint8_t>(1U << direction);
}

uint8_t oppositeRailDirectionBit(uint8_t direction)
{
    return railDirectionBit(static_cast<uint8_t>((direction + 2U) % 4U));
}

GridPosition railDirectionOffset(uint8_t direction)
{
    constexpr std::array<GridPosition, 4> offsets {
        GridPosition { 0, -1 },
        GridPosition { 1, 0 },
        GridPosition { 0, 1 },
        GridPosition { -1, 0 },
    };
    return offsets.at(direction);
}

TileType railTileAt(const Level& level, GridPosition3 cell)
{
    if (!level.inBounds(cell)) {
        return TileType::Air;
    }
    const std::optional<TileType> plate = level.plateAt(cell);
    if (plate && tileTypeIsRail(*plate)) {
        return *plate;
    }
    return level.tileAt(
        static_cast<uint32_t>(cell.x),
        static_cast<uint32_t>(cell.y),
        static_cast<uint32_t>(cell.z));
}

bool railsConnect(
    const std::function<TileType(GridPosition3)>& railAt,
    GridPosition3 from,
    uint8_t direction,
    GridPosition3 to)
{
    return (railConnectionMask(railAt(from)) &
               railDirectionBit(direction)) != 0 &&
        (railConnectionMask(railAt(to)) &
               oppositeRailDirectionBit(direction)) != 0;
}

} // namespace

Level::MinecartRoute Level::buildMinecartRoute(
    const Level::Minecart& minecart,
    const std::function<TileType(GridPosition3)>& railAt,
    std::string_view sourceName)
{
    Level::MinecartRoute route;
    route.cells.push_back(minecart.cell);
    route.stops.push_back(minecart.cell);
    route.stopCellIndices.push_back(0);

    if (minecart.initialDirection >= 4) {
        return route;
    }

    const GridPosition firstOffset =
        railDirectionOffset(minecart.initialDirection);
    GridPosition3 previous = minecart.cell;
    GridPosition3 current {
        minecart.cell.x + firstOffset.x,
        minecart.cell.y + firstOffset.y,
        minecart.cell.z,
    };
    if (!railsConnect(
            railAt, minecart.cell, minecart.initialDirection, current)) {
        return route;
    }

    while (true) {
        if (current == minecart.cell) {
            route.loop = true;
            break;
        }
        if (std::ranges::find(route.cells, current) != route.cells.end()) {
            throw std::runtime_error(
                "Minecart rail route intersects itself before returning to its start: " +
                std::string(sourceName));
        }
        route.cells.push_back(current);
        if (tileTypeIsRailStop(railAt(current))) {
            route.stops.push_back(current);
            route.stopCellIndices.push_back(route.cells.size() - 1);
        }

        const uint8_t connections = railConnectionMask(railAt(current));
        bool foundNext = false;
        GridPosition3 next {};
        for (uint8_t direction = 0; direction < 4; ++direction) {
            if ((connections & railDirectionBit(direction)) == 0) {
                continue;
            }
            const GridPosition offset = railDirectionOffset(direction);
            const GridPosition3 candidate {
                current.x + offset.x,
                current.y + offset.y,
                current.z,
            };
            if (candidate == previous ||
                !railsConnect(railAt, current, direction, candidate)) {
                continue;
            }
            if (foundNext) {
                throw std::runtime_error(
                    "Minecart rail route branches: " +
                    std::string(sourceName));
            }
            foundNext = true;
            next = candidate;
        }
        if (!foundNext) {
            break;
        }
        previous = current;
        current = next;
    }
    return route;
}

Level Level::loadFromFile(const std::filesystem::path& path)
{
    return loadFromDefinition(loadDefinitionFromFile(path), path.string());
}

Level::Definition Level::loadDefinitionFromFile(
    const std::filesystem::path& path)
{
    std::ifstream file(path);
    if (!file) {
        throw std::runtime_error("Failed to open level file: " + path.string());
    }

    std::vector<std::string> lines;
    std::string line;
    while (std::getline(file, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        lines.push_back(line);
    }

    return parseDefinition(lines, path.string());
}

Level::Definition Level::parseDefinition(
    const std::vector<std::string>& lines,
    std::string_view sourceName)
{
    const std::string source(sourceName);
    if (lines.empty()) {
        throw std::runtime_error("Level is empty: " + source);
    }

    const bool layered = std::ranges::any_of(lines, [](const std::string& line) {
        return line.starts_with(layerPrefix);
    });
    if (!layered) {
        if (std::ranges::any_of(lines, [](const std::string& line) {
                return line.starts_with(waterPrefix) ||
                    line.starts_with(cameraPrefix) ||
                    line.starts_with(characterPrefix) ||
                    line.starts_with(decorationPrefix) ||
                    line.starts_with(selectorPrefix) ||
                    line.starts_with(lecternPrefix) ||
                    line.starts_with(gatePrefix) ||
                    line.starts_with(rotatorPrefix) ||
                    line.starts_with(lockPlatePrefix) ||
                    line.starts_with(elevatorPrefix) ||
                    line.starts_with(minecartPrefix) ||
                    line.starts_with(objectLinkPrefix) ||
                    line.starts_with(linkColorPrefix) ||
                    line.starts_with(platePrefix) ||
                    line.starts_with(groundSplatPrefix) ||
                    line.starts_with(groundPaintPrefix);
            })) {
            throw std::runtime_error(
                "Level metadata requires explicit '@layer 0' sections: " + source);
        }
        return { .layers = { lines } };
    }

    Definition definition;
    std::vector<std::pair<GridPosition3, TileType>> legacyLecternDirections;
    std::optional<uint32_t> currentLayer;
    for (const std::string& line : lines) {
        if (line.starts_with(groundSplatPrefix) || line.starts_with(groundPaintPrefix)) {
            if (currentLayer) {
                throw std::runtime_error("Ground metadata must appear before '@layer 0': " + source);
            }
            try {
                const bool splat = line.starts_with(groundSplatPrefix);
                const Json object = Json::parse(std::string_view(line).substr(
                    splat ? groundSplatPrefix.size() : groundPaintPrefix.size()));
                if (splat) {
                    definition.groundSplats.push_back({
                        .name = object.at("name").get<std::string>(),
                        .base = object.at("base").get<std::string>(),
                        .detail = object.at("detail").get<std::string>(),
                        .mask = object.at("mask").get<std::string>(),
                        .color = parseDecorationVec3(object, "color", sourceName),
                    });
                } else {
                    definition.groundPaint.push_back({
                        .cell = parseLinkedCell(object.at("cell"), "cell", sourceName, "Ground paint"),
                        .splat = object.at("splat").get<std::string>(),
                    });
                }
            } catch (const Json::exception& error) {
                throw std::runtime_error("Invalid ground JSON in " + source + ": " + error.what());
            }
            continue;
        }
        if (line.starts_with(cameraPrefix)) {
            if (currentLayer) {
                throw std::runtime_error(
                    "Camera metadata must appear before '@layer 0': " + source);
            }
            if (definition.cameraAngles) {
                throw std::runtime_error(
                    "Level contains more than one '@camera' directive: " + source);
            }
            definition.cameraAngles = parseCameraAngles(
                std::string_view(line).substr(cameraPrefix.size()), sourceName);
            continue;
        }

        if (line.starts_with(characterPrefix)) {
            if (currentLayer) {
                throw std::runtime_error(
                    "Character metadata must appear before '@layer 0': " + source);
            }
            if (definition.character) {
                throw std::runtime_error(
                    "Level contains more than one '@character' directive: " + source);
            }
            const std::string_view name =
                std::string_view(line).substr(characterPrefix.size());
            definition.character = characterTypeFromName(name);
            if (!definition.character) {
                throw std::runtime_error(
                    "Invalid character metadata; expected '@character lorekeeper', "
                    "'@character rogue', '@character knight', '@character druid', "
                    "'@character witch', or '@character bard': " + source);
            }
            continue;
        }

        if (line.starts_with(lecternPrefix)) {
            if (!definition.layers.empty()) {
                throw std::runtime_error(
                    "Lectern metadata must appear before '@layer 0': " + source);
            }
            std::optional<TileType> legacyDirection;
            definition.lecterns.push_back(parseLectern(
                std::string_view(line).substr(lecternPrefix.size()), sourceName, legacyDirection));
            if (legacyDirection) {
                legacyLecternDirections.emplace_back(definition.lecterns.back().cell, *legacyDirection);
            }
            continue;
        }
        if (line.starts_with(selectorPrefix)) {
            if (currentLayer) {
                throw std::runtime_error(
                    "Selector metadata must appear before '@layer 0': " + source);
            }
            definition.selectors.push_back(parseSelector(
                std::string_view(line).substr(selectorPrefix.size()),
                sourceName));
            continue;
        }

        if (line.starts_with(decorationPrefix)) {
            if (currentLayer) {
                throw std::runtime_error(
                    "Decoration metadata must appear before '@layer 0': " + source);
            }
            definition.decorations.push_back(parseDecoration(
                std::string_view(line).substr(decorationPrefix.size()),
                sourceName));
            continue;
        }

        if (line.starts_with(gatePrefix)) {
            if (currentLayer) {
                throw std::runtime_error(
                    "Gate metadata must appear before '@layer 0': " + source);
            }
            definition.gates.push_back(parseLinkedRecord<Gate>(
                std::string_view(line).substr(gatePrefix.size()),
                sourceName,
                "Gate"));
            continue;
        }

        if (line.starts_with(platePrefix)) {
            if (currentLayer) {
                throw std::runtime_error(
                    "Plate metadata must appear before '@layer 0': " + source);
            }
            definition.plates.push_back(parsePlate(
                std::string_view(line).substr(platePrefix.size()),
                sourceName));
            continue;
        }

        if (line.starts_with(rotatorPrefix)) {
            if (currentLayer) {
                throw std::runtime_error(
                    "Rotator metadata must appear before '@layer 0': " + source);
            }
            definition.rotators.push_back(parseLinkedRecord<Rotator>(
                std::string_view(line).substr(rotatorPrefix.size()),
                sourceName,
                "Rotator"));
            continue;
        }
        if (line.starts_with(lockPlatePrefix)) {
            if (currentLayer) {
                throw std::runtime_error(
                    "LockPlate metadata must appear before '@layer 0': " + source);
            }
            definition.lockPlates.push_back(parseLinkedRecord<LockPlate>(
                std::string_view(line).substr(lockPlatePrefix.size()),
                sourceName,
                "LockPlate"));
            continue;
        }

        if (line.starts_with(elevatorPrefix)) {
            if (currentLayer) {
                throw std::runtime_error(
                    "Elevator metadata must appear before '@layer 0': " + source);
            }
            definition.elevators.push_back(parseLinkedRecord<Elevator>(
                std::string_view(line).substr(elevatorPrefix.size()),
                sourceName,
                "Elevator"));
            continue;
        }

        if (line.starts_with(minecartPrefix)) {
            if (currentLayer) {
                throw std::runtime_error(
                    "Minecart metadata must appear before '@layer 0': " + source);
            }
            definition.minecarts.push_back(parseLinkedRecord<Minecart>(
                std::string_view(line).substr(minecartPrefix.size()),
                sourceName,
                "Minecart"));
            continue;
        }

        if (line.starts_with(portalPrefix)) {
            if (currentLayer) {
                throw std::runtime_error(
                    "Portal metadata must appear before '@layer 0': " + source);
            }
            definition.portals.push_back(parseObjectLink(
                std::string_view(line).substr(portalPrefix.size()),
                sourceName));
            continue;
        }

        if (line.starts_with(objectLinkPrefix)) {
            if (currentLayer) {
                throw std::runtime_error(
                    "Object link metadata must appear before '@layer 0': " + source);
            }
            definition.objectLinks.push_back(parseObjectLink(
                std::string_view(line).substr(objectLinkPrefix.size()),
                sourceName));
            continue;
        }

        if (line.starts_with(linkColorPrefix)) {
            if (currentLayer) {
                throw std::runtime_error(
                    "Link color metadata must appear before '@layer 0': " + source);
            }
            definition.linkColors.push_back(parseLinkColor(
                std::string_view(line).substr(linkColorPrefix.size()),
                sourceName));
            continue;
        }

        if (line.starts_with(waterPrefix)) {
            if (currentLayer) {
                throw std::runtime_error(
                    "Water metadata must appear before '@layer 0': " + source);
            }
            const std::optional<uint32_t> waterLayer =
                parseWaterHeader(line);
            if (!waterLayer) {
                throw std::runtime_error(
                    "Invalid water layer metadata; expected '@water N': " + source);
            }
            if (definition.waterLayer) {
                throw std::runtime_error(
                    "Level contains more than one '@water' directive: " + source);
            }
            definition.waterLayer = waterLayer;
            continue;
        }

        if (line.starts_with(layerPrefix)) {
            const std::optional<uint32_t> layer = parseLayerHeader(line);
            if (!layer || *layer != definition.layers.size()) {
                throw std::runtime_error(
                    "Layer headers must be sequential, starting with '@layer 0': " + source);
            }
            definition.layers.emplace_back();
            currentLayer = layer;
            continue;
        }

        if (!currentLayer) {
            if (line.empty()) {
                continue;
            }
            throw std::runtime_error("Level data appears before '@layer 0': " + source);
        }

        if (line.empty()) {
            continue;
        }
        definition.layers[*currentLayer].push_back(line);
    }

    if (definition.layers.empty()) {
        throw std::runtime_error("Level contains no layers: " + source);
    }
    if (std::ranges::any_of(
            definition.layers,
            [](const std::vector<std::string>& layer) {
                return layer.empty();
            })) {
        throw std::runtime_error("Every layer must contain at least one row: " + source);
    }
    if (definition.waterLayer &&
        *definition.waterLayer >= definition.layers.size()) {
        throw std::runtime_error(
            "Water layer must refer to an existing layer: " + source);
    }

    std::ranges::sort(
        definition.selectors, {}, &Level::ScreenSelector::id);
    for (size_t i = 0; i < definition.selectors.size(); ++i) {
        const ScreenSelector& selector = definition.selectors[i];
        validateSelectorRecord(selector, sourceName);
        if (i > 0 && definition.selectors[i - 1].id == selector.id) {
            throw std::runtime_error(
                "Level contains duplicate selector id " +
                std::to_string(selector.id) + ": " + source);
        }
        for (size_t j = 0; j < i; ++j) {
            if (definition.selectors[j].cell == selector.cell) {
                throw std::runtime_error(
                    "Level contains more than one selector at cell " +
                    std::to_string(selector.cell.x) + "," +
                    std::to_string(selector.cell.y) + "," +
                    std::to_string(selector.cell.z) + ": " + source);
            }
        }
    }

    // Accept files saved by the earlier metadata-based direction picker.
    // Directional grid tiles are authoritative; only the legacy T is converted.
    for (const auto& [cell, direction] : legacyLecternDirections) {
        if (cell.z < 0 || cell.y < 0 || cell.x < 0 ||
            static_cast<std::size_t>(cell.z) >= definition.layers.size() ||
            static_cast<std::size_t>(cell.y) >= definition.layers[cell.z].size() ||
            static_cast<std::size_t>(cell.x) >= definition.layers[cell.z][cell.y].size()) {
            throw std::runtime_error("Lectern metadata must reference a Lectern tile: " + source);
        }
        char& tile = definition.layers[cell.z][cell.y][cell.x];
        if (tile == tileTypeToChar(TileType::LecternSouth)) {
            tile = tileTypeToChar(direction);
        }
    }

    canonicalizeLinkedRecords(definition.gates, sourceName, "Gate");
    canonicalizeLinkedRecords(definition.rotators, sourceName, "Rotator");
    canonicalizeLinkedRecords(definition.lockPlates, sourceName, "LockPlate");
    canonicalizeLinkedRecords(definition.elevators, sourceName, "Elevator");
    canonicalizeLinkedRecords(definition.minecarts, sourceName, "Minecart");
    canonicalizeObjectLinks(definition.objectLinks, sourceName);
    canonicalizeObjectLinks(definition.portals, sourceName);
    canonicalizePlates(definition.plates, sourceName);
    canonicalizeLinkColors(definition.linkColors, sourceName);
    validateGroundSplats(definition, sourceName);

    return definition;
}

Level::LayerRows Level::parseLayerRows(
    const std::vector<std::string>& lines,
    std::string_view sourceName)
{
    return parseDefinition(lines, sourceName).layers;
}

std::vector<std::string> Level::serializeDefinition(
    const Definition& definition)
{
    validateGroundSplats(definition, "serialized level");
    if (definition.layers.size() == 1 && !definition.character &&
        !definition.waterLayer && !definition.cameraAngles &&
        definition.decorations.empty() &&
        definition.selectors.empty() && definition.lecterns.empty() && definition.gates.empty() &&
        definition.rotators.empty() && definition.lockPlates.empty() &&
        definition.elevators.empty() &&
        definition.minecarts.empty() && definition.objectLinks.empty() &&
        definition.portals.empty() && definition.linkColors.empty() &&
        definition.plates.empty() && definition.groundSplats.empty() &&
        definition.groundPaint.empty()) {
        return definition.layers.front();
    }

    std::vector<std::string> lines;
    for (const GroundSplat& splat : definition.groundSplats) {
        const Json object {
            { "name", splat.name }, { "base", splat.base },
            { "detail", splat.detail }, { "mask", splat.mask },
            { "color", { splat.color.x, splat.color.y, splat.color.z } },
        };
        lines.push_back(std::string(groundSplatPrefix) + object.dump());
    }
    std::vector<GroundPaint> paint = definition.groundPaint;
    std::ranges::sort(paint, [](const GroundPaint& a, const GroundPaint& b) {
        if (a.cell.z != b.cell.z) return a.cell.z < b.cell.z;
        if (a.cell.y != b.cell.y) return a.cell.y < b.cell.y;
        return a.cell.x < b.cell.x;
    });
    for (const GroundPaint& tile : paint) {
        const Json object {
            { "cell", { tile.cell.x, tile.cell.y, tile.cell.z } },
            { "splat", tile.splat },
        };
        lines.push_back(std::string(groundPaintPrefix) + object.dump());
    }
    if (definition.cameraAngles) {
        validateCameraAngles(*definition.cameraAngles, "serialized level");
        const Json object {
            { "pitch", definition.cameraAngles->pitchDegrees },
            { "yaw", definition.cameraAngles->yawDegrees },
        };
        lines.push_back(std::string(cameraPrefix) + object.dump());
    }
    if (definition.character) {
        lines.push_back(
            std::string(characterPrefix) +
            std::string(characterTypeName(*definition.character)));
    }
    if (definition.waterLayer) {
        lines.push_back(
            std::string(waterPrefix) +
            std::to_string(*definition.waterLayer));
    }
    std::vector<Lectern> lecterns = definition.lecterns;
    std::ranges::sort(lecterns, {}, [](const Lectern& lectern) {
        return std::array { lectern.cell.z, lectern.cell.y, lectern.cell.x };
    });
    for (const Lectern& lectern : lecterns) {
        const Json object {
            { "cell", { lectern.cell.x, lectern.cell.y, lectern.cell.z } },
            { "text", lectern.text },
        };
        lines.push_back(std::string(lecternPrefix) + object.dump());
    }
    std::vector<ScreenSelector> selectors = definition.selectors;
    std::ranges::sort(selectors, {}, &ScreenSelector::id);
    for (const ScreenSelector& selector : selectors) {
        lines.push_back(serializeSelector(selector));
    }
    std::vector<Gate> gates = definition.gates;
    std::ranges::sort(gates, {}, [](const Gate& gate) {
        return std::array { gate.cell.z, gate.cell.y, gate.cell.x };
    });
    for (const Gate& gate : gates) {
        lines.push_back(serializeLinkedRecord(gate, gatePrefix, "Gate"));
    }
    std::vector<Rotator> rotators = definition.rotators;
    std::ranges::sort(rotators, {}, [](const Rotator& rotator) {
        return std::array { rotator.cell.z, rotator.cell.y, rotator.cell.x };
    });
    for (const Rotator& rotator : rotators) {
        lines.push_back(
            serializeLinkedRecord(rotator, rotatorPrefix, "Rotator"));
    }
    std::vector<LockPlate> lockPlates = definition.lockPlates;
    std::ranges::sort(lockPlates, {}, [](const LockPlate& lockPlate) {
        return std::array { lockPlate.cell.z, lockPlate.cell.y, lockPlate.cell.x };
    });
    for (const LockPlate& lockPlate : lockPlates) {
        lines.push_back(
            serializeLinkedRecord(lockPlate, lockPlatePrefix, "LockPlate"));
    }
    std::vector<Elevator> elevators = definition.elevators;
    std::ranges::sort(elevators, {}, [](const Elevator& elevator) {
        return std::array { elevator.cell.z, elevator.cell.y, elevator.cell.x };
    });
    for (const Elevator& elevator : elevators) {
        lines.push_back(
            serializeLinkedRecord(elevator, elevatorPrefix, "Elevator"));
    }
    std::vector<Minecart> minecarts = definition.minecarts;
    std::ranges::sort(minecarts, {}, [](const Minecart& minecart) {
        return std::array { minecart.cell.z, minecart.cell.y, minecart.cell.x };
    });
    for (const Minecart& minecart : minecarts) {
        lines.push_back(
            serializeLinkedRecord(minecart, minecartPrefix, "Minecart"));
    }
    std::vector<Portal> portals = definition.portals;
    canonicalizeObjectLinks(portals, "serialized level");
    for (const Portal& portal : portals) {
        const Json object {
            { "cell", { portal.cell.x, portal.cell.y, portal.cell.z } },
            { "color", { portal.color.x, portal.color.y, portal.color.z } },
        };
        lines.push_back(std::string(portalPrefix) + object.dump());
    }
    std::vector<ObjectLink> objectLinks = definition.objectLinks;
    canonicalizeObjectLinks(objectLinks, "serialized level");
    for (const ObjectLink& objectLink : objectLinks) {
        lines.push_back(serializeObjectLink(objectLink));
    }
    std::vector<Plate> plates = definition.plates;
    std::ranges::sort(plates, {}, [](const Plate& plate) {
        return std::array { plate.cell.z, plate.cell.y, plate.cell.x };
    });
    for (const Plate& plate : plates) {
        lines.push_back(serializePlate(plate));
    }
    std::vector<LinkColor> linkColors = definition.linkColors;
    canonicalizeLinkColors(linkColors, "serialized level");
    for (const LinkColor& record : linkColors) {
        lines.push_back(serializeLinkColor(record));
    }
    for (const Decoration& decoration : definition.decorations) {
        lines.push_back(serializeDecoration(decoration));
    }
    if (definition.character || definition.waterLayer ||
        definition.cameraAngles ||
        !definition.decorations.empty() || !definition.selectors.empty() ||
        !definition.lecterns.empty() ||
        !definition.gates.empty() || !definition.rotators.empty() ||
        !definition.lockPlates.empty() ||
        !definition.elevators.empty() || !definition.minecarts.empty() ||
        !definition.objectLinks.empty() || !definition.portals.empty() ||
        !definition.linkColors.empty() || !definition.plates.empty() ||
        !definition.groundSplats.empty() || !definition.groundPaint.empty()) {
        lines.emplace_back();
    }
    for (size_t layer = 0; layer < definition.layers.size(); ++layer) {
        if (layer > 0) {
            lines.emplace_back();
        }
        lines.push_back(std::string(layerPrefix) + std::to_string(layer));
        lines.insert(
            lines.end(),
            definition.layers[layer].begin(),
            definition.layers[layer].end());
    }
    return lines;
}

std::vector<std::string> Level::serializeLayerRows(const LayerRows& layers)
{
    return serializeDefinition({ .layers = layers });
}

Level Level::loadFromLines(const std::vector<std::string>& lines, std::string_view sourceName)
{
    return loadFromDefinition(parseDefinition(lines, sourceName), sourceName);
}

Level Level::loadFromDefinition(
    const Definition& definition,
    std::string_view sourceName)
{
    if (definition.cameraAngles) {
        validateCameraAngles(*definition.cameraAngles, sourceName);
    }
    Level level = loadFromLayers(
        definition.layers,
        sourceName,
        definition.waterLayer,
        definition.decorations,
        definition.selectors,
        definition.gates,
        definition.rotators,
        definition.plates,
        definition.character.value_or(CharacterType::Rogue),
        definition.elevators,
        definition.minecarts,
        definition.objectLinks,
        definition.portals,
        definition.lockPlates,
        definition.lecterns);
    level.cameraAngles_ = definition.cameraAngles;
    validateGroundSplats(definition, sourceName);
    level.groundSplats_ = definition.groundSplats;
    level.groundPaint_ = definition.groundPaint;
    return level;
}

const Level::GroundSplat* Level::groundSplatAt(
    const std::vector<GroundSplat>& splats,
    const std::vector<GroundPaint>& paint, GridPosition3 cell)
{
    if (splats.empty()) return nullptr;
    const auto assigned = std::ranges::find(paint, cell, &GroundPaint::cell);
    if (assigned == paint.end()) return &splats.front();
    const auto found = std::ranges::find(splats, assigned->splat, &GroundSplat::name);
    return found == splats.end() ? &splats.front() : &*found;
}

void Level::validateGroundSplats(const Definition& definition, std::string_view sourceName)
{
    const auto fail = [&](const char* message) {
        throw std::runtime_error(std::string(message) + ": " + std::string(sourceName));
    };
    const auto colorKey = [](Vec3 color) {
        return std::array<int, 3> { static_cast<int>(std::lround(color.x * 255.0f)),
            static_cast<int>(std::lround(color.y * 255.0f)),
            static_cast<int>(std::lround(color.z * 255.0f)) };
    };
    for (std::size_t i = 0; i < definition.groundSplats.size(); ++i) {
        const GroundSplat& splat = definition.groundSplats[i];
        if (splat.name.empty() || splat.base.empty() || splat.detail.empty() || splat.mask.empty())
            fail("Ground splat names and texture names must not be empty");
        for (float component : { splat.color.x, splat.color.y, splat.color.z }) {
            if (!std::isfinite(component) || component < 0 || component > 1)
                fail("Ground splat colors must be finite and in [0, 1]");
        }
        for (std::size_t j = 0; j < i; ++j) {
            if (definition.groundSplats[j].name == splat.name)
                fail("Ground splat names must be unique within a screen");
            if (colorKey(definition.groundSplats[j].color) == colorKey(splat.color))
                fail("Ground splat colors must be unique within a screen");
        }
    }
    for (std::size_t i = 0; i < definition.groundPaint.size(); ++i) {
        const GroundPaint& paint = definition.groundPaint[i];
        if (std::ranges::find(definition.groundSplats, paint.splat, &GroundSplat::name) == definition.groundSplats.end())
            fail("Ground paint references an unknown splat map");
        const GridPosition3 cell = paint.cell;
        if (cell.x < 0 || cell.y < 0 || cell.z < 0 ||
            static_cast<std::size_t>(cell.z) >= definition.layers.size() ||
            static_cast<std::size_t>(cell.y) >= definition.layers[static_cast<std::size_t>(cell.z)].size() ||
            static_cast<std::size_t>(cell.x) >= definition.layers[static_cast<std::size_t>(cell.z)][static_cast<std::size_t>(cell.y)].size() ||
            !tileTypeHasSplatTop(charToTileType(definition.layers[static_cast<std::size_t>(cell.z)][static_cast<std::size_t>(cell.y)][static_cast<std::size_t>(cell.x)]).value_or(TileType::Air)))
            fail("Ground paint must reference a ground or cliff tile inside the screen");
        for (std::size_t j = 0; j < i; ++j) {
            if (definition.groundPaint[j].cell == cell) fail("Duplicate ground paint cell");
        }
    }
}

Level Level::loadFromLayers(
    const LayerRows& sourceLayers,
    std::string_view sourceName,
    std::optional<uint32_t> waterLayer,
    const std::vector<Decoration>& decorations,
    const std::vector<ScreenSelector>& selectors,
    const std::vector<Gate>& gates,
    const std::vector<Rotator>& rotators,
    const std::vector<Plate>& plates,
    CharacterType selectedCharacter,
    const std::vector<Elevator>& elevators,
    const std::vector<Minecart>& minecarts,
    const std::vector<ObjectLink>& objectLinks,
    const std::vector<Portal>& portals,
    const std::vector<LockPlate>& lockPlates,
    const std::vector<Lectern>& lecterns)
{
    const std::string source(sourceName);
    if (sourceLayers.empty()) {
        throw std::runtime_error("Level contains no layers: " + source);
    }

    Level level;
    level.character_ = selectedCharacter;
    level.depth_ = static_cast<uint32_t>(sourceLayers.size());
    if (waterLayer && *waterLayer >= level.depth_) {
        throw std::runtime_error(
            "Water layer must refer to an existing layer: " + source);
    }
    level.waterLayer_ = waterLayer;
    for (const Decoration& decoration : decorations) {
        validateDecoration(decoration, sourceName);
    }
    level.decorations_ = decorations;
    level.selectors_ = selectors;
    std::ranges::sort(level.selectors_, {}, &ScreenSelector::id);
    for (size_t i = 0; i < level.selectors_.size(); ++i) {
        validateSelectorRecord(level.selectors_[i], sourceName);
        if (i > 0 &&
            level.selectors_[i - 1].id == level.selectors_[i].id) {
            throw std::runtime_error(
                "Level contains duplicate selector id " +
                std::to_string(level.selectors_[i].id) + ": " + source);
        }
        for (size_t j = 0; j < i; ++j) {
            if (level.selectors_[j].cell == level.selectors_[i].cell) {
                throw std::runtime_error(
                    "Level contains more than one selector at the same cell: " +
                    source);
            }
        }
    }
    level.gates_ = gates;
    canonicalizeLinkedRecords(level.gates_, sourceName, "Gate");
    level.rotators_ = rotators;
    canonicalizeLinkedRecords(level.rotators_, sourceName, "Rotator");
    level.lockPlates_ = lockPlates;
    canonicalizeLinkedRecords(level.lockPlates_, sourceName, "LockPlate");
    level.elevators_ = elevators;
    canonicalizeLinkedRecords(level.elevators_, sourceName, "Elevator");
    level.minecarts_ = minecarts;
    canonicalizeLinkedRecords(level.minecarts_, sourceName, "Minecart");
    level.portals_ = portals;
    canonicalizeObjectLinks(level.portals_, sourceName);
    level.objectLinks_ = objectLinks;
    canonicalizeObjectLinks(level.objectLinks_, sourceName);
    level.coveredPlates_ = plates;
    canonicalizePlates(level.coveredPlates_, sourceName);
    for (const auto& layer : sourceLayers) {
        level.height_ = std::max(level.height_, static_cast<uint32_t>(layer.size()));
        for (const std::string& row : layer) {
            level.width_ = std::max(level.width_, static_cast<uint32_t>(row.size()));
        }
    }

    if (level.width_ == 0 || level.height_ == 0) {
        throw std::runtime_error("Level has no tiles: " + source);
    }

    level.tiles_.assign(
        static_cast<size_t>(level.width_) * level.height_ * level.depth_,
        TileType::Air);
    std::size_t matchedCoveredPlates = 0;

    for (uint32_t z = 0; z < level.depth_; ++z) {
        const auto& layer = sourceLayers[z];
        for (uint32_t y = 0; y < static_cast<uint32_t>(layer.size()); ++y) {
            for (uint32_t x = 0; x < static_cast<uint32_t>(layer[y].size()); ++x) {
                const char character = layer[y][x];
                const GridPosition3 position {
                    static_cast<int>(x),
                    static_cast<int>(y),
                    static_cast<int>(z),
                };

                const std::optional<TileType> tile = charToTileType(character);
                if (!tile) {
                    throw unknownLevelCharacter(character);
                }

                if (tileTypeIsPlayerStart(*tile)) {
                    const CharacterType heroCharacter = *tile == TileType::Knight
                        ? CharacterType::Knight
                        : *tile == TileType::Druid
                            ? CharacterType::Druid
                            : *tile == TileType::Witch
                                ? CharacterType::Witch
                                : *tile == TileType::Bard
                                    ? CharacterType::Bard
                                    : *tile == TileType::Rogue
                                        ? CharacterType::Rogue
                                        : selectedCharacter;
                    level.playerStarts_.push_back({ position, heroCharacter });
                    if (level.playerStarts_.size() == 1) {
                        level.playerStart_ = position;
                        level.character_ = heroCharacter;
                    }
                }

                if (const std::optional<CharacterType> wardrobeCharacter =
                        wardrobeCharacterForTile(*tile)) {
                    level.wardrobes_.push_back({
                        .cell = position,
                        .character = *wardrobeCharacter,
                    });
                }

                if (tileTypeIsMovableObject(*tile)) {
                    level.movableTiles_.push_back({
                        .type = *tile,
                        .position = position,
                    });
                }

                if (*tile == TileType::Enemy) {
                    level.enemyStarts_.push_back(position);
                }

                // Movable units leave their covered plate in the static grid.
                const auto covered = std::ranges::find(
                    level.coveredPlates_, position, &Plate::cell);
                if (covered != level.coveredPlates_.end()) {
                    if (!tileTypeCanCoverSurface(*tile, covered->tile)) {
                        throw std::runtime_error(
                            "A '@plate' record must lie beneath a unit or mirror (or a minecart or minecart gate) compatible with its surface, not '" +
                            std::string(tileTypeName(*tile)) + "': " + source);
                    }
                    ++matchedCoveredPlates;
                }
                const TileType plate =
                    covered != level.coveredPlates_.end()
                    ? covered->tile
                    : (tileTypeIsPlate(*tile)
                            ? *tile
                            : TileType::Air);
                level.tiles_[tileIndex(x, y, z, level.width_, level.height_)] =
                    tileTypeOccupiesLevelCell(*tile)
                    ? plate
                    : *tile;
                if (tileTypeIsSignalSource(plate)) {
                    level.pressurePlates_.push_back(position);
                }
                if (plate == TileType::End) {
                    level.ends_.push_back(position);
                }
            }
        }
    }

    if (matchedCoveredPlates != level.coveredPlates_.size()) {
        throw std::runtime_error(
            "A '@plate' record must lie beneath a unit or mirror (or a minecart or minecart gate): " + source);
    }

    for (const Portal& portal : level.portals_) {
        if (!tileTypeIsPortal(
                level.plateAt(portal.cell).value_or(TileType::Air))) {
            throw std::runtime_error(
                "Portal metadata cell must contain a Portal tile: " + source);
        }
    }

    for (const ObjectLink& link : level.objectLinks_) {
        const auto movable = std::ranges::find(
            level.movableTiles_, link.cell, &MovableTile::position);
        if (movable == level.movableTiles_.end()) {
            throw std::runtime_error(
                "Object link metadata cell must contain a movable object: " +
                source);
        }
    }

    if (level.playerStarts_.empty()) {
        throw std::runtime_error(
            "Level is missing a hero start tile ('" +
            std::string(1, tileTypeToChar(TileType::Rogue)) + "' or '" +
            std::string(1, tileTypeToChar(TileType::Knight)) + "' or '" +
            std::string(1, tileTypeToChar(TileType::Druid)) + "' or '" +
            std::string(1, tileTypeToChar(TileType::Witch)) + "' or '" +
            std::string(1, tileTypeToChar(TileType::Bard)) + "'): " + source);
    }

    for (const Gate& gate : level.gates_) {
        if (!level.inBounds(gate.cell) ||
            level.authoredTileAt(
                static_cast<uint32_t>(gate.cell.x),
                static_cast<uint32_t>(gate.cell.y),
                static_cast<uint32_t>(gate.cell.z)) != TileType::Gate) {
            throw std::runtime_error(
                "Gate metadata cell must contain a Gate tile: " + source);
        }
        for (GridPosition3 plate : gate.pressurePlates) {
            if (!tileTypeIsSignalSource(level.plateAt(plate).value_or(TileType::Air))) {
                throw std::runtime_error(
                    "Gate links must refer to Pressure or Button tiles: " + source);
            }
        }
    }
    for (const Rotator& rotator : level.rotators_) {
        if (!tileTypeIsRotator(
                level.plateAt(rotator.cell).value_or(TileType::Air))) {
            throw std::runtime_error(
                "Rotator metadata cell must contain a Rotator tile: " + source);
        }
        for (GridPosition3 plate : rotator.pressurePlates) {
            if (!tileTypeIsSignalSource(level.plateAt(plate).value_or(TileType::Air))) {
                throw std::runtime_error(
                    "Rotator links must refer to Pressure or Button tiles: " + source);
            }
        }
    }
    for (const LockPlate& lockPlate : level.lockPlates_) {
        if (!tileTypeIsLockPlate(
                level.plateAt(lockPlate.cell).value_or(TileType::Air))) {
            throw std::runtime_error(
                "LockPlate metadata cell must contain a LockPlate tile: " + source);
        }
        for (GridPosition3 plate : lockPlate.pressurePlates) {
            if (!tileTypeIsSignalSource(level.plateAt(plate).value_or(TileType::Air))) {
                throw std::runtime_error(
                    "LockPlate links must refer to Pressure or Button tiles: " + source);
            }
        }
    }
    for (const Elevator& elevator : level.elevators_) {
        if (!level.inBounds(elevator.cell) ||
            level.authoredTileAt(
                static_cast<uint32_t>(elevator.cell.x),
                static_cast<uint32_t>(elevator.cell.y),
                static_cast<uint32_t>(elevator.cell.z)) != TileType::Elevator) {
            throw std::runtime_error(
                "Elevator metadata cell must contain an Elevator tile: " + source);
        }
        for (const int stop : elevator.levels) {
            if (stop >= static_cast<int>(level.depth_)) {
                throw std::runtime_error(
                    "Elevator stop levels must refer to existing layers: " +
                    source);
            }
        }
        for (GridPosition3 plate : elevator.pressurePlates) {
            if (!tileTypeIsSignalSource(level.plateAt(plate).value_or(TileType::Air))) {
                throw std::runtime_error(
                    "Elevator links must refer to Pressure or Button tiles: " + source);
            }
        }
    }
    level.minecartRoutes_.reserve(level.minecarts_.size());
    for (const Minecart& minecart : level.minecarts_) {
        if (!level.inBounds(minecart.cell) ||
            level.authoredTileAt(
                static_cast<uint32_t>(minecart.cell.x),
                static_cast<uint32_t>(minecart.cell.y),
                static_cast<uint32_t>(minecart.cell.z)) != TileType::Minecart) {
            throw std::runtime_error(
                "Minecart metadata cell must contain a Minecart tile: " + source);
        }
        const TileType stop = level.plateAt(minecart.cell).value_or(TileType::Air);
        if (!tileTypeIsRailStop(stop)) {
            throw std::runtime_error(
                "A Minecart must be authored on top of a Rail Stop: " + source);
        }
        if ((railConnectionMask(stop) &
                railDirectionBit(minecart.initialDirection)) == 0) {
            throw std::runtime_error(
                "Minecart initial direction must follow its starting Rail Stop: " +
                source);
        }
        for (GridPosition3 plate : minecart.pressurePlates) {
            if (!tileTypeIsSignalSource(level.plateAt(plate).value_or(TileType::Air))) {
                throw std::runtime_error(
                    "Minecart links must refer to Pressure or Button tiles: " + source);
            }
        }
        level.minecartRoutes_.push_back(
            buildMinecartRoute(minecart,
                [&](GridPosition3 cell) { return railTileAt(level, cell); }, sourceName));
    }
    for (uint32_t z = 0; z < level.depth_; ++z) {
        for (uint32_t y = 0; y < level.height_; ++y) {
            for (uint32_t x = 0; x < level.width_; ++x) {
                const GridPosition3 cell {
                    static_cast<int>(x),
                    static_cast<int>(y),
                    static_cast<int>(z),
                };
                const TileType authored = level.authoredTileAt(x, y, z);
                if (authored == TileType::Elevator &&
                    level.elevatorAt(cell) == nullptr) {
                    throw std::runtime_error(
                        "Every Elevator tile requires an '@elevator' metadata record: " +
                        source);
                }
                if (authored == TileType::Minecart &&
                    level.minecartAt(cell) == nullptr) {
                    throw std::runtime_error(
                        "Every Minecart tile requires an '@minecart' metadata record: " +
                        source);
                }
                if (authored == TileType::Gate &&
                    level.gateAt(cell) == nullptr) {
                    throw std::runtime_error(
                        "Every Gate tile requires an '@gate' metadata record: " +
                        source);
                }
                if (tileTypeIsRotator(level.plateAt(cell).value_or(authored)) &&
                    level.rotatorAt(cell) == nullptr) {
                    throw std::runtime_error(
                        "Every Rotator tile requires an '@rotator' metadata record: " +
                        source);
                }
                if (tileTypeIsLockPlate(level.plateAt(cell).value_or(authored)) &&
                    level.lockPlateAt(cell) == nullptr) {
                    throw std::runtime_error(
                        "Every LockPlate tile requires an '@lockplate' metadata record: " +
                        source);
                }
            }
        }
    }

    level.lecterns_ = lecterns;
    for (std::size_t i = 0; i < level.lecterns_.size(); ++i) {
        const Lectern& lectern = level.lecterns_[i];
        if (!level.inBounds(lectern.cell) ||
            !tileTypeIsLectern(level.authoredTileAt(static_cast<uint32_t>(lectern.cell.x),
                static_cast<uint32_t>(lectern.cell.y),
                static_cast<uint32_t>(lectern.cell.z)))) {
            throw std::runtime_error("Lectern metadata must reference a Lectern tile: " + source);
        }
        for (std::size_t j = 0; j < i; ++j) {
            if (level.lecterns_[j].cell == lectern.cell) {
                throw std::runtime_error("Duplicate lectern cell: " + source);
            }
        }
    }
    // Unconfigured stands still read as a blank book while being authored.
    for (uint32_t z = 0; z < level.depth_; ++z) {
        for (uint32_t y = 0; y < level.height_; ++y) {
            for (uint32_t x = 0; x < level.width_; ++x) {
                const GridPosition3 cell { static_cast<int>(x), static_cast<int>(y), static_cast<int>(z) };
                if (tileTypeIsLectern(level.authoredTileAt(x, y, z)) && !level.lecternAt(cell)) {
                    level.lecterns_.push_back({ .cell = cell, .text = {} });
                }
            }
        }
    }

    for (const ScreenSelector& selector : level.selectors_) {
        if (!level.isWalkable(selector.cell)) {
            throw std::runtime_error(
                "Selector " + std::to_string(selector.id) +
                " must be placed on a supported walkable cell: " + source);
        }
    }

    for (uint32_t z = 0; z < level.depth_; ++z) {
        for (uint32_t y = 0; y < level.height_; ++y) {
            for (uint32_t x = 0; x < level.width_; ++x) {
                if (level.tileAt(x, y, z) != TileType::Ladder) {
                    continue;
                }
                const GridPosition3 position {
                    static_cast<int>(x),
                    static_cast<int>(y),
                    static_cast<int>(z),
                };
                if (!hasAdjacentGround(level, position)) {
                    throw std::runtime_error(
                        "Ladder tile 'L' must be next to a ground tile on the same layer: " + source);
                }
            }
        }
    }

    return level;
}

TileType Level::tileAt(uint32_t x, uint32_t y, uint32_t z) const
{
    const TileType authored = authoredTileAt(x, y, z);
    if (authored == TileType::Air &&
        waterLayer_ &&
        z == *waterLayer_) {
        return TileType::Water;
    }
    return authored;
}

TileType Level::authoredTileAt(uint32_t x, uint32_t y, uint32_t z) const
{
    return tiles_[tileIndex(x, y, z, width_, height_)];
}

std::optional<TileType> Level::supportingTileAt(GridPosition3 position) const
{
    const GridPosition3 support {
        position.x,
        position.y,
        position.z - 1,
    };
    if (!inBounds(support)) {
        return std::nullopt;
    }

    return tileAt(
        static_cast<uint32_t>(support.x),
        static_cast<uint32_t>(support.y),
        static_cast<uint32_t>(support.z));
}

bool Level::inBounds(GridPosition3 position) const
{
    return position.x >= 0 &&
        position.y >= 0 &&
        position.z >= 0 &&
        position.x < static_cast<int>(width_) &&
        position.y < static_cast<int>(height_) &&
        position.z < static_cast<int>(depth_);
}

bool Level::isWalkable(GridPosition3 position) const
{
    if (!inBounds(position)) {
        return false;
    }

    const TileType tile = tileAt(
        static_cast<uint32_t>(position.x),
        static_cast<uint32_t>(position.y),
        static_cast<uint32_t>(position.z));
    const std::optional<TileType> support = supportingTileAt(position);
    return tileTypeAllowsEntity(tile) &&
        support &&
        tileTypeSupportsEntity(*support);
}

bool Level::isEnd(GridPosition3 position) const
{
    if (!inBounds(position)) {
        return false;
    }

    return tileAt(
        static_cast<uint32_t>(position.x),
        static_cast<uint32_t>(position.y),
        static_cast<uint32_t>(position.z)) == TileType::End;
}

const Level::Lectern* Level::lecternAt(GridPosition3 cell) const
{
    const auto found = std::ranges::find(lecterns_, cell, &Lectern::cell);
    return found == lecterns_.end() ? nullptr : &*found;
}

const Level::ScreenSelector* Level::selectorAt(GridPosition3 cell) const
{
    const auto found = std::ranges::find(selectors_, cell, &ScreenSelector::cell);
    return found == selectors_.end() ? nullptr : &*found;
}

const Level::Wardrobe* Level::wardrobeAt(GridPosition3 cell) const
{
    const auto found = std::ranges::find(wardrobes_, cell, &Wardrobe::cell);
    return found == wardrobes_.end() ? nullptr : &*found;
}

const Level::Gate* Level::gateAt(GridPosition3 cell) const
{
    const auto found = std::ranges::find(gates_, cell, &Gate::cell);
    return found == gates_.end() ? nullptr : &*found;
}

const Level::Gate* Level::gateForPressurePlate(GridPosition3 cell) const
{
    const auto found = std::ranges::find_if(gates_, [cell](const Gate& gate) {
        return std::ranges::find(gate.pressurePlates, cell) !=
            gate.pressurePlates.end();
    });
    return found == gates_.end() ? nullptr : &*found;
}

const Level::Rotator* Level::rotatorAt(GridPosition3 cell) const
{
    const auto found = std::ranges::find(rotators_, cell, &Rotator::cell);
    return found == rotators_.end() ? nullptr : &*found;
}

const Level::LockPlate* Level::lockPlateAt(GridPosition3 cell) const
{
    const auto found = std::ranges::find(lockPlates_, cell, &LockPlate::cell);
    return found == lockPlates_.end() ? nullptr : &*found;
}

const Level::Rotator* Level::rotatorForPressurePlate(GridPosition3 cell) const
{
    const auto found = std::ranges::find_if(
        rotators_,
        [cell](const Rotator& rotator) {
            return std::ranges::find(rotator.pressurePlates, cell) !=
                rotator.pressurePlates.end();
        });
    return found == rotators_.end() ? nullptr : &*found;
}

const Level::LockPlate* Level::lockPlateForPressurePlate(GridPosition3 cell) const
{
    const auto found = std::ranges::find_if(
        lockPlates_,
        [cell](const LockPlate& lockPlate) {
            return std::ranges::find(lockPlate.pressurePlates, cell) !=
                lockPlate.pressurePlates.end();
        });
    return found == lockPlates_.end() ? nullptr : &*found;
}

std::size_t Level::Elevator::startStop() const
{
    const auto found = std::ranges::find(levels, cell.z);
    return found == levels.end()
        ? 0
        : static_cast<std::size_t>(std::distance(levels.begin(), found));
}

const Level::Elevator* Level::elevatorAt(GridPosition3 cell) const
{
    const auto found = std::ranges::find(elevators_, cell, &Elevator::cell);
    return found == elevators_.end() ? nullptr : &*found;
}

const Level::Elevator* Level::elevatorForPressurePlate(GridPosition3 cell) const
{
    const auto found = std::ranges::find_if(
        elevators_,
        [cell](const Elevator& elevator) {
            return std::ranges::find(elevator.pressurePlates, cell) !=
                elevator.pressurePlates.end();
        });
    return found == elevators_.end() ? nullptr : &*found;
}

const Level::Minecart* Level::minecartAt(GridPosition3 cell) const
{
    const auto found = std::ranges::find(minecarts_, cell, &Minecart::cell);
    return found == minecarts_.end() ? nullptr : &*found;
}

const Level::Minecart* Level::minecartForPressurePlate(GridPosition3 cell) const
{
    const auto found = std::ranges::find_if(
        minecarts_,
        [cell](const Minecart& minecart) {
            return std::ranges::find(minecart.pressurePlates, cell) !=
                minecart.pressurePlates.end();
        });
    return found == minecarts_.end() ? nullptr : &*found;
}

std::optional<TileType> Level::plateAt(GridPosition3 cell) const
{
    if (!inBounds(cell)) {
        return std::nullopt;
    }
    const auto covered = std::ranges::find(coveredPlates_, cell, &Plate::cell);
    if (covered != coveredPlates_.end()) {
        return covered->tile;
    }
    const TileType tile = authoredTileAt(
        static_cast<uint32_t>(cell.x),
        static_cast<uint32_t>(cell.y),
        static_cast<uint32_t>(cell.z));
    return tileTypeIsPlate(tile) ? std::optional<TileType>(tile) : std::nullopt;
}

std::optional<Vec3> Level::pressurePlateLinkColor(GridPosition3 cell) const
{
    if (const Gate* gate = gateForPressurePlate(cell)) {
        return gate->color;
    }
    if (const Rotator* rotator = rotatorForPressurePlate(cell)) {
        return rotator->color;
    }
    if (const LockPlate* lockPlate = lockPlateForPressurePlate(cell)) {
        return lockPlate->color;
    }
    if (const Elevator* elevator = elevatorForPressurePlate(cell)) {
        return elevator->color;
    }
    if (const Minecart* minecart = minecartForPressurePlate(cell)) {
        return minecart->color;
    }
    return std::nullopt;
}

const Level::Portal* Level::portalAt(GridPosition3 cell) const
{
    const auto found = std::ranges::find(portals_, cell, &Portal::cell);
    return found == portals_.end() ? nullptr : &*found;
}

std::optional<GridPosition3> Level::portalExit(GridPosition3 cell) const
{
    const Portal* entrance = portalAt(cell);
    if (!entrance) {
        return std::nullopt;
    }
    const Portal* exit = nullptr;
    for (const Portal& candidate : portals_) {
        if (candidate.cell == cell || candidate.scope != entrance->scope ||
            objectLinkColorKey(candidate.color) !=
                objectLinkColorKey(entrance->color)) {
            continue;
        }
        if (exit) {
            return std::nullopt;
        }
        exit = &candidate;
    }
    return exit ? std::optional<GridPosition3>(exit->cell) : std::nullopt;
}

std::optional<Level::PortalCrossing> Level::portalCrossing(
    GridPosition3 from,
    GridPosition direction) const
{
    const TileType entrance = plateAt(from).value_or(TileType::Air);
    if (!tileTypeIsPortal(entrance) ||
        portalEdgeOffset(entrance) != direction) {
        return std::nullopt;
    }
    const auto exit = portalExit(from);
    if (!exit || !tileTypeIsPortal(plateAt(*exit).value_or(TileType::Air))) {
        return std::nullopt;
    }
    const GridPosition edge = portalEdgeOffset(*plateAt(*exit));
    const GridPosition outgoing { -edge.x, -edge.y };
    GridPosition rotated = direction;
    int turns = 0;
    while (rotated != outgoing) {
        rotated = { -rotated.y, rotated.x };
        ++turns;
    }
    return PortalCrossing { *exit, outgoing, turns };
}

std::optional<Vec3> Level::movableLinkColor(std::size_t movableIndex) const
{
    if (movableIndex >= movableTiles_.size()) {
        return std::nullopt;
    }
    const auto found = std::ranges::find(
        objectLinks_, movableTiles_[movableIndex].position, &ObjectLink::cell);
    return found == objectLinks_.end()
        ? std::nullopt
        : std::optional<Vec3>(found->color);
}

bool Level::movablesAreLinked(std::size_t left, std::size_t right) const
{
    if (left >= movableTiles_.size() || right >= movableTiles_.size()) {
        return false;
    }
    const auto linkFor = [&](std::size_t index) {
        return std::ranges::find(
            objectLinks_, movableTiles_[index].position, &ObjectLink::cell);
    };
    const auto leftLink = linkFor(left);
    const auto rightLink = linkFor(right);
    return leftLink != objectLinks_.end() && rightLink != objectLinks_.end() &&
        leftLink->scope == rightLink->scope &&
        objectLinkColorKey(leftLink->color) ==
            objectLinkColorKey(rightLink->color);
}

} // namespace sokoban

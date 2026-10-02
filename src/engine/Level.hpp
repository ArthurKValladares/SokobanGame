#pragma once

#include "engine/Character.hpp"
#include "engine/Math.hpp"
#include "engine/LevelLocation.hpp"
#include "engine/TileTypes.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sokoban {

class Level {
public:
    using LayerRows = std::vector<std::vector<std::string>>;

    struct ScreenSelector {
        uint32_t id = 0;
        GridPosition3 cell {};
        std::optional<LevelLocation> target;

        bool operator==(const ScreenSelector&) const = default;
    };

    struct Gate {
        GridPosition3 cell {};
        std::vector<GridPosition3> pressurePlates;
        Vec3 color { 1.0f, 0.72f, 0.12f };

        bool operator==(const Gate&) const = default;
    };

    // A Rotator tile's authored links. Direction comes from the tile type;
    // the record says which pressure plates activate it and which color the
    // plate and its linked pressure plates share. Linking follows the gate
    // rule: the rotator activates when every linked plate becomes occupied.
    struct Rotator {
        GridPosition3 cell {};
        std::vector<GridPosition3> pressurePlates;
        Vec3 color { 0.24f, 0.62f, 0.92f };

        bool operator==(const Rotator&) const = default;
    };

    // An Elevator tile's authored record. The tile marks the platform's
    // starting cell; `levels` lists the layers the platform stops at, in
    // travel order, and always includes the starting layer. Activation follows
    // the rotator rule (it fires once each time every linked pressure plate
    // becomes occupied) and moves the platform one stop along the list,
    // turning back at either end: [0, 3, 5, 7] travels
    // 0 -> 3 -> 5 -> 7 -> 5 -> 3 -> 0 -> 3 ... A platform resting at layer L
    // is a solid block in cell (x, y, L), its top flush with the other blocks
    // of that layer, and it carries whatever stands on it.
    struct Elevator {
        GridPosition3 cell {};
        std::vector<GridPosition3> pressurePlates;
        Vec3 color { 0.38f, 0.80f, 0.44f };
        std::vector<int> levels;

        // Index of the starting layer (cell.z) in `levels`.
        [[nodiscard]] std::size_t startStop() const;

        bool operator==(const Elevator&) const = default;
    };

    // A minecart's authored start and pressure links. `initialDirection` is
    // 0/1/2/3 for north/east/south/west and chooses which side of its start
    // stop the active route follows. Open tracks ping-pong on that side;
    // closed tracks keep circling in that orientation.
    struct Minecart {
        GridPosition3 cell {};
        std::vector<GridPosition3> pressurePlates;
        Vec3 color { 0.86f, 0.54f, 0.18f };
        uint8_t initialDirection = 0;

        bool operator==(const Minecart&) const = default;
    };

    // Runtime route derived from the oriented rail tiles. Cells are ordered
    // from the cart's start along its selected direction; stops are a subset
    // in that same order and always begin with the starting stop.
    struct MinecartRoute {
        std::vector<GridPosition3> cells;
        std::vector<GridPosition3> stops;
        std::vector<std::size_t> stopCellIndices;
        bool loop = false;

        bool operator==(const MinecartRoute&) const = default;
    };

    // A plate (see TileProperty::Plate) authored underneath something already
    // standing on it: the layer grid holds the occupant, this record the
    // plate. Plates with nothing on them stay in the grid as usual.
    struct Plate {
        GridPosition3 cell {};
        TileType tile = TileType::PressurePlate;

        bool operator==(const Plate&) const = default;
    };

    // Level-editor bookkeeping, never read by gameplay. In the editor a
    // pressure plate drives every gate, rotator and elevator of its color;
    // saving turns those color groups into the explicit `pressurePlates`
    // lists above, which are what gameplay uses. Linked plates take their
    // color back from their device when a screen is loaded, so only a
    // pressure plate that drives nothing yet needs its color written down,
    // here, to survive a save.
    struct LinkColor {
        GridPosition3 cell {};
        Vec3 color {};

        bool operator==(const LinkColor& other) const
        {
            return cell == other.cell && color.x == other.color.x &&
                color.y == other.color.y && color.z == other.color.z;
        }
    };

    struct Decoration {
        struct PointLight {
            // Offset in the decoration's local space. It is scaled and
            // rotated with the mesh, so the light remains attached while the
            // decoration is transformed in the editor.
            Vec3 offset { 0.0f, 0.0f, 1.0f };
            Vec3 color { 1.0f, 0.85f, 0.65f };
            float intensity = 2.0f;
            float range = 5.0f;
            bool castsShadows = true;
            // Minimum receiver offset in world units. Shaders increase it
            // automatically on surfaces viewed at a grazing light angle.
            float shadowBias = 0.01f;
            float shadowOpacity = 1.0f;

            bool operator==(const PointLight& other) const
            {
                return offset.x == other.offset.x &&
                    offset.y == other.offset.y &&
                    offset.z == other.offset.z &&
                    color.x == other.color.x &&
                    color.y == other.color.y &&
                    color.z == other.color.z &&
                    intensity == other.intensity &&
                    range == other.range &&
                    castsShadows == other.castsShadows &&
                    shadowBias == other.shadowBias &&
                    shadowOpacity == other.shadowOpacity;
            }
        };

        // Stable manifest model name. Screen files never embed source asset
        // paths or renderer ids, so manifest reordering cannot corrupt them.
        std::string model;
        // World-space pivot. A freshly placed decoration uses the centre of
        // the supporting tile at its top surface.
        Vec3 position {};
        // Euler angles in degrees, applied X then Y then Z.
        Vec3 rotationDegrees {};
        Vec3 scale { 1.0f, 1.0f, 1.0f };
        std::optional<PointLight> pointLight;

        bool operator==(const Decoration& other) const
        {
            return model == other.model &&
                position.x == other.position.x &&
                position.y == other.position.y &&
                position.z == other.position.z &&
                rotationDegrees.x == other.rotationDegrees.x &&
                rotationDegrees.y == other.rotationDegrees.y &&
                rotationDegrees.z == other.rotationDegrees.z &&
                scale.x == other.scale.x &&
                scale.y == other.scale.y &&
                scale.z == other.scale.z &&
                pointLight == other.pointLight;
        }
    };

    struct Definition {
        LayerRows layers;
        std::optional<uint32_t> waterLayer;
        std::vector<Decoration> decorations;
        std::vector<ScreenSelector> selectors;
        std::vector<Gate> gates;
        std::vector<Rotator> rotators;
        std::vector<Plate> plates;
        std::vector<Elevator> elevators;
        std::vector<Minecart> minecarts;
        // Editor-only (see LinkColor); Level::loadFromDefinition ignores it.
        std::vector<LinkColor> linkColors;
        // Missing only for backwards-compatible legacy documents. Runtime
        // levels always resolve it to Rogue.
        std::optional<CharacterType> character;

        bool operator==(const Definition&) const = default;
    };

    struct MovableTile {
        TileType type = TileType::Rock;
        GridPosition3 position {};
    };

    struct PlayerStart {
        GridPosition3 position {};
        CharacterType character = CharacterType::Rogue;

        bool operator==(const PlayerStart&) const = default;
    };

    static Level loadFromFile(const std::filesystem::path& path);
    [[nodiscard]] static Definition loadDefinitionFromFile(
        const std::filesystem::path& path);
    static Level loadFromLines(const std::vector<std::string>& lines, std::string_view sourceName);
    static Level loadFromDefinition(const Definition& definition, std::string_view sourceName);
    static Level loadFromLayers(
        const LayerRows& layers,
        std::string_view sourceName,
        std::optional<uint32_t> waterLayer = std::nullopt,
        const std::vector<Decoration>& decorations = {},
        const std::vector<ScreenSelector>& selectors = {},
        const std::vector<Gate>& gates = {},
        const std::vector<Rotator>& rotators = {},
        const std::vector<Plate>& plates = {},
        CharacterType selectedCharacter = CharacterType::Rogue,
        const std::vector<Elevator>& elevators = {},
        const std::vector<Minecart>& minecarts = {});
    [[nodiscard]] static Definition parseDefinition(
        const std::vector<std::string>& lines,
        std::string_view sourceName);
    [[nodiscard]] static std::vector<std::string> serializeDefinition(
        const Definition& definition);
    [[nodiscard]] static LayerRows parseLayerRows(const std::vector<std::string>& lines, std::string_view sourceName);
    [[nodiscard]] static std::vector<std::string> serializeLayerRows(const LayerRows& layers);

    [[nodiscard]] uint32_t width() const { return width_; }
    [[nodiscard]] uint32_t height() const { return height_; }
    [[nodiscard]] uint32_t depth() const { return depth_; }
    [[nodiscard]] GridPosition3 playerStart() const { return playerStart_; }
    [[nodiscard]] CharacterType character() const { return character_; }
    [[nodiscard]] const std::vector<PlayerStart>& playerStarts() const
    {
        return playerStarts_;
    }
    [[nodiscard]] const std::vector<MovableTile>& movableTiles() const { return movableTiles_; }
    [[nodiscard]] const std::vector<GridPosition3>& enemyStarts() const { return enemyStarts_; }
    [[nodiscard]] const std::vector<GridPosition3>& pressurePlates() const { return pressurePlates_; }
    [[nodiscard]] const std::vector<Gate>& gates() const { return gates_; }
    [[nodiscard]] const Gate* gateAt(GridPosition3 cell) const;
    [[nodiscard]] const Gate* gateForPressurePlate(GridPosition3 cell) const;
    [[nodiscard]] const std::vector<Rotator>& rotators() const { return rotators_; }
    [[nodiscard]] const Rotator* rotatorAt(GridPosition3 cell) const;
    [[nodiscard]] const Rotator* rotatorForPressurePlate(GridPosition3 cell) const;
    [[nodiscard]] const std::vector<Elevator>& elevators() const { return elevators_; }
    // The elevator authored at `cell` (its starting cell), if any.
    [[nodiscard]] const Elevator* elevatorAt(GridPosition3 cell) const;
    [[nodiscard]] const Elevator* elevatorForPressurePlate(GridPosition3 cell) const;
    [[nodiscard]] const std::vector<Minecart>& minecarts() const { return minecarts_; }
    [[nodiscard]] const std::vector<MinecartRoute>& minecartRoutes() const
    {
        return minecartRoutes_;
    }
    [[nodiscard]] const Minecart* minecartAt(GridPosition3 cell) const;
    [[nodiscard]] const Minecart* minecartForPressurePlate(GridPosition3 cell) const;
    // The color a pressure plate takes from the gate, rotator or elevator it
    // drives, in that order of precedence. Empty for unlinked plates.
    [[nodiscard]] std::optional<Vec3> pressurePlateLinkColor(GridPosition3 cell) const;
    // The plate at `cell`, whether it is uncovered or has something authored
    // on top of it. Units standing on a plate leave it in tileAt(); a mirror
    // on a plate replaces it there, so rules that ask about plates use this.
    [[nodiscard]] std::optional<TileType> plateAt(GridPosition3 cell) const;
    // Every `@plate` record (plates authored beneath an occupant).
    [[nodiscard]] const std::vector<Plate>& coveredPlates() const { return coveredPlates_; }
    [[nodiscard]] const std::vector<GridPosition3>& ends() const { return ends_; }
    [[nodiscard]] std::optional<uint32_t> waterLayer() const { return waterLayer_; }
    [[nodiscard]] const std::vector<Decoration>& decorations() const { return decorations_; }
    [[nodiscard]] const std::vector<ScreenSelector>& selectors() const { return selectors_; }
    [[nodiscard]] const ScreenSelector* selectorAt(GridPosition3 cell) const;
    [[nodiscard]] TileType authoredTileAt(uint32_t x, uint32_t y, uint32_t z = 0) const;
    [[nodiscard]] TileType tileAt(uint32_t x, uint32_t y, uint32_t z = 0) const;
    [[nodiscard]] std::optional<TileType> supportingTileAt(GridPosition3 position) const;
    [[nodiscard]] bool inBounds(GridPosition3 position) const;
    [[nodiscard]] bool isWalkable(GridPosition3 position) const;
    [[nodiscard]] bool isEnd(GridPosition3 position) const;

private:
    uint32_t width_ = 0;
    uint32_t height_ = 0;
    uint32_t depth_ = 0;
    GridPosition3 playerStart_ {};
    CharacterType character_ = CharacterType::Rogue;
    std::vector<PlayerStart> playerStarts_;
    std::vector<MovableTile> movableTiles_;
    std::vector<GridPosition3> enemyStarts_;
    std::vector<GridPosition3> pressurePlates_;
    std::vector<Gate> gates_;
    std::vector<Rotator> rotators_;
    std::vector<Elevator> elevators_;
    std::vector<Minecart> minecarts_;
    std::vector<MinecartRoute> minecartRoutes_;
    std::vector<Plate> coveredPlates_;
    std::vector<GridPosition3> ends_;
    std::vector<TileType> tiles_;
    std::optional<uint32_t> waterLayer_;
    std::vector<Decoration> decorations_;
    std::vector<ScreenSelector> selectors_;
};

} // namespace sokoban

#include "engine/MakerDraftCodec.hpp"

#include <algorithm>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace sokoban::makerDraftCodec {
namespace {

template <typename Integer>
Integer draftInteger(const nlohmann::json& value)
{
    if (value.is_number_unsigned()) {
        const auto number = value.get<uint64_t>();
        if (std::in_range<Integer>(number)) return static_cast<Integer>(number);
    } else if (value.is_number_integer()) {
        const auto number = value.get<int64_t>();
        if (std::in_range<Integer>(number)) return static_cast<Integer>(number);
    }
    throw std::runtime_error("The overworld draft contains an invalid integer.");
}

} // namespace

void validateDraftGrid(const Level::Definition& definition)
{
    if (definition.layers.empty()) throw std::runtime_error("The draft has no board.");
    for (const auto& layer : definition.layers) {
        if (layer.empty()) throw std::runtime_error("The draft contains an empty layer.");
        for (const auto& row : layer) {
            if (row.empty()) throw std::runtime_error("The draft contains an empty row.");
            if (std::ranges::any_of(row, [](char tile) { return !charToTileType(tile); })) {
                throw std::runtime_error("The draft contains an unrecognized tile.");
            }
        }
    }
}

nlohmann::json encodeOverworld(const OverworldDraftOverride& draft)
{
    OverworldMap::validateDraft(draft);
    std::unordered_map<OverworldScreenId, OverworldSlot> slots;
    slots.reserve(draft.layout->screens.size());
    for (const auto& screen : draft.layout->screens) slots.emplace(screen.id, screen.slot);

    nlohmann::json screens = nlohmann::json::array();
    for (const auto& component : draft.definitions) {
        const auto slot = slots.at(component.screen);
        screens.push_back({
            { "id", component.screen },
            { "slot", { slot.x, slot.y } },
            { "screen", Level::serializeDefinition(component.definition) },
        });
    }
    return {
        { "format", 1 },
        { "width", draft.layout->screenWidth },
        { "height", draft.layout->screenHeight },
        { "screens", std::move(screens) },
    };
}

OverworldDraftOverride decodeOverworld(const nlohmann::json& encoded, std::string_view name)
{
    if (draftInteger<int>(encoded.at("format")) != 1) {
        throw std::runtime_error("Unsupported overworld draft format.");
    }
    OverworldDraftOverride draft;
    auto& layout = draft.layout.emplace();
    layout.screenWidth = draftInteger<uint32_t>(encoded.at("width"));
    layout.screenHeight = draftInteger<uint32_t>(encoded.at("height"));
    const auto& screens = encoded.at("screens");
    if (!screens.is_array() ||
        screens.size() > static_cast<std::size_t>(OverworldLayout::maxSlotSpan * OverworldLayout::maxSlotSpan)) {
        throw std::runtime_error("The overworld draft has an invalid number of screens.");
    }
    for (const auto& screen : screens) {
        const auto id = draftInteger<OverworldScreenId>(screen.at("id"));
        const auto& slot = screen.at("slot");
        if (!slot.is_array() || slot.size() != 2) {
            throw std::runtime_error("The overworld draft has an invalid screen slot.");
        }
        layout.screens.push_back({
            .id = id,
            .file = "screen" + std::to_string(id) + ".scr",
            .slot = { draftInteger<int>(slot[0]), draftInteger<int>(slot[1]) },
        });
        draft.definitions.push_back({
            .screen = id,
            .definition = Level::parseDefinition(
                screen.at("screen").get<std::vector<std::string>>(), name),
        });
    }
    OverworldMap::validateDraft(draft);
    return draft;
}

} // namespace sokoban::makerDraftCodec

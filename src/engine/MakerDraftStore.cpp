#include "engine/MakerDraftStore.hpp"

#include "engine/AtomicFile.hpp"
#include "engine/FileLock.hpp"
#include "engine/PathText.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <optional>
#include <utility>

namespace sokoban {
namespace {

namespace fs = std::filesystem;

constexpr std::string_view prefix = "draft-";
constexpr std::string_view draftSuffix = ".draft";
constexpr std::string_view overworldSuffix = ".world";
constexpr std::string_view hexDigits = "0123456789abcdef";

std::string_view nameError(std::string_view name)
{
    if (name.empty() || name.find_first_not_of(' ') == std::string_view::npos) {
        return "Enter a draft name.";
    }
    if (name.size() > MakerDraftStore::maxNameBytes) {
        return "The draft name is too long.";
    }
    if (name.front() == ' ' || name.back() == ' ') {
        return "Remove spaces from the beginning and end of the draft name.";
    }
    if (name == "." || name == ".." || name.find_first_of("/\\") != std::string_view::npos ||
        std::ranges::any_of(name, [](unsigned char byte) { return byte < 32 || byte == 127; })) {
        return "The draft name contains an unsupported character.";
    }
    try {
        // The JSON encoder checks that every input byte is valid UTF-8.
        (void)nlohmann::json(std::string(name)).dump();
    } catch (const nlohmann::json::type_error&) {
        return "The draft name must be valid UTF-8 text.";
    }
    return {};
}

std::string draftFilename(std::string_view name, std::string_view suffix)
{
    if (const auto error = nameError(name); !error.empty()) {
        throw std::runtime_error(std::string(error));
    }
    // Encode bytes so each name is one portable path component. Filesystem
    // case folding and Windows device names cannot alias another draft.
    std::string filename(prefix);
    filename.reserve(prefix.size() + name.size() * 2 + suffix.size());
    for (unsigned char byte : name) {
        filename += hexDigits[byte >> 4];
        filename += hexDigits[byte & 15];
    }
    filename += suffix;
    return filename;
}

std::optional<std::string> draftName(std::string_view filename, std::string_view suffix)
{
    if (!filename.starts_with(prefix) || !filename.ends_with(suffix)) {
        return std::nullopt;
    }
    filename.remove_prefix(prefix.size());
    filename.remove_suffix(suffix.size());
    if (filename.size() > MakerDraftStore::maxNameBytes * 2 || filename.size() % 2 != 0) {
        return std::nullopt;
    }
    std::string name;
    name.reserve(filename.size() / 2);
    for (std::size_t offset = 0; offset < filename.size(); offset += 2) {
        const auto high = hexDigits.find(filename[offset]);
        const auto low = hexDigits.find(filename[offset + 1]);
        if (high == std::string_view::npos || low == std::string_view::npos) {
            return std::nullopt;
        }
        name += static_cast<char>(high * 16 + low);
    }
    return nameError(name).empty() ? std::optional(std::move(name)) : std::nullopt;
}

bool regularFileExists(const fs::path& path)
{
    const auto status = fs::symlink_status(path);
    if (!fs::exists(status)) {
        return false;
    }
    if (!fs::is_regular_file(status)) {
        throw std::runtime_error("The draft path is occupied by a non-draft file.");
    }
    return true;
}

void validateDraftGrid(const Level::Definition& definition)
{
    if (definition.layers.empty()) {
        throw std::runtime_error("The draft has no board.");
    }
    for (const auto& layer : definition.layers) {
        if (layer.empty()) {
            throw std::runtime_error("The draft contains an empty layer.");
        }
        for (const auto& row : layer) {
            if (row.empty()) {
                throw std::runtime_error("The draft contains an empty row.");
            }
            if (std::ranges::any_of(row, [](char tile) { return !charToTileType(tile); })) {
                throw std::runtime_error("The draft contains an unrecognized tile.");
            }
        }
    }
}

std::vector<std::string> listDraftNames(const fs::path& root, std::string_view suffix)
{
    std::vector<std::string> names;
    if (!fs::exists(root)) {
        return names;
    }
    for (const auto& entry : fs::directory_iterator(root)) {
        std::error_code statusError;
        if (!fs::is_regular_file(entry.symlink_status(statusError))) {
            continue;
        }
        if (auto name = draftName(pathText(entry.path().filename()), suffix)) {
            names.push_back(std::move(*name));
        }
    }
    std::ranges::sort(names);
    return names;
}

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

MakerDraftStore::MakerDraftStore(std::filesystem::path root) : root_(std::move(root))
{
    if (root_.empty()) {
        throw std::invalid_argument("The Maker drafts directory is missing.");
    }
}

std::vector<std::string> MakerDraftStore::list() const
{
    return listDraftNames(root_, draftSuffix);
}

void MakerDraftStore::save(std::string_view name, const Level::Definition& definition,
    bool overwrite) const
{
    const fs::path path = root_ / draftFilename(name, draftSuffix);
    const FileLock lock(root_ / ".writer.lock", "Maker draft writer");
    if (regularFileExists(path) && !overwrite) {
        throw NameExists {};
    }

    validateDraftGrid(definition);
    std::string contents;
    for (const std::string& line : Level::serializeDefinition(definition)) {
        contents += line;
        contents += '\n';
    }
    atomicFile::write(path, contents);
}

Level::Definition MakerDraftStore::load(std::string_view name) const
{
    const fs::path path = root_ / draftFilename(name, draftSuffix);
    if (!regularFileExists(path)) {
        throw std::runtime_error("This draft is no longer available.");
    }
    auto definition = Level::loadDefinitionFromFile(path);
    validateDraftGrid(definition);
    return definition;
}

std::vector<std::string> MakerDraftStore::listOverworlds() const
{
    return listDraftNames(root_, overworldSuffix);
}

void MakerDraftStore::saveOverworld(std::string_view name, const OverworldDraftOverride& draft,
    bool overwrite) const
{
    const fs::path path = root_ / draftFilename(name, overworldSuffix);
    const FileLock lock(root_ / ".writer.lock", "Maker draft writer");
    if (regularFileExists(path) && !overwrite) throw NameExists {};

    OverworldMap::validateDraft(draft);
    nlohmann::json screens = nlohmann::json::array();
    for (const auto& component : draft.definitions) {
        const auto& spec = *std::ranges::find(draft.layout->screens, component.screen, &OverworldScreenSpec::id);
        screens.push_back({
            { "id", spec.id },
            { "slot", { spec.slot.x, spec.slot.y } },
            { "screen", Level::serializeDefinition(component.definition) },
        });
    }
    const nlohmann::json encoded {
        { "format", 1 },
        { "width", draft.layout->screenWidth },
        { "height", draft.layout->screenHeight },
        { "screens", std::move(screens) },
    };
    atomicFile::write(path, encoded.dump());
}

OverworldDraftOverride MakerDraftStore::loadOverworld(std::string_view name) const
{
    const fs::path path = root_ / draftFilename(name, overworldSuffix);
    if (!regularFileExists(path)) throw std::runtime_error("This overworld draft is no longer available.");
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("Could not read the overworld draft.");
    try {
        const auto encoded = nlohmann::json::parse(input);
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
    } catch (const nlohmann::json::exception&) {
        throw std::runtime_error("The overworld draft is damaged.");
    }
}

} // namespace sokoban

#include "engine/MakerDraftStore.hpp"

#include "engine/AtomicFile.hpp"
#include "engine/FileLock.hpp"
#include "engine/PathText.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <optional>
#include <utility>

namespace sokoban {
namespace {

namespace fs = std::filesystem;

constexpr std::string_view prefix = "draft-";
constexpr std::string_view suffix = ".draft";
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

std::string draftFilename(std::string_view name)
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

std::optional<std::string> draftName(std::string_view filename)
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

} // namespace

MakerDraftStore::MakerDraftStore(std::filesystem::path root) : root_(std::move(root))
{
    if (root_.empty()) {
        throw std::invalid_argument("The Maker drafts directory is missing.");
    }
}

std::vector<std::string> MakerDraftStore::list() const
{
    std::vector<std::string> names;
    if (!fs::exists(root_)) {
        return names;
    }
    for (const auto& entry : fs::directory_iterator(root_)) {
        std::error_code statusError;
        if (!fs::is_regular_file(entry.symlink_status(statusError))) {
            continue;
        }
        if (auto name = draftName(pathText(entry.path().filename()))) {
            names.push_back(std::move(*name));
        }
    }
    std::ranges::sort(names);
    return names;
}

void MakerDraftStore::save(std::string_view name, const Level::Definition& definition,
    bool overwrite) const
{
    const fs::path path = root_ / draftFilename(name);
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
    const fs::path path = root_ / draftFilename(name);
    if (!regularFileExists(path)) {
        throw std::runtime_error("This draft is no longer available.");
    }
    auto definition = Level::loadDefinitionFromFile(path);
    validateDraftGrid(definition);
    return definition;
}

} // namespace sokoban

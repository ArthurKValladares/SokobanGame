#include "engine/MakerDraftStore.hpp"

#include "engine/AtomicFile.hpp"
#include "engine/FileLock.hpp"
#include "engine/MakerDraftCodec.hpp"
#include "engine/PathText.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <fstream>
#include <iterator>
#include <optional>
#include <random>
#include <tuple>
#include <utility>

namespace sokoban {
namespace {

namespace fs = std::filesystem;

constexpr std::string_view prefix = "draft-";
constexpr std::string_view draftSuffix = ".draft";
constexpr std::string_view overworldSuffix = ".world";
constexpr std::string_view levelPrefix = "level-";
constexpr std::string_view levelSuffix = ".level";
constexpr std::string_view legacyIdPrefix = "legacy-";
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
    if (name == "." || name == ".." ||
        name.find_first_of("/\\") != std::string_view::npos ||
        std::ranges::any_of(name, [](unsigned char byte) {
            return byte < 32 || byte == 127;
        })) {
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

std::string hexName(std::string_view name)
{
    if (const auto error = nameError(name); !error.empty()) {
        throw std::runtime_error(std::string(error));
    }
    // Encode bytes so each name is one portable path component. Filesystem
    // case folding and Windows device names cannot alias another draft.
    std::string hex;
    hex.reserve(name.size() * 2);
    for (unsigned char byte : name) {
        hex += hexDigits[byte >> 4];
        hex += hexDigits[byte & 15];
    }
    return hex;
}

std::string draftFilename(std::string_view name, std::string_view suffix)
{
    auto filename = std::string(prefix) + hexName(name);
    filename += suffix;
    return filename;
}

std::optional<std::string> nameFromHex(std::string_view hex)
{
    if (hex.size() > MakerDraftStore::maxNameBytes * 2 || hex.size() % 2 != 0) {
        return std::nullopt;
    }
    std::string name;
    name.reserve(hex.size() / 2);
    for (std::size_t offset = 0; offset < hex.size(); offset += 2) {
        const auto high = hexDigits.find(hex[offset]);
        const auto low = hexDigits.find(hex[offset + 1]);
        if (high == std::string_view::npos || low == std::string_view::npos) {
            return std::nullopt;
        }
        name += static_cast<char>(high * 16 + low);
    }
    return nameError(name).empty() ? std::optional(std::move(name))
                                   : std::nullopt;
}

std::optional<std::string> draftName(
    std::string_view filename,
    std::string_view suffix)
{
    if (!filename.starts_with(prefix) || !filename.ends_with(suffix))
        return std::nullopt;
    filename.remove_prefix(prefix.size());
    filename.remove_suffix(suffix.size());
    return nameFromHex(filename);
}

std::string legacyId(std::string_view name)
{
    return std::string(legacyIdPrefix) + hexName(name);
}

std::optional<std::string> legacyName(std::string_view id)
{
    if (!id.starts_with(legacyIdPrefix)) return std::nullopt;
    return nameFromHex(id.substr(legacyIdPrefix.size()));
}

bool validLevelId(std::string_view id)
{
    constexpr std::size_t idLength = 32;
    if (id.starts_with(legacyIdPrefix)) return legacyName(id).has_value();
    return id.size() == idLength && std::ranges::all_of(id, [](char character) {
               return hexDigits.find(character) != std::string_view::npos;
           });
}

fs::path stableLevelPath(const fs::path& root, std::string_view id)
{
    auto filename = std::string(levelPrefix);
    filename += id;
    filename += levelSuffix;
    return root / filename;
}

std::string newLevelId(const fs::path& root)
{
    std::random_device random;
    std::uniform_int_distribution<unsigned> byte(0, 255);
    std::string id(32, '0');
    do {
        for (std::size_t position = 0; position < id.size(); position += 2) {
            const auto value = byte(random);
            id[position] = hexDigits[value >> 4];
            id[position + 1] = hexDigits[value & 15];
        }
    } while (fs::exists(fs::symlink_status(stableLevelPath(root, id))));
    return id;
}

bool regularFileExists(const fs::path& path)
{
    const auto status = fs::symlink_status(path);
    if (!fs::exists(status)) {
        return false;
    }
    if (!fs::is_regular_file(status)) {
        throw std::runtime_error(
            "The draft path is occupied by a non-draft file.");
    }
    return true;
}

std::vector<std::string> listDraftNames(
    const fs::path& root,
    std::string_view suffix)
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

std::optional<fs::path> levelPathForId(
    const fs::path& root,
    std::string_view id)
{
    if (!validLevelId(id)) return std::nullopt;
    auto path = stableLevelPath(root, id);
    std::error_code error;
    const auto status = fs::symlink_status(path, error);
    if (!error && fs::is_regular_file(status)) return path;
    if (status.type() != fs::file_type::not_found) return std::nullopt;

    if (const auto oldName = legacyName(id)) {
        path = root / draftFilename(*oldName, draftSuffix);
        if (fs::is_regular_file(fs::symlink_status(path, error)) && !error)
            return path;
    }
    return std::nullopt;
}

struct StableLevelHeader {
    MakerDraftStore::SavedLevel level;
    bool deleted = false;
};

std::optional<StableLevelHeader> readStableLevel(
    std::istream& stream,
    std::string_view id)
{
    // Metadata occupies a fixed-size first line. Catalog operations do not
    // need to read or parse the board, even for large drafts.
    std::array<char, MakerDraftStore::maxNameBytes * 4 + 64> line {};
    if (!stream.getline(line.data(), static_cast<std::streamsize>(line.size())))
        return std::nullopt;
    const auto header = nlohmann::json::parse(line.data(), nullptr, false);
    if (!header.is_object() || !header.contains("format") ||
        !header.at("format").is_number_integer() || header.at("format") != 1 ||
        !header.contains("name") || !header.at("name").is_string()) {
        return std::nullopt;
    }
    auto name = header.at("name").get<std::string>();
    if (!nameError(name).empty()) return std::nullopt;
    auto alias = legacyName(id);
    if (!alias && header.contains("legacyName")) {
        if (!header.at("legacyName").is_string()) return std::nullopt;
        alias = header.at("legacyName").get<std::string>();
        if (!nameError(*alias).empty()) return std::nullopt;
    }
    if (header.contains("deleted") && !header.at("deleted").is_boolean())
        return std::nullopt;
    return StableLevelHeader {
        { std::string(id), std::move(name), std::move(alias) },
        header.value("deleted", false),
    };
}

std::optional<StableLevelHeader> readStableLevel(
    const fs::path& path,
    std::string_view id)
{
    std::ifstream input(path, std::ios::binary);
    return readStableLevel(input, id);
}

std::string readBinaryFile(const fs::path& path)
{
    std::ifstream input(path, std::ios::binary);
    std::string content { std::istreambuf_iterator<char>(input),
                          std::istreambuf_iterator<char>() };
    if (input.bad())
        throw std::runtime_error("Could not read the saved level.");
    return content;
}

nlohmann::json levelHeader(const MakerDraftStore::SavedLevel& level)
{
    nlohmann::json header { { "format", 1 }, { "name", level.name } };
    if (level.legacyName && !level.id.starts_with(legacyIdPrefix))
        header["legacyName"] = *level.legacyName;
    return header;
}

void writeSavedLevel(
    const fs::path& root,
    const MakerDraftStore::SavedLevel& level,
    const Level::Definition& definition)
{
    const auto path = stableLevelPath(root, level.id);
    (void)regularFileExists(path);
    makerDraftCodec::validateDraftGrid(definition);
    auto content = levelHeader(level).dump();
    content += '\n';
    for (const auto& line : Level::serializeDefinition(definition)) {
        content += line;
        content += '\n';
    }
    atomicFile::write(path, content);
}

} // namespace

MakerDraftStore::MakerDraftStore(std::filesystem::path root)
    : root_(std::move(root))
{
    if (root_.empty()) {
        throw std::invalid_argument("The Maker drafts directory is missing.");
    }
}

std::vector<std::string> MakerDraftStore::list() const
{
    std::vector<std::string> names;
    for (auto& level : listLevels())
        names.push_back(std::move(level.name));
    return names;
}

std::vector<MakerDraftStore::SavedLevel> MakerDraftStore::listLevels(
    std::vector<std::string>* retiredNames) const
{
    std::vector<SavedLevel> levels;
    if (retiredNames) retiredNames->clear();
    if (!fs::exists(root_)) return levels;
    for (const auto& entry : fs::directory_iterator(root_)) {
        const auto filename = pathText(entry.path().filename());
        std::string_view id(filename);
        if (!id.starts_with(levelPrefix) || !id.ends_with(levelSuffix))
            continue;
        id.remove_prefix(levelPrefix.size());
        id.remove_suffix(levelSuffix.size());
        std::error_code error;
        if (!validLevelId(id) ||
            !fs::is_regular_file(entry.symlink_status(error)) || error)
            continue;
        if (auto record = readStableLevel(entry.path(), id)) {
            if (!record->deleted) {
                levels.push_back(std::move(record->level));
            } else if (retiredNames) {
                retiredNames->push_back(std::move(record->level.name));
                if (record->level.legacyName)
                    retiredNames->push_back(
                        std::move(*record->level.legacyName));
            }
        }
    }
    for (const auto& name : listDraftNames(root_, draftSuffix)) {
        auto id = legacyId(name);
        const auto stable = stableLevelPath(root_, id);
        // Leave older .draft files intact. Once their canonical record exists
        // they are backups, not additional entries in the Maker library.
        if (fs::symlink_status(stable).type() == fs::file_type::not_found) {
            levels.push_back({ std::move(id), name, name });
        }
    }
    std::ranges::sort(
        levels, [](const SavedLevel& first, const SavedLevel& second) {
            return std::tie(first.name, first.id) <
                std::tie(second.name, second.id);
        });
    // A manually copied file may duplicate a name. Keep the picker unambiguous.
    const auto duplicates = std::ranges::unique(levels, {}, &SavedLevel::name);
    levels.erase(duplicates.begin(), duplicates.end());
    if (retiredNames) {
        std::ranges::sort(*retiredNames);
        const auto duplicateNames = std::ranges::unique(*retiredNames);
        retiredNames->erase(duplicateNames.begin(), duplicateNames.end());
    }
    return levels;
}

std::optional<MakerDraftStore::SavedLevel> MakerDraftStore::findLevelByName(
    std::string_view name) const
{
    if (!nameError(name).empty()) return std::nullopt;
    auto levels = listLevels();
    const auto found = std::ranges::find(levels, name, &SavedLevel::name);
    return found == levels.end() ? std::nullopt
                                 : std::optional(std::move(*found));
}

std::optional<MakerDraftStore::SavedLevel> MakerDraftStore::findLevelById(
    std::string_view id) const
{
    const auto path = levelPathForId(root_, id);
    if (!path) return std::nullopt;
    if (path->extension() == levelSuffix) {
        auto record = readStableLevel(*path, id);
        if (!record || record->deleted) return std::nullopt;
        return std::move(record->level);
    }
    const auto name = legacyName(id);
    return SavedLevel { std::string(id), *name, name };
}

std::optional<MakerDraftStore::SavedLevel> MakerDraftStore::resolveLevel(
    std::optional<std::string_view> id,
    std::optional<std::string_view> legacy) const
{
    if (id) return findLevelById(*id);
    if (!legacy || !nameError(*legacy).empty()) return std::nullopt;
    const auto originalId = legacyId(*legacy);
    if (auto existing = findLevelById(originalId)) return existing;
    if (fs::exists(fs::symlink_status(stableLevelPath(root_, originalId))))
        return std::nullopt;
    std::vector<std::string> retiredNames;
    auto levels = listLevels(&retiredNames);
    const auto alias =
        std::ranges::find_if(levels, [&](const SavedLevel& level) {
            return level.legacyName && *level.legacyName == *legacy;
        });
    if (alias != levels.end()) return std::move(*alias);
    if (std::ranges::binary_search(retiredNames, *legacy)) return std::nullopt;
    const auto current = std::ranges::find(levels, *legacy, &SavedLevel::name);
    return current == levels.end() ? std::nullopt
                                   : std::optional(std::move(*current));
}

MakerDraftStore::SavedLevel MakerDraftStore::save(
    std::string_view name,
    const Level::Definition& definition,
    bool overwrite) const
{
    const fs::path path = root_ / draftFilename(name, draftSuffix);
    const FileLock lock(root_ / ".writer.lock", "Maker draft writer");
    (void)regularFileExists(path);
    auto existing = findLevelByName(name);
    if (existing && !overwrite) throw NameExists {};
    auto level = existing
        ? std::move(*existing)
        : SavedLevel { newLevelId(root_), std::string(name), {} };
    writeSavedLevel(root_, level, definition);
    return level;
}

MakerDraftStore::SavedLevel MakerDraftStore::saveById(
    std::string_view id,
    const Level::Definition& definition) const
{
    const FileLock lock(root_ / ".writer.lock", "Maker draft writer");
    auto level = findLevelById(id);
    if (!level) throw std::runtime_error("This draft is no longer available.");
    writeSavedLevel(root_, *level, definition);
    return std::move(*level);
}

MakerDraftStore::SavedLevel MakerDraftStore::renameLevel(
    std::string_view id,
    std::string_view name) const
{
    const fs::path legacyPath = root_ / draftFilename(name, draftSuffix);
    const FileLock lock(root_ / ".writer.lock", "Maker draft writer");
    auto level = findLevelById(id);
    if (!level) throw std::runtime_error("This draft is no longer available.");
    if (level->name != name) {
        (void)regularFileExists(legacyPath);
        if (findLevelByName(name)) throw NameExists {};
        const auto path = levelPathForId(root_, id);
        if (!path)
            throw std::runtime_error("This draft is no longer available.");
        if (!level->legacyName) {
            const auto oldTarget = resolveLevel(std::nullopt, level->name);
            if (oldTarget && oldTarget->id == id)
                level->legacyName = level->name;
        }
        level->name = name;
        if (path->extension() == levelSuffix) {
            // The name lives only in the header line. Replace that line and
            // copy the board bytes, so a rename cannot alter board data.
            const auto content = readBinaryFile(*path);
            const auto headerEnd = content.find('\n');
            if (headerEnd == std::string::npos) {
                throw std::runtime_error(
                    "The saved level metadata is damaged.");
            }
            auto renamed = levelHeader(*level).dump();
            renamed.append(content, headerEnd);
            atomicFile::write(*path, renamed);
        } else {
            // A legacy draft has no header; renaming promotes it to a stable
            // level file, which needs the parsed board.
            writeSavedLevel(
                root_, *level, Level::loadDefinitionFromFile(*path));
        }
    }
    return std::move(*level);
}

void MakerDraftStore::deleteLevel(std::string_view id) const
{
    const FileLock lock(root_ / ".writer.lock", "Maker draft writer");
    const auto level = findLevelById(id);
    if (!level) throw std::runtime_error("This draft is no longer available.");

    const auto path = stableLevelPath(root_, id);
    (void)regularFileExists(path);
    auto header = levelHeader(*level);
    header["deleted"] = true;
    // Retire the ID and remove the board in one write. A promoted draft's
    // old file remains hidden even if cleanup is interrupted.
    atomicFile::write(path, header.dump() + '\n');
    if (const auto originalName = legacyName(id)) {
        const auto original = root_ / draftFilename(*originalName, draftSuffix);
        std::error_code error;
        if (fs::is_regular_file(fs::symlink_status(original, error)) &&
            !error) {
            fs::remove(original, error);
        }
    }
}

Level::Definition MakerDraftStore::load(std::string_view name) const
{
    const auto level = findLevelByName(name);
    if (!level) throw std::runtime_error("This draft is no longer available.");
    return loadById(level->id);
}

Level::Definition MakerDraftStore::loadById(std::string_view id) const
{
    const auto path = levelPathForId(root_, id);
    if (!path) throw std::runtime_error("This draft is no longer available.");
    Level::Definition definition;
    if (path->extension() == levelSuffix) {
        std::ifstream input(*path, std::ios::binary);
        const auto record = readStableLevel(input, id);
        if (!record)
            throw std::runtime_error("The saved level metadata is damaged.");
        if (record->deleted)
            throw std::runtime_error("This draft is no longer available.");
        std::vector<std::string> lines;
        std::string line;
        while (std::getline(input, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            lines.push_back(line);
        }
        if (input.bad())
            throw std::runtime_error("Could not read the saved level.");
        definition = Level::parseDefinition(lines, record->level.name);
    } else {
        definition = Level::loadDefinitionFromFile(*path);
    }
    makerDraftCodec::validateDraftGrid(definition);
    return definition;
}

std::optional<MakerDraftStore::DraftVersion> MakerDraftStore::draftVersion(
    std::string_view name) const
{
    const auto level = findLevelByName(name);
    return level ? draftVersionById(level->id) : std::nullopt;
}

std::optional<MakerDraftStore::DraftVersion> MakerDraftStore::draftVersionById(
    std::string_view id) const
{
    const auto path = levelPathForId(root_, id);
    if (!path) return std::nullopt;
    if (path->extension() == levelSuffix) {
        const auto record = readStableLevel(*path, id);
        if (!record || record->deleted) return std::nullopt;
    }
    std::error_code error;
    const fs::directory_entry draft(*path, error);
    if (error) return std::nullopt;
    const auto status = draft.symlink_status(error);
    if (error || !fs::is_regular_file(status)) return std::nullopt;

    const auto modified = draft.last_write_time(error);
    if (error) return std::nullopt;
    const auto bytes = draft.file_size(error);
    if (error) return std::nullopt;

    return DraftVersion { .modified = modified, .bytes = bytes };
}

std::vector<std::string> MakerDraftStore::listOverworlds() const
{
    return listDraftNames(root_, overworldSuffix);
}

void MakerDraftStore::saveOverworld(
    std::string_view name,
    const OverworldDraftOverride& draft,
    bool overwrite) const
{
    const fs::path path = root_ / draftFilename(name, overworldSuffix);
    const FileLock lock(root_ / ".writer.lock", "Maker draft writer");
    if (regularFileExists(path) && !overwrite) throw NameExists {};

    atomicFile::write(path, makerDraftCodec::encodeOverworld(draft).dump());
}

void MakerDraftStore::renameOverworld(
    std::string_view oldName,
    std::string_view newName) const
{
    const auto source = root_ / draftFilename(oldName, overworldSuffix);
    const auto destination = root_ / draftFilename(newName, overworldSuffix);
    const FileLock lock(root_ / ".writer.lock", "Maker draft writer");
    if (!regularFileExists(source))
        throw std::runtime_error(
            "This overworld draft is no longer available.");
    if (source == destination) return;
    if (regularFileExists(destination)) throw NameExists {};
    atomicFile::replace(destination, source);
}

void MakerDraftStore::deleteOverworld(std::string_view name) const
{
    const auto path = root_ / draftFilename(name, overworldSuffix);
    const FileLock lock(root_ / ".writer.lock", "Maker draft writer");
    if (!regularFileExists(path) || !fs::remove(path)) {
        throw std::runtime_error(
            "This overworld draft is no longer available.");
    }
}

OverworldDraftOverride MakerDraftStore::loadOverworld(
    std::string_view name) const
{
    const fs::path path = root_ / draftFilename(name, overworldSuffix);
    if (!regularFileExists(path))
        throw std::runtime_error(
            "This overworld draft is no longer available.");
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("Could not read the overworld draft.");
    try {
        return makerDraftCodec::decodeOverworld(
            nlohmann::json::parse(input), name);
    } catch (const nlohmann::json::exception&) {
        throw std::runtime_error("The overworld draft is damaged.");
    }
}

} // namespace sokoban

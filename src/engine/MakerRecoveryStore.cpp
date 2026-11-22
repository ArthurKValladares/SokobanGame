#include "engine/MakerRecoveryStore.hpp"

#include "engine/AtomicFile.hpp"
#include "engine/FileLock.hpp"
#include "engine/MakerDraftCodec.hpp"
#include "engine/MakerDraftStore.hpp"

#include <nlohmann/json.hpp>

#include <fstream>
#include <stdexcept>
#include <utility>

namespace sokoban {
namespace {

namespace fs = std::filesystem;

constexpr std::string_view lockFilename = ".recovery.lock";
constexpr std::string_view lockPurpose = "Maker recovery writer";
constexpr int recoveryFormat = 1;
constexpr std::size_t maximumIdBytes =
    MakerDraftStore::maxNameBytes * 2 + std::string_view("legacy-").size();

bool recoverySlotExists(const fs::path& path)
{
    const auto status = fs::symlink_status(path);
    if (!fs::exists(status)) return false;
    if (!fs::is_regular_file(status))
        throw std::runtime_error("The Maker recovery path is not a file.");
    return true;
}

void validateRecoveryIdentity(const MakerRecoverySnapshot& snapshot)
{
    if (snapshot.draftName.size() > MakerDraftStore::maxNameBytes ||
        (snapshot.draftId &&
         (snapshot.draftId->empty() ||
          snapshot.draftId->size() > maximumIdBytes))) {
        throw std::runtime_error(
            "The Maker recovery draft identity is invalid.");
    }
}

} // namespace

MakerRecoveryStore::MakerRecoveryStore(std::filesystem::path root)
    : root_(std::move(root))
{
    if (root_.empty())
        throw std::invalid_argument("The Maker recovery directory is missing.");
}

std::filesystem::path MakerRecoveryStore::slot(MakerRecoveryKind kind) const
{
    return root_ /
        (kind == MakerRecoveryKind::Puzzle ? "puzzle.autosave.json"
                                           : "overworld.autosave.json");
}

std::optional<MakerRecoverySnapshot> MakerRecoveryStore::load(
    MakerRecoveryKind kind) const
{
    const auto path = slot(kind);
    if (!recoverySlotExists(path)) return std::nullopt;
    const FileLock lock(root_ / lockFilename, lockPurpose);
    if (!recoverySlotExists(path)) return std::nullopt;
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("Could not read the Maker autosave.");

    try {
        const auto encoded = nlohmann::json::parse(input);
        if (encoded.is_null()) return std::nullopt;
        const auto& format = encoded.at("format");
        if (!format.is_number_integer() || format != recoveryFormat) {
            throw std::runtime_error("Unsupported Maker autosave format.");
        }
        MakerRecoverySnapshot snapshot {
            .draftName = encoded.at("draftName").get<std::string>()
        };
        if (const auto id = encoded.find("draftId"); id != encoded.end()) {
            snapshot.draftId = id->get<std::string>();
        }
        validateRecoveryIdentity(snapshot);
        const auto& document = encoded.at("document");
        if (kind == MakerRecoveryKind::Overworld) {
            if (const auto screen = encoded.find("activeScreen");
                screen != encoded.end() && screen->is_number_unsigned()) {
                const auto id = screen->get<uint64_t>();
                if (id != 0 && std::in_range<OverworldScreenId>(id)) {
                    snapshot.activeScreen = static_cast<OverworldScreenId>(id);
                }
            }
            snapshot.document =
                makerDraftCodec::decodeOverworld(document, "Maker autosave");
        } else {
            auto definition = Level::parseDefinition(
                document.get<std::vector<std::string>>(), "Maker autosave");
            makerDraftCodec::validateDraftGrid(definition);
            snapshot.document = std::move(definition);
        }
        return snapshot;
    } catch (const nlohmann::json::exception&) {
        throw std::runtime_error("The Maker autosave is damaged.");
    }
}

void MakerRecoveryStore::save(const MakerRecoverySnapshot& snapshot) const
{
    validateRecoveryIdentity(snapshot);
    nlohmann::json encoded { { "format", recoveryFormat },
                             { "draftName", snapshot.draftName } };
    if (snapshot.draftId) encoded["draftId"] = *snapshot.draftId;
    if (const auto* puzzle =
            std::get_if<Level::Definition>(&snapshot.document)) {
        makerDraftCodec::validateDraftGrid(*puzzle);
        encoded["document"] = Level::serializeDefinition(*puzzle);
    } else {
        encoded["document"] = makerDraftCodec::encodeOverworld(
            std::get<OverworldDraftOverride>(snapshot.document));
        if (snapshot.activeScreen)
            encoded["activeScreen"] = *snapshot.activeScreen;
    }
    const auto contents = encoded.dump();
    const auto path = slot(snapshot.kind());
    const FileLock lock(root_ / lockFilename, lockPurpose);
    (void)recoverySlotExists(path);
    atomicFile::write(path, contents);
}

void MakerRecoveryStore::discard(MakerRecoveryKind kind) const
{
    const auto path = slot(kind);
    if (!recoverySlotExists(path)) return;
    const FileLock lock(root_ / lockFilename, lockPurpose);
    if (!recoverySlotExists(path)) return;
    // Write a tombstone through the same durable replacement path as an
    // autosave.
    atomicFile::write(path, "null");
}

} // namespace sokoban

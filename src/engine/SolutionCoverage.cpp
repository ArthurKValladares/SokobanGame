#include "engine/SolutionCoverage.hpp"

#include "engine/Level.hpp"
#include "engine/LevelCatalog.hpp"
#include "engine/Solution.hpp"

#include <nlohmann/json.hpp>

#include <charconv>
#include <exception>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace sokoban::solution {
namespace {

std::string readText(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("cannot read " + path.string());
    std::stringstream text;
    text << stream.rdbuf();
    return text.str();
}

std::string screenName(LevelLocation location)
{
    return "level" + std::to_string(location.level) + "/screen" +
        std::to_string(location.screen);
}

struct Draft {
    std::uint64_t digest = 0;
    std::string reason;
};

using LocationKey = std::pair<int, int>;

std::map<LocationKey, Draft> readDrafts(const std::filesystem::path& path)
{
    std::map<LocationKey, Draft> drafts;
    if (!std::filesystem::exists(path)) return drafts;
    const auto json = nlohmann::json::parse(readText(path));
    if (!json.is_object() || json.size() != 2 || json.at("format") != 1 ||
        !json.at("drafts").is_array()) {
        throw std::runtime_error("expected format 1 and a drafts array");
    }
    for (const auto& item : json.at("drafts")) {
        if (!item.is_object() || item.size() != 4 ||
            !item.at("level").is_number_integer() ||
            !item.at("screen").is_number_integer()) {
            throw std::runtime_error(
                "draft needs level, screen, level-digest and reason");
        }
        const int level = item.at("level").get<int>();
        const int screen = item.at("screen").get<int>();
        if (level < 0 || screen < 0 || item.at("level") != level ||
            item.at("screen") != screen) {
            throw std::runtime_error(
                "draft location must be nonnegative integers");
        }
        Draft draft;
        draft.reason = item.at("reason").get<std::string>();
        if (draft.reason.find_first_not_of(" \t\r\n") == std::string::npos) {
            throw std::runtime_error("draft needs a nonempty reason");
        }
        const auto digest = item.at("level-digest").get<std::string>();
        const auto [end, error] = std::from_chars(
            digest.data(), digest.data() + digest.size(), draft.digest, 16);
        if (digest.size() != 16 || error != std::errc { } ||
            end != digest.data() + digest.size()) {
            throw std::runtime_error(
                "draft level-digest must be 16 hex digits");
        }
        if (!drafts.emplace(LocationKey { level, screen }, std::move(draft))
                 .second) {
            throw std::runtime_error("duplicate draft location");
        }
    }
    return drafts;
}

} // namespace

CoverageReport auditCoverage(
    const std::filesystem::path& levelsRoot,
    const std::filesystem::path& solutionsDir)
{
    CoverageReport report;
    const auto coveragePath = solutionsDir / "coverage.json";
    std::map<LocationKey, Draft> drafts;
    try {
        drafts = readDrafts(coveragePath);
    } catch (const std::exception& error) {
        report.errors.push_back("coverage.json: " + std::string(error.what()));
    }
    std::map<std::filesystem::path, Solution> recordings;
    if (std::filesystem::is_directory(solutionsDir)) {
        for (const auto& entry :
             std::filesystem::directory_iterator(solutionsDir)) {
            if (!entry.is_regular_file() ||
                entry.path().extension() != ".solution") {
                continue;
            }
            try {
                recordings.emplace(entry.path(), parse(readText(entry.path())));
            } catch (const std::exception& error) {
                report.errors.push_back(
                    entry.path().filename().string() + ": " + error.what());
            }
        }
    }
    // Enumerate instead of stopping at the first gap: a missing directory or
    // screen must not silently remove everything after it from coverage.
    std::map<LocationKey, std::filesystem::path> screens;
    if (std::filesystem::is_directory(levelsRoot)) {
        for (const auto& directory :
             std::filesystem::directory_iterator(levelsRoot)) {
            if (!directory.is_directory()) continue;
            const auto level = levelIndexFromDirectoryName(
                directory.path().filename().string());
            if (!level) continue;
            for (const auto& entry :
                 std::filesystem::directory_iterator(directory.path())) {
                if (!entry.is_regular_file()) continue;
                const auto screen =
                    screenIndexFromFilename(entry.path().filename().string());
                if (screen)
                    screens.emplace(
                        LocationKey { *level, *screen }, entry.path());
            }
        }
    }
    report.screens = screens.size();
    if (screens.empty()) report.errors.push_back("no puzzle screens found");
    std::set<std::filesystem::path> used;
    for (const auto& [key, path] : screens) {
        const auto name = screenName({ key.first, key.second });
        try {
            const auto definition = Level::loadDefinitionFromFile(path);
            const auto digest = levelDigest(definition);
            const auto exemption = drafts.find(key);
            bool isDraft = false;
            if (exemption != drafts.end()) {
                if (exemption->second.digest != digest) {
                    report.errors.push_back(
                        name +
                        ": draft changed; review its coverage exception");
                } else {
                    isDraft = true;
                    ++report.drafts;
                    report.notes.push_back(
                        name + ": draft: " + exemption->second.reason);
                }
            }
            bool matched = false;
            for (const auto& [file, recording] : recordings) {
                if (recording.levelDigest != digest) continue;
                matched = true;
                used.insert(file);
                const auto result = replay(
                    Level::loadFromDefinition(definition, name), recording);
                if (!result.passed) {
                    report.errors.push_back(
                        name + " (" + file.filename().string() +
                        "): " + result.message);
                }
            }
            if (matched) {
                ++report.replayed;
            } else if (!isDraft) {
                report.errors.push_back(
                    name + ": missing current recording (digest " +
                    digestText(digest) + "); solve and re-record this screen");
            }
        } catch (const std::exception& error) {
            report.errors.push_back(name + ": " + error.what());
        }
    }
    for (const auto& [key, draft] : drafts) {
        (void)draft;
        if (!screens.contains(key)) {
            report.errors.push_back(
                screenName({ key.first, key.second }) +
                ": coverage exception names a missing screen");
        }
    }
    for (const auto& [file, recording] : recordings) {
        (void)recording;
        if (!used.contains(file)) {
            report.notes.push_back(
                file.filename().string() + ": matches no current screen");
        }
    }
    return report;
}

} // namespace sokoban::solution

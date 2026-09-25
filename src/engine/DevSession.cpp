#include "engine/DevSession.hpp"

#include "engine/AtomicFile.hpp"

#include <nlohmann/json.hpp>

#include <fstream>

namespace sokoban {

namespace {

using Json = nlohmann::ordered_json;

constexpr int devSessionFormat = 1;

std::u8string utf8(const std::string& text)
{
    return { text.begin(), text.end() };
}

std::string narrow(const std::u8string& text)
{
    return { text.begin(), text.end() };
}

} // namespace

std::optional<DevSession> loadDevSession(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return std::nullopt;
    }
    try {
        const Json json = Json::parse(input);
        if (json.value("format", 0) != devSessionFormat) {
            return std::nullopt;
        }
        DevSession session;
        session.resumeOnLaunch = json.value("resumeOnLaunch", true);
        session.editorDocument = std::filesystem::path(
            utf8(json.value("editorDocument", std::string {})));
        session.editingDocument = json.value("editingDocument", false);
        session.activeLayer = json.value("activeLayer", 0);
        session.tool = json.value("tool", std::string("tiles"));
        return session;
    } catch (const nlohmann::json::exception&) {
        return std::nullopt;
    }
}

void saveDevSession(const std::filesystem::path& path, const DevSession& session)
{
    const Json json {
        { "format", devSessionFormat },
        { "resumeOnLaunch", session.resumeOnLaunch },
        { "editorDocument", narrow(session.editorDocument.generic_u8string()) },
        { "editingDocument", session.editingDocument },
        { "activeLayer", session.activeLayer },
        { "tool", session.tool },
    };
    atomicFile::write(path, json.dump(2) + '\n');
}

} // namespace sokoban

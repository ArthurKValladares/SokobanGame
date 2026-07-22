#include "engine/Log.hpp"

#include <array>
#include <chrono>
#include <ctime>
#include <fstream>
#include <iostream>
#include <mutex>
#include <string_view>

namespace sokoban::log {
namespace {

// All shared logging state lives behind one mutex so render-thread,
// background, and main-thread messages never interleave mid-line.
std::mutex& sinkMutex()
{
    static std::mutex mutex;
    return mutex;
}

Level& minimumLevel()
{
    static Level level = Level::Info;
    return level;
}

std::ofstream& fileSink()
{
    static std::ofstream stream;
    return stream;
}

std::filesystem::path& fileSinkPath()
{
    static std::filesystem::path path;
    return path;
}

[[nodiscard]] std::string_view levelName(Level level)
{
    switch (level) {
    case Level::Debug: return "DEBUG";
    case Level::Info: return "INFO";
    case Level::Warning: return "WARN";
    case Level::Error: return "ERROR";
    }
    return "?";
}

[[nodiscard]] std::string timestamp()
{
    const std::time_t now = std::chrono::system_clock::to_time_t(
        std::chrono::system_clock::now());
    std::tm local {};
#if defined(_WIN32)
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    std::array<char, 16> buffer {};
    const std::size_t written =
        std::strftime(buffer.data(), buffer.size(), "%H:%M:%S", &local);
    return std::string(buffer.data(), written);
}

} // namespace

void setMinimumLevel(Level level)
{
    const std::lock_guard<std::mutex> lock(sinkMutex());
    minimumLevel() = level;
}

void addFileSink(const std::filesystem::path& path)
{
    const std::lock_guard<std::mutex> lock(sinkMutex());
    if (fileSink().is_open() && fileSinkPath() == path) {
        return;
    }
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    fileSink().close();
    fileSink().clear();
    fileSink().open(path, std::ios::out | std::ios::app);
    if (fileSink()) {
        fileSinkPath() = path;
    } else {
        // Best effort: the game runs without the file, stderr still works.
        std::cerr << "Log: could not open log file " << path.string() << '\n';
        fileSinkPath().clear();
    }
}

void reset()
{
    const std::lock_guard<std::mutex> lock(sinkMutex());
    fileSink().close();
    fileSink().clear();
    fileSinkPath().clear();
    minimumLevel() = Level::Info;
}

Message::Message(Level level)
    : level_(level)
    , enabled_(false)
{
    const std::lock_guard<std::mutex> lock(sinkMutex());
    enabled_ = static_cast<int>(level) >= static_cast<int>(minimumLevel());
}

Message::~Message()
{
    if (!enabled_) {
        return;
    }
    std::string line = "[" + timestamp() + "] [" +
        std::string(levelName(level_)) + "] " + stream_.str() + "\n";

    const std::lock_guard<std::mutex> lock(sinkMutex());
    std::cerr << line;
    if (fileSink().is_open()) {
        fileSink() << line;
        fileSink().flush();
    }
}

} // namespace sokoban::log

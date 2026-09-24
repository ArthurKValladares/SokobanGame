#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace sokoban::log {

enum class Level {
    Debug,
    Info,
    Warning,
    Error,
};

enum class Category {
    General,
    Application,
    Gameplay,
    Rendering,
    Assets,
    Audio,
    Persistence,
    Input,
    Editor,
    Tasks,
    Logging,
    Count,
};

inline constexpr std::size_t categoryCount =
    static_cast<std::size_t>(Category::Count);
inline constexpr std::size_t historyCapacity = 4096;

[[nodiscard]] std::string_view levelName(Level level);
[[nodiscard]] std::string_view categoryName(Category category);

// A bounded in-memory copy of recent messages for diagnostics and developer
// tools. Entries are captured before the asynchronous output queue, so a full
// file/stderr queue does not make the debug console blind to the incident.
struct Entry {
    uint64_t sequence = 0;
    std::chrono::system_clock::time_point timestamp;
    Level level = Level::Info;
    Category category = Category::General;
    std::string message;
};

struct HistorySnapshot {
    uint64_t revision = 0;
    std::vector<Entry> entries;
};

struct Configuration {
    std::size_t queueCapacity = 4096;
    std::chrono::milliseconds flushInterval { 1000 };
    // A zero limit deliberately disables file rotation. Otherwise the active
    // log is capped and the newest archived logs use .1, .2, ... suffixes.
    std::uintmax_t maxFileBytes = 2 * 1024 * 1024;
    std::size_t maxArchivedFiles = 5;
    bool stderrEnabled = true;
};

struct Diagnostics {
    uint64_t enqueuedMessages = 0;
    uint64_t writtenMessages = 0;
    uint64_t filteredMessages = 0;
    uint64_t droppedMessages = 0;
    uint64_t droppedMessageReports = 0;
    uint64_t flushes = 0;
    uint64_t fileSinkFailures = 0;
    uint64_t fileRotations = 0;
    uint64_t fileRotationFailures = 0;
    std::array<uint64_t, categoryCount> droppedByCategory {};
    std::size_t queuedMessages = 0;
    std::size_t queueCapacity = 0;
    bool writerRunning = false;
    bool writerActive = false;
    bool fileSinkOpen = false;
};

// Replaces the process logger configuration after draining and stopping its
// current writer. The defaults are suitable for the game; configurability is
// also useful to isolate logging tests from stderr.
void configure(Configuration configuration);

// Messages below this level are filtered before formatting or queueing.
// Default Info; Debug builds may lower it to Debug.
void setMinimumLevel(Level level);

// Asynchronously opens/replaces the append-only file sink on the writer
// thread. Passing the active path again is a no-op.
void addFileSink(const std::filesystem::path& path);

// Waits until all accepted messages are written and both sinks are flushed.
void flush();

// Drains, flushes, and joins the writer. A later message/configuration request
// starts it again, which keeps reset-based tests and late shutdown logs safe.
void shutdown();

// Drains and restores default configuration, level, sinks, and diagnostics.
void reset();

[[nodiscard]] Diagnostics diagnostics();

// History is opt-in so shipping builds do not copy every log message solely
// for a developer panel. Disabling it also releases retained messages.
void setHistoryEnabled(bool enabled);

// Supplying the revision from a previous snapshot avoids copying unchanged
// entries. A changed revision always returns the complete current history,
// including an empty vector after clearHistory().
[[nodiscard]] HistorySnapshot historySnapshot(
    std::optional<uint64_t> knownRevision = std::nullopt);
void clearHistory();

// Accumulates one message on the calling thread and enqueues it on destruction.
// Disk/stderr output, timestamp formatting, and sink flushing are writer-owned.
// Output format:
//   [HH:MM:SS.mmm] [LEVEL] [CATEGORY] message
class Message {
public:
    explicit Message(
        Level level,
        Category category = Category::General);
    ~Message() noexcept;

    Message(const Message&) = delete;
    Message& operator=(const Message&) = delete;

    template <typename T>
    Message& operator<<(const T& value)
    {
        if (enabled_) {
            stream_ << value;
        }
        return *this;
    }

private:
    Level level_;
    Category category_;
    bool enabled_;
    std::ostringstream stream_;
};

[[nodiscard]] inline Message debug(
    Category category = Category::General)
{
    return Message(Level::Debug, category);
}

[[nodiscard]] inline Message info(
    Category category = Category::General)
{
    return Message(Level::Info, category);
}

[[nodiscard]] inline Message warning(
    Category category = Category::General)
{
    return Message(Level::Warning, category);
}

[[nodiscard]] inline Message error(
    Category category = Category::General)
{
    return Message(Level::Error, category);
}

} // namespace sokoban::log

#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <sstream>

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

struct Configuration {
    std::size_t queueCapacity = 4096;
    std::chrono::milliseconds flushInterval { 1000 };
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

#pragma once

#include <filesystem>
#include <sstream>

namespace sokoban::log {

enum class Level {
    Debug,
    Info,
    Warning,
    Error,
};

// Messages below this level are dropped. Default Info (Debug is silent in
// normal runs). Debug builds may lower it to Level::Debug.
void setMinimumLevel(Level level);

// Additionally append log lines to this file (best effort; a failure to open
// is reported once to stderr and then ignored). Intended to be dropped next
// to the player profiles so shipped builds leave a diagnostic trail. Passing
// the same path again is a no-op.
void addFileSink(const std::filesystem::path& path);

// Closes the file sink and restores the default minimum level (tests/shutdown).
void reset();

// Accumulates a single log line via operator<< and, on destruction, emits
// "[HH:MM:SS] [LEVEL] <message>\n" to stderr (and the file sink, if any) when
// the level clears the minimum. Build one through the level helpers below and
// stream into the temporary:
//
//     log::warning() << "asset preload skipped " << path << ": " << reason;
//
// The temporary flushes at the end of the full expression. Emission is
// serialized, so background/render-thread logs interleave cleanly.
class Message {
public:
    explicit Message(Level level);
    ~Message();

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
    bool enabled_;
    std::ostringstream stream_;
};

[[nodiscard]] inline Message debug() { return Message(Level::Debug); }
[[nodiscard]] inline Message info() { return Message(Level::Info); }
[[nodiscard]] inline Message warning() { return Message(Level::Warning); }
[[nodiscard]] inline Message error() { return Message(Level::Error); }

} // namespace sokoban::log

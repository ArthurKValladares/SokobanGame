#include "engine/Log.hpp"

#include "engine/LogQueue.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

namespace sokoban::log {
namespace {

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

[[nodiscard]] std::string_view categoryName(Category category)
{
    switch (category) {
    case Category::General: return "GENERAL";
    case Category::Application: return "APP";
    case Category::Gameplay: return "GAMEPLAY";
    case Category::Rendering: return "RENDER";
    case Category::Assets: return "ASSETS";
    case Category::Audio: return "AUDIO";
    case Category::Persistence: return "SAVE";
    case Category::Input: return "INPUT";
    case Category::Editor: return "EDITOR";
    case Category::Tasks: return "TASKS";
    case Category::Logging: return "LOG";
    case Category::Count: break;
    }
    return "?";
}

[[nodiscard]] std::string timestamp(
    std::chrono::system_clock::time_point time)
{
    const std::time_t seconds =
        std::chrono::system_clock::to_time_t(time);
    std::tm local {};
#if defined(_WIN32)
    localtime_s(&local, &seconds);
#else
    localtime_r(&seconds, &local);
#endif
    std::array<char, 16> clock {};
    const std::size_t written = std::strftime(
        clock.data(), clock.size(), "%H:%M:%S", &local);
    const auto milliseconds =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            time.time_since_epoch()) %
        std::chrono::seconds(1);
    std::array<char, 24> result {};
    std::snprintf(
        result.data(),
        result.size(),
        "%.*s.%03lld",
        static_cast<int>(written),
        clock.data(),
        static_cast<long long>(milliseconds.count()));
    return result.data();
}

[[nodiscard]] std::string formatRecord(
    const detail::Record& record)
{
    return "[" + timestamp(record.timestamp) + "] [" +
        std::string(levelName(record.level)) + "] [" +
        std::string(categoryName(record.category)) + "] " +
        record.message + '\n';
}

class ProcessLogger {
public:
    ProcessLogger()
        : queue_(std::make_unique<detail::BoundedQueue>(
              configuration_.queueCapacity))
    {
    }

    ~ProcessLogger()
    {
        shutdown();
    }

    bool enabled(Level level)
    {
        if (static_cast<int>(level) >=
            minimumLevel_.load(std::memory_order_relaxed)) {
            return true;
        }
        filteredMessages_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    void enqueue(
        Level level,
        Category category,
        std::string message) noexcept
    {
        try {
            detail::Record record {
                .timestamp = std::chrono::system_clock::now(),
                .level = level,
                .category = category,
                .message = std::move(message),
            };
            std::lock_guard lock(mutex_);
            if (stopping_) {
                noteDropLocked(category);
                return;
            }
            ensureWriterLocked();
            const detail::PushResult pushed =
                queue_->push(std::move(record));
            if (pushed.droppedCategory) {
                noteDropLocked(*pushed.droppedCategory);
            }
            if (pushed.accepted) {
                ++diagnostics_.enqueuedMessages;
            }
            wake_.notify_one();
        } catch (...) {
            // Logging must never terminate the process. Allocation failures
            // are intentionally invisible to the caller.
        }
    }

    void configure(Configuration configuration)
    {
        configuration.queueCapacity =
            std::max<std::size_t>(configuration.queueCapacity, 1);
        configuration.flushInterval = std::max(
            configuration.flushInterval,
            std::chrono::milliseconds(1));
        shutdown();
        std::lock_guard lock(mutex_);
        configuration_ = configuration;
        queue_ = std::make_unique<detail::BoundedQueue>(
            configuration_.queueCapacity);
        diagnostics_.queueCapacity = configuration_.queueCapacity;
    }

    void setMinimumLevel(Level level)
    {
        minimumLevel_.store(
            static_cast<int>(level),
            std::memory_order_relaxed);
    }

    void addFileSink(const std::filesystem::path& path)
    {
        std::lock_guard lock(mutex_);
        if (requestedFilePath_ == path &&
            diagnostics_.fileSinkOpen) {
            return;
        }
        requestedFilePath_ = path;
        ++fileConfigurationGeneration_;
        ensureWriterLocked();
        wake_.notify_one();
    }

    void flush()
    {
        std::unique_lock lock(mutex_);
        drained_.wait(lock, [this] { return !stopping_; });
        ensureWriterLocked();
        const uint64_t request = ++flushRequestGeneration_;
        wake_.notify_one();
        drained_.wait(lock, [&] {
            return flushCompletedGeneration_ >= request &&
                queue_->empty() &&
                pendingDroppedMessages_ == 0 &&
                !writerActive_;
        });
    }

    void shutdown()
    {
        std::thread writer;
        {
            std::unique_lock lock(mutex_);
            if (stopping_) {
                drained_.wait(lock, [this] { return !stopping_; });
                return;
            }
            if (!writer_.joinable()) {
                return;
            }
            stopping_ = true;
            wake_.notify_one();
            writer = std::move(writer_);
        }
        writer.join();
        {
            std::lock_guard lock(mutex_);
            stopping_ = false;
            drained_.notify_all();
        }
    }

    void reset()
    {
        shutdown();
        std::lock_guard lock(mutex_);
        configuration_ = {};
        queue_ = std::make_unique<detail::BoundedQueue>(
            configuration_.queueCapacity);
        requestedFilePath_.clear();
        fileConfigurationGeneration_ = 0;
        flushRequestGeneration_ = 0;
        flushCompletedGeneration_ = 0;
        pendingDroppedMessages_ = 0;
        diagnostics_ = {};
        diagnostics_.queueCapacity = configuration_.queueCapacity;
        filteredMessages_.store(0, std::memory_order_relaxed);
        minimumLevel_.store(
            static_cast<int>(Level::Info),
            std::memory_order_relaxed);
    }

    Diagnostics diagnostics()
    {
        std::lock_guard lock(mutex_);
        Diagnostics result = diagnostics_;
        result.filteredMessages =
            filteredMessages_.load(std::memory_order_relaxed);
        result.queuedMessages = queue_->size();
        result.queueCapacity = queue_->capacity();
        result.writerRunning = writer_.joinable();
        result.writerActive = writerActive_;
        return result;
    }

private:
    void ensureWriterLocked()
    {
        if (writer_.joinable() || stopping_) {
            return;
        }
        stopping_ = false;
        writer_ = std::thread([this] { writerMain(); });
    }

    void noteDropLocked(Category category)
    {
        ++diagnostics_.droppedMessages;
        ++pendingDroppedMessages_;
        const std::size_t index = static_cast<std::size_t>(category);
        if (index < diagnostics_.droppedByCategory.size()) {
            ++diagnostics_.droppedByCategory[index];
        }
    }

    void writerMain()
    {
        std::ofstream file;
        uint64_t appliedFileConfiguration = 0;
        auto nextPeriodicFlush =
            std::chrono::steady_clock::now() +
            configuration_.flushInterval;

        for (;;) {
            std::vector<detail::Record> records;
            uint64_t dropped = 0;
            uint64_t requestedFlush = 0;
            uint64_t requestedFileConfiguration = 0;
            std::filesystem::path requestedPath;
            Configuration configuration;
            bool stopping = false;
            bool periodicFlush = false;
            {
                std::unique_lock lock(mutex_);
                wake_.wait_until(lock, nextPeriodicFlush, [&] {
                    return stopping_ ||
                        !queue_->empty() ||
                        pendingDroppedMessages_ != 0 ||
                        fileConfigurationGeneration_ !=
                            appliedFileConfiguration ||
                        flushRequestGeneration_ >
                            flushCompletedGeneration_;
                });
                periodicFlush =
                    std::chrono::steady_clock::now() >=
                    nextPeriodicFlush;
                records = queue_->takeAll();
                dropped = std::exchange(
                    pendingDroppedMessages_, 0);
                requestedFlush = flushRequestGeneration_;
                requestedFileConfiguration =
                    fileConfigurationGeneration_;
                requestedPath = requestedFilePath_;
                configuration = configuration_;
                stopping = stopping_;
                writerActive_ =
                    !records.empty() ||
                    dropped != 0 ||
                    requestedFileConfiguration !=
                        appliedFileConfiguration ||
                    periodicFlush ||
                    requestedFlush > flushCompletedGeneration_;
            }

            bool fileConfigurationChanged =
                requestedFileConfiguration !=
                appliedFileConfiguration;
            bool fileOpenFailed = false;
            if (fileConfigurationChanged) {
                file.close();
                file.clear();
                if (!requestedPath.empty()) {
                    std::error_code error;
                    std::filesystem::create_directories(
                        requestedPath.parent_path(), error);
                    file.open(
                        requestedPath,
                        std::ios::out | std::ios::app);
                    if (!file) {
                        fileOpenFailed = true;
                        if (configuration.stderrEnabled) {
                            std::cerr
                                << "Log: could not open log file "
                                << requestedPath.string() << '\n';
                        }
                    }
                }
                appliedFileConfiguration =
                    requestedFileConfiguration;
            }

            if (dropped != 0) {
                detail::Record report {
                    .timestamp = std::chrono::system_clock::now(),
                    .level = Level::Warning,
                    .category = Category::Logging,
                    .message = "Dropped " + std::to_string(dropped) +
                        " log message" +
                        (dropped == 1 ? "" : "s") +
                        " because the queue was full",
                };
                const std::string line = formatRecord(report);
                if (configuration.stderrEnabled) {
                    std::cerr << line;
                }
                if (file.is_open()) {
                    file << line;
                }
            }

            bool containsError = false;
            for (const detail::Record& record : records) {
                const std::string line = formatRecord(record);
                if (configuration.stderrEnabled) {
                    std::cerr << line;
                }
                if (file.is_open()) {
                    file << line;
                }
                containsError |= record.level == Level::Error;
            }

            const bool mustFlush =
                periodicFlush ||
                containsError ||
                stopping ||
                requestedFlush > flushCompletedGeneration_;
            if (mustFlush) {
                if (configuration.stderrEnabled) {
                    std::cerr.flush();
                }
                if (file.is_open()) {
                    file.flush();
                }
                nextPeriodicFlush =
                    std::chrono::steady_clock::now() +
                    configuration.flushInterval;
            }

            bool shouldStop = false;
            {
                std::lock_guard lock(mutex_);
                diagnostics_.writtenMessages += records.size();
                if (dropped != 0) {
                    ++diagnostics_.droppedMessageReports;
                }
                if (mustFlush) {
                    ++diagnostics_.flushes;
                    flushCompletedGeneration_ = std::max(
                        flushCompletedGeneration_,
                        requestedFlush);
                }
                if (fileOpenFailed) {
                    ++diagnostics_.fileSinkFailures;
                }
                diagnostics_.fileSinkOpen = file.is_open();
                writerActive_ = false;
                shouldStop = stopping_ &&
                    queue_->empty() &&
                    pendingDroppedMessages_ == 0;
                drained_.notify_all();
            }
            if (shouldStop) {
                break;
            }
        }

        if (file.is_open()) {
            file.flush();
            file.close();
        }
        {
            std::lock_guard lock(mutex_);
            diagnostics_.fileSinkOpen = false;
            writerActive_ = false;
            drained_.notify_all();
        }
    }

    std::mutex mutex_;
    std::condition_variable wake_;
    std::condition_variable drained_;
    Configuration configuration_;
    std::unique_ptr<detail::BoundedQueue> queue_;
    std::thread writer_;
    std::filesystem::path requestedFilePath_;
    uint64_t fileConfigurationGeneration_ = 0;
    uint64_t flushRequestGeneration_ = 0;
    uint64_t flushCompletedGeneration_ = 0;
    uint64_t pendingDroppedMessages_ = 0;
    Diagnostics diagnostics_ {
        .queueCapacity = configuration_.queueCapacity,
    };
    std::atomic<int> minimumLevel_ {
        static_cast<int>(Level::Info) };
    std::atomic<uint64_t> filteredMessages_ { 0 };
    bool writerActive_ = false;
    bool stopping_ = false;
};

ProcessLogger& processLogger()
{
    static ProcessLogger logger;
    return logger;
}

} // namespace

void configure(Configuration configuration)
{
    processLogger().configure(configuration);
}

void setMinimumLevel(Level level)
{
    processLogger().setMinimumLevel(level);
}

void addFileSink(const std::filesystem::path& path)
{
    processLogger().addFileSink(path);
}

void flush()
{
    processLogger().flush();
}

void shutdown()
{
    processLogger().shutdown();
}

void reset()
{
    processLogger().reset();
}

Diagnostics diagnostics()
{
    return processLogger().diagnostics();
}

Message::Message(Level level, Category category)
    : level_(level)
    , category_(category)
    , enabled_(processLogger().enabled(level))
{
}

Message::~Message() noexcept
{
    if (!enabled_) {
        return;
    }
    try {
        processLogger().enqueue(
            level_, category_, stream_.str());
    } catch (...) {
    }
}

} // namespace sokoban::log

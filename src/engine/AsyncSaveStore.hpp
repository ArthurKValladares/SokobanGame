#pragma once

#include "engine/SaveStore.hpp"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace sokoban {

// Serializes and coalesces runtime profile writes on a dedicated worker.
// Loading remains synchronous during startup, before the game loop begins.
class AsyncSaveStore {
public:
    enum class Urgency {
        Deferred,
        Immediate,
    };

    struct Diagnostics {
        std::uint64_t requests = 0;
        std::uint64_t completedWrites = 0;
        std::uint64_t coalescedRequests = 0;
        bool pending = false;
        bool writing = false;
        bool lastWriteSucceeded = true;
    };

    explicit AsyncSaveStore(
        std::filesystem::path root,
        std::chrono::milliseconds writeDelay = std::chrono::seconds(2));
    ~AsyncSaveStore();

    AsyncSaveStore(const AsyncSaveStore&) = delete;
    AsyncSaveStore& operator=(const AsyncSaveStore&) = delete;

    [[nodiscard]] SaveStore::LoadResult load();
    void requestSave(PlayerProfile profile, Urgency urgency = Urgency::Deferred);
    void flush();

    [[nodiscard]] std::string status() const;
    [[nodiscard]] Diagnostics diagnostics() const;
    [[nodiscard]] const std::filesystem::path& primaryPath() const
    {
        return store_.primaryPath();
    }

private:
    void workerLoop();

    SaveStore store_;
    const std::chrono::milliseconds writeDelay_;
    mutable std::mutex mutex_;
    std::condition_variable condition_;
    std::optional<PlayerProfile> pendingProfile_;
    std::chrono::steady_clock::time_point writeDeadline_ {};
    std::string status_;
    std::uint64_t requestCount_ = 0;
    std::uint64_t completedWriteCount_ = 0;
    std::uint64_t coalescedRequestCount_ = 0;
    bool forceWrite_ = false;
    bool writing_ = false;
    bool stopping_ = false;
    bool lastWriteSucceeded_ = true;
    // Declared last so the worker cannot observe partially constructed state.
    std::thread worker_;
};

} // namespace sokoban

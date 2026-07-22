#pragma once

#include "engine/SaveStore.hpp"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace sokoban {

// Serializes and coalesces runtime profile writes on a single dedicated
// worker thread. A store may host several independent channels (each its own
// on-disk SaveStore and pending profile); the one worker services all of
// them, so multiple save destinations do not each cost a thread. Loading
// stays synchronous (used during startup and slot switches). The channel-less
// overloads target channel 0, preserving the original single-store API.
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
        std::chrono::milliseconds writeDelay = std::chrono::seconds(2),
        std::string fileStem = "profile",
        ProfileSections sections = ProfileSections::All);
    ~AsyncSaveStore();

    AsyncSaveStore(const AsyncSaveStore&) = delete;
    AsyncSaveStore& operator=(const AsyncSaveStore&) = delete;

    // Adds another keyed store served by the same worker; returns its channel
    // id. Call only during setup (before saves are in flight).
    [[nodiscard]] int addChannel(
        std::filesystem::path root,
        std::string fileStem,
        ProfileSections sections);

    // Drains the channel's pending write and repoints it at a new store
    // (e.g. a save-slot switch). No-op-safe from the game thread.
    void replaceChannel(
        int channel,
        std::filesystem::path root,
        std::string fileStem,
        ProfileSections sections);

    [[nodiscard]] SaveStore::LoadResult load(int channel = 0);
    void requestSave(int channel, PlayerProfile profile, Urgency urgency = Urgency::Deferred);
    void requestSave(PlayerProfile profile, Urgency urgency = Urgency::Deferred)
    {
        requestSave(0, std::move(profile), urgency);
    }
    // Blocks until every channel has no pending or in-flight write.
    void flush();

    [[nodiscard]] std::string status(int channel = 0) const;
    [[nodiscard]] Diagnostics diagnostics(int channel = 0) const;
    [[nodiscard]] const std::filesystem::path& primaryPath(int channel = 0) const;

private:
    struct Channel {
        explicit Channel(SaveStore store)
            : store(std::move(store))
        {
        }

        SaveStore store;
        std::optional<PlayerProfile> pending;
        std::chrono::steady_clock::time_point deadline {};
        std::string status;
        std::uint64_t requestCount = 0;
        std::uint64_t completedWriteCount = 0;
        std::uint64_t coalescedRequestCount = 0;
        bool forceWrite = false;
        bool writing = false;
        bool lastWriteSucceeded = true;
    };

    void workerLoop();
    [[nodiscard]] bool anyPendingLocked() const;
    [[nodiscard]] Channel& channelAt(int channel);
    [[nodiscard]] const Channel& channelAt(int channel) const;

    const std::chrono::milliseconds writeDelay_;
    mutable std::mutex mutex_;
    std::condition_variable condition_;
    // deque keeps element addresses stable as channels are added at setup.
    std::deque<Channel> channels_;
    bool stopping_ = false;
    // Declared last so the worker cannot observe partially constructed state.
    std::thread worker_;
};

} // namespace sokoban

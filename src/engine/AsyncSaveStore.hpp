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
#include <vector>

namespace sokoban {

// Serializes and coalesces runtime profile writes on a single dedicated
// worker thread. A store may host several independent channels (each its own
// on-disk SaveStore and pending profile); the one worker services all of
// them, so multiple save destinations do not each cost a thread. Loading
// stays synchronous (used during startup and slot switches). The channel-less
// overloads target channel 0, preserving the original single-store API.
class AsyncSaveStore {
public:
    using Revision = std::uint64_t;

    enum class Urgency {
        Deferred,
        Immediate,
    };

    enum class PersistenceOutcome {
        Persisted,
        RetryableFailure,
    };

    struct PersistenceResult {
        PersistenceOutcome outcome = PersistenceOutcome::Persisted;
        // Repointing or deleting a channel begins a fresh destination epoch,
        // represented by zero until that destination receives a save request.
        Revision requestedRevision = 0;
        Revision persistedRevision = 0;
        std::string message;
    };

    struct FlushResult {
        // Indexed by channel id. The channel set is stable while an owner-side
        // management operation such as flush is in progress.
        std::vector<PersistenceResult> channels;

        [[nodiscard]] bool allPersisted() const;
        [[nodiscard]] const PersistenceResult& forChannel(int channel) const;
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

    // Ownership contract: construction, destruction, channel management,
    // loading, deletion, flushing and primaryPath() belong to one owner thread
    // (the application's main thread). Do not run those operations concurrently
    // with requestSave(). requestSave(), retryFailedSave(), status() and
    // diagnostics() are synchronized producer/snapshot operations.

    // Adds another keyed store served by the same worker; returns its channel
    // id. Call only during owner-thread setup, before saves are in flight.
    [[nodiscard]] int addChannel(
        std::filesystem::path root,
        std::string fileStem,
        ProfileSections sections);

    // Drains the channel's pending write and repoints it at a new store (e.g. a
    // save-slot switch). A RetryableFailure leaves the old channel and its
    // latest requested snapshot intact.
    [[nodiscard]] PersistenceResult replaceChannel(
        int channel,
        std::filesystem::path root,
        std::string fileStem,
        ProfileSections sections);

    [[nodiscard]] SaveStore::LoadResult load(int channel = 0);
    Revision requestSave(
        int channel,
        PlayerProfile profile,
        Urgency urgency = Urgency::Deferred);
    Revision requestSave(
        PlayerProfile profile,
        Urgency urgency = Urgency::Deferred)
    {
        return requestSave(0, std::move(profile), urgency);
    }
    // Makes a retained failed snapshot eligible for one more background write.
    // Returns false when this channel has no failed snapshot to retry.
    [[nodiscard]] bool retryFailedSave(int channel = 0);
    // Quiesces one channel, commits its deletion marker, and discards its
    // queued or retained failed snapshot only after that commit succeeds.
    [[nodiscard]] SaveStore::DeleteResult deleteProfile(int channel = 0);
    // Blocks until every channel has no actionable or in-flight write. A
    // failed snapshot remains pending but blocked from automatic retries;
    // another request supersedes it. The result records the newest requested
    // and durably persisted revision for every channel's current destination.
    [[nodiscard]] FlushResult flush();

    [[nodiscard]] std::string status(int channel = 0) const;
    [[nodiscard]] Diagnostics diagnostics(int channel = 0) const;
    [[nodiscard]] std::filesystem::path primaryPath(int channel = 0) const;

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
        Revision nextRevision = 0;
        Revision requestedRevision = 0;
        Revision pendingRevision = 0;
        Revision persistedRevision = 0;
        std::uint64_t completedWriteCount = 0;
        std::uint64_t coalescedRequestCount = 0;
        bool forceWrite = false;
        bool retryBlocked = false;
        bool writing = false;
        bool lastWriteSucceeded = true;
    };

    void workerLoop();
    [[nodiscard]] bool anyPendingLocked() const;
    [[nodiscard]] PersistenceResult persistenceResultLocked(int channel) const;
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

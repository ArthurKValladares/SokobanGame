#pragma once

#include "engine/MakerRecoveryStore.hpp"

#include <array>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace sokoban {

// Writes Maker recovery journals on a worker thread.
//
// A journal write serializes the whole draft and replaces the file durably,
// which flushes the disk. Doing that on the frame that noticed an edit
// stalls it; the caller takes the snapshot (a copy) and hands it over here
// instead.
//
// Each kind has one waiting request. A newer request replaces an older one
// that has not started, so a slow disk coalesces writes rather than queueing
// them; requests for one kind still complete in the order they were made.
// Results are collected by the owner thread with takeResults(). A replaced
// request produces no result. The worker starts on the first request and the
// destructor completes whatever is waiting before it joins.
class MakerAutosaveWriter {
public:
    struct Result {
        MakerRecoveryKind kind = MakerRecoveryKind::Puzzle;
        // The revision passed with the request.
        uint64_t revision = 0;
        // True after a save, false after a discard. Meaningful on success.
        bool journalPresent = false;
        // Empty on success.
        std::string error;
    };

    explicit MakerAutosaveWriter(MakerRecoveryStore store);
    ~MakerAutosaveWriter();

    MakerAutosaveWriter(const MakerAutosaveWriter&) = delete;
    MakerAutosaveWriter& operator=(const MakerAutosaveWriter&) = delete;

    void save(uint64_t revision, MakerRecoverySnapshot snapshot);
    void discard(MakerRecoveryKind kind, uint64_t revision);

    // Completed requests since the last call, oldest first.
    [[nodiscard]] std::vector<Result> takeResults();

    // Blocks until every request made so far has completed. Use before
    // touching the recovery store directly.
    void flush();

private:
    struct Request {
        MakerRecoveryKind kind = MakerRecoveryKind::Puzzle;
        uint64_t revision = 0;
        // Empty for a discard.
        std::optional<MakerRecoverySnapshot> snapshot;
        // Orders the two kinds' waiting requests against each other.
        uint64_t sequence = 0;
    };

    void submit(Request request);
    void workerLoop();

    MakerRecoveryStore store_;
    std::mutex mutex_;
    std::condition_variable changed_;
    std::array<std::optional<Request>, 2> waiting_;
    std::vector<Result> results_;
    uint64_t nextSequence_ = 0;
    bool writing_ = false;
    bool stopping_ = false;
    std::thread worker_;
};

} // namespace sokoban

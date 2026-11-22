#include "engine/MakerAutosaveWriter.hpp"

#include <exception>
#include <utility>

namespace sokoban {
namespace {

std::size_t slotIndex(MakerRecoveryKind kind)
{
    return kind == MakerRecoveryKind::Puzzle ? 0 : 1;
}

} // namespace

MakerAutosaveWriter::MakerAutosaveWriter(MakerRecoveryStore store)
    : store_(std::move(store))
{
}

MakerAutosaveWriter::~MakerAutosaveWriter()
{
    {
        const std::scoped_lock lock(mutex_);
        stopping_ = true;
    }
    changed_.notify_all();
    if (worker_.joinable()) {
        worker_.join();
    }
}

void MakerAutosaveWriter::save(
    uint64_t revision,
    MakerRecoverySnapshot snapshot)
{
    const MakerRecoveryKind kind = snapshot.kind();
    submit({ .kind = kind,
             .revision = revision,
             .snapshot = std::move(snapshot) });
}

void MakerAutosaveWriter::discard(MakerRecoveryKind kind, uint64_t revision)
{
    submit({ .kind = kind, .revision = revision });
}

void MakerAutosaveWriter::submit(Request request)
{
    {
        const std::scoped_lock lock(mutex_);
        request.sequence = nextSequence_++;
        waiting_[slotIndex(request.kind)] = std::move(request);
        if (!worker_.joinable()) {
            worker_ = std::thread([this] { workerLoop(); });
        }
    }
    changed_.notify_all();
}

std::vector<MakerAutosaveWriter::Result> MakerAutosaveWriter::takeResults()
{
    const std::scoped_lock lock(mutex_);
    return std::exchange(results_, {});
}

void MakerAutosaveWriter::flush()
{
    std::unique_lock lock(mutex_);
    changed_.wait(
        lock, [this] { return !writing_ && !waiting_[0] && !waiting_[1]; });
}

void MakerAutosaveWriter::workerLoop()
{
    std::unique_lock lock(mutex_);
    while (true) {
        changed_.wait(
            lock, [this] { return stopping_ || waiting_[0] || waiting_[1]; });
        // Waiting requests are completed even when stopping: the last edits
        // before shutdown are the ones a recovery journal exists for.
        std::optional<Request>* next = nullptr;
        for (auto& candidate : waiting_) {
            if (candidate &&
                (!next || candidate->sequence < (*next)->sequence)) {
                next = &candidate;
            }
        }
        if (!next) {
            return;
        }
        Request request = std::move(**next);
        next->reset();
        writing_ = true;
        lock.unlock();

        Result result {
            .kind = request.kind,
            .revision = request.revision,
            .journalPresent = request.snapshot.has_value(),
        };
        try {
            if (request.snapshot) {
                store_.save(*request.snapshot);
            } else {
                store_.discard(request.kind);
            }
        } catch (const std::exception& error) {
            result.error = error.what();
            if (result.error.empty()) {
                result.error = "unknown error";
            }
        } catch (...) {
            result.error = "unknown error";
        }

        lock.lock();
        writing_ = false;
        results_.push_back(std::move(result));
        changed_.notify_all();
    }
}

} // namespace sokoban

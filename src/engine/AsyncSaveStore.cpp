#include "engine/AsyncSaveStore.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace sokoban {

AsyncSaveStore::AsyncSaveStore(
    std::filesystem::path root,
    std::chrono::milliseconds writeDelay)
    : store_(std::move(root))
    , writeDelay_(std::max(writeDelay, std::chrono::milliseconds::zero()))
    , worker_([this] { workerLoop(); })
{
}

AsyncSaveStore::~AsyncSaveStore()
{
    flush();
    {
        const std::scoped_lock lock(mutex_);
        stopping_ = true;
    }
    condition_.notify_all();
    worker_.join();
}

SaveStore::LoadResult AsyncSaveStore::load()
{
    {
        const std::scoped_lock lock(mutex_);
        if (pendingProfile_ || writing_) {
            throw std::logic_error("player profile cannot be loaded while saves are pending");
        }
    }
    SaveStore::LoadResult result = store_.load();
    {
        const std::scoped_lock lock(mutex_);
        status_ = result.message;
    }
    return result;
}

void AsyncSaveStore::requestSave(PlayerProfile profile, Urgency urgency)
{
    {
        const std::scoped_lock lock(mutex_);
        ++requestCount_;
        if (pendingProfile_) {
            ++coalescedRequestCount_;
        } else {
            writeDeadline_ = std::chrono::steady_clock::now() + writeDelay_;
        }
        pendingProfile_ = std::move(profile);
        if (urgency == Urgency::Immediate) {
            forceWrite_ = true;
        }
    }
    condition_.notify_one();
}

void AsyncSaveStore::flush()
{
    std::unique_lock lock(mutex_);
    if (pendingProfile_) {
        forceWrite_ = true;
        condition_.notify_one();
    }
    condition_.wait(lock, [this] {
        return !pendingProfile_ && !writing_;
    });
}

std::string AsyncSaveStore::status() const
{
    const std::scoped_lock lock(mutex_);
    return status_;
}

AsyncSaveStore::Diagnostics AsyncSaveStore::diagnostics() const
{
    const std::scoped_lock lock(mutex_);
    return {
        .requests = requestCount_,
        .completedWrites = completedWriteCount_,
        .coalescedRequests = coalescedRequestCount_,
        .pending = pendingProfile_.has_value(),
        .writing = writing_,
        .lastWriteSucceeded = lastWriteSucceeded_,
    };
}

void AsyncSaveStore::workerLoop()
{
    std::unique_lock lock(mutex_);
    while (true) {
        condition_.wait(lock, [this] {
            return stopping_ || pendingProfile_.has_value();
        });
        if (stopping_ && !pendingProfile_) {
            return;
        }

        while (pendingProfile_ && !forceWrite_) {
            if (condition_.wait_until(lock, writeDeadline_) == std::cv_status::timeout) {
                break;
            }
            if (stopping_) {
                forceWrite_ = true;
            }
        }
        if (!pendingProfile_) {
            continue;
        }

        PlayerProfile profile = std::move(*pendingProfile_);
        pendingProfile_.reset();
        forceWrite_ = false;
        writing_ = true;
        lock.unlock();

        const bool succeeded = store_.save(profile);
        const std::string writeStatus = store_.status();

        lock.lock();
        writing_ = false;
        lastWriteSucceeded_ = succeeded;
        status_ = writeStatus;
        ++completedWriteCount_;
        condition_.notify_all();
    }
}

} // namespace sokoban

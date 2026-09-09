#include "engine/AsyncSaveStore.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace sokoban {

AsyncSaveStore::AsyncSaveStore(
    std::filesystem::path root,
    std::chrono::milliseconds writeDelay,
    const std::string& fileStem,
    ProfileSections sections)
    : writeDelay_(std::max(writeDelay, std::chrono::milliseconds::zero()))
{
    channels_.emplace_back(
        SaveStore(std::move(root), fileStem, sections));
    worker_ = std::thread([this] { workerLoop(); });
}

AsyncSaveStore::~AsyncSaveStore()
{
    (void)flush();
    {
        const std::scoped_lock lock(mutex_);
        stopping_ = true;
    }
    condition_.notify_all();
    worker_.join();
}

bool AsyncSaveStore::FlushResult::allPersisted() const
{
    return std::ranges::all_of(channels, [](const PersistenceResult& result) {
        return result.outcome == PersistenceOutcome::Persisted;
    });
}

const AsyncSaveStore::PersistenceResult&
AsyncSaveStore::FlushResult::forChannel(int channel) const
{
    return channels.at(static_cast<std::size_t>(channel));
}

int AsyncSaveStore::addChannel(
    std::filesystem::path root,
    const std::string& fileStem,
    ProfileSections sections)
{
    const std::scoped_lock lock(mutex_);
    channels_.emplace_back(
        SaveStore(std::move(root), fileStem, sections));
    return static_cast<int>(channels_.size()) - 1;
}

AsyncSaveStore::Channel& AsyncSaveStore::channelAt(int channel)
{
    return channels_.at(static_cast<std::size_t>(channel));
}

const AsyncSaveStore::Channel& AsyncSaveStore::channelAt(int channel) const
{
    return channels_.at(static_cast<std::size_t>(channel));
}

bool AsyncSaveStore::anyPendingLocked() const
{
    return std::ranges::any_of(channels_, [](const Channel& channel) {
        return channel.pending.has_value() && !channel.retryBlocked;
    });
}

AsyncSaveStore::PersistenceResult AsyncSaveStore::persistenceResultLocked(
    int channel) const
{
    const Channel& target = channelAt(channel);
    return {
        .outcome = target.pending || target.writing ||
                !target.lastWriteSucceeded
            ? PersistenceOutcome::RetryableFailure
            : PersistenceOutcome::Persisted,
        .requestedRevision = target.requestedRevision,
        .persistedRevision = target.persistedRevision,
        .message = target.status,
    };
}

AsyncSaveStore::PersistenceResult AsyncSaveStore::replaceChannel(
    int channel,
    std::filesystem::path root,
    const std::string& fileStem,
    ProfileSections sections)
{
    std::unique_lock lock(mutex_);
    Channel& target = channelAt(channel);
    if (target.pending && !target.retryBlocked) {
        target.forceWrite = true;
    }
    condition_.notify_all();
    condition_.wait(lock, [&target] {
        return !target.writing &&
            (!target.pending || target.retryBlocked);
    });
    const PersistenceResult persistence = persistenceResultLocked(channel);
    if (persistence.outcome != PersistenceOutcome::Persisted) {
        return persistence;
    }

    target.store = SaveStore(std::move(root), fileStem, sections);
    target.status.clear();
    target.lastWriteSucceeded = true;
    target.retryBlocked = false;
    target.requestedRevision = 0;
    target.pendingRevision = 0;
    target.persistedRevision = 0;
    return persistence;
}

SaveStore::LoadResult AsyncSaveStore::load(int channel)
{
    {
        const std::scoped_lock lock(mutex_);
        const Channel& target = channelAt(channel);
        if (target.pending || target.writing) {
            throw std::logic_error(
                "player profile cannot be loaded while saves are pending");
        }
    }
    // The store is only mutated by the worker while writing (excluded above)
    // or by replaceChannel (which flushes first), so loading off-lock is safe.
    SaveStore::LoadResult result = channelAt(channel).store.load();
    {
        const std::scoped_lock lock(mutex_);
        channelAt(channel).status = result.message;
    }
    return result;
}

AsyncSaveStore::Revision AsyncSaveStore::requestSave(
    int channel,
    PlayerProfile profile,
    Urgency urgency)
{
    Revision revision = 0;
    {
        const std::scoped_lock lock(mutex_);
        Channel& target = channelAt(channel);
        ++target.requestCount;
        revision = ++target.nextRevision;
        target.requestedRevision = revision;
        if (target.pending) {
            ++target.coalescedRequestCount;
        } else {
            target.deadline = std::chrono::steady_clock::now() + writeDelay_;
        }
        target.pending = std::move(profile);
        target.pendingRevision = revision;
        target.retryBlocked = false;
        if (urgency == Urgency::Immediate) {
            target.forceWrite = true;
        }
    }
    condition_.notify_one();
    return revision;
}

bool AsyncSaveStore::retryFailedSave(int channel)
{
    {
        const std::scoped_lock lock(mutex_);
        Channel& target = channelAt(channel);
        if (!target.pending || !target.retryBlocked) {
            return false;
        }
        target.retryBlocked = false;
        target.forceWrite = true;
    }
    condition_.notify_one();
    return true;
}

SaveStore::DeleteResult AsyncSaveStore::deleteProfile(int channel)
{
    std::unique_lock lock(mutex_);
    Channel& target = channelAt(channel);
    condition_.wait(lock, [&target] { return !target.writing; });
    SaveStore::DeleteResult result = target.store.deleteProfile();
    target.status = result.message;
    if (result.succeeded) {
        target.pending.reset();
        target.forceWrite = false;
        target.retryBlocked = false;
        target.lastWriteSucceeded = true;
        target.requestedRevision = 0;
        target.pendingRevision = 0;
        target.persistedRevision = 0;
    }
    condition_.notify_all();
    return result;
}

AsyncSaveStore::FlushResult AsyncSaveStore::flush()
{
    std::unique_lock lock(mutex_);
    for (Channel& channel : channels_) {
        if (channel.pending && !channel.retryBlocked) {
            channel.forceWrite = true;
        }
    }
    condition_.notify_all();
    condition_.wait(lock, [this] {
        return std::ranges::none_of(channels_, [](const Channel& channel) {
            return (channel.pending.has_value() && !channel.retryBlocked) ||
                channel.writing;
        });
    });
    FlushResult result;
    result.channels.reserve(channels_.size());
    for (std::size_t channel = 0; channel < channels_.size(); ++channel) {
        result.channels.push_back(
            persistenceResultLocked(static_cast<int>(channel)));
    }
    return result;
}

std::string AsyncSaveStore::status(int channel) const
{
    const std::scoped_lock lock(mutex_);
    return channelAt(channel).status;
}

AsyncSaveStore::Diagnostics AsyncSaveStore::diagnostics(int channel) const
{
    const std::scoped_lock lock(mutex_);
    const Channel& target = channelAt(channel);
    return {
        .requests = target.requestCount,
        .completedWrites = target.completedWriteCount,
        .coalescedRequests = target.coalescedRequestCount,
        .pending = target.pending.has_value(),
        .writing = target.writing,
        .lastWriteSucceeded = target.lastWriteSucceeded,
    };
}

std::filesystem::path AsyncSaveStore::primaryPath(int channel) const
{
    const std::scoped_lock lock(mutex_);
    return channelAt(channel).store.primaryPath();
}

void AsyncSaveStore::workerLoop()
{
    std::unique_lock lock(mutex_);
    while (true) {
        condition_.wait(lock, [this] {
            return stopping_ || anyPendingLocked();
        });
        if (stopping_ && !anyPendingLocked()) {
            return;
        }

        // Pick a channel to write: one that is forced, at shutdown, or past
        // its coalescing deadline. Otherwise sleep until the soonest deadline
        // and re-evaluate (a new request or stop may arrive meanwhile).
        const auto now = std::chrono::steady_clock::now();
        int due = -1;
        std::chrono::steady_clock::time_point earliest =
            std::chrono::steady_clock::time_point::max();
        for (std::size_t i = 0; i < channels_.size(); ++i) {
            Channel& channel = channels_[i];
            if (!channel.pending || channel.retryBlocked) {
                continue;
            }
            if (stopping_ || channel.forceWrite || now >= channel.deadline) {
                due = static_cast<int>(i);
                break;
            }
            earliest = std::min(earliest, channel.deadline);
        }
        if (due < 0) {
            condition_.wait_until(lock, earliest);
            continue;
        }

        Channel& channel = channels_[static_cast<std::size_t>(due)];
        PlayerProfile profile = std::move(*channel.pending);
        const Revision revision = channel.pendingRevision;
        channel.pending.reset();
        channel.forceWrite = false;
        channel.writing = true;
        lock.unlock();

        const bool succeeded = channel.store.save(profile);
        const std::string writeStatus = channel.store.status();

        lock.lock();
        channel.writing = false;
        channel.lastWriteSucceeded = succeeded;
        channel.status = writeStatus;
        if (succeeded) {
            channel.persistedRevision = revision;
        }
        if (!succeeded && !channel.pending) {
            channel.pending = std::move(profile);
            channel.pendingRevision = revision;
            channel.retryBlocked = true;
        }
        ++channel.completedWriteCount;
        condition_.notify_all();
    }
}

} // namespace sokoban

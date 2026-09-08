#include "engine/AsyncSaveStore.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace sokoban {

AsyncSaveStore::AsyncSaveStore(
    std::filesystem::path root,
    std::chrono::milliseconds writeDelay,
    std::string fileStem,
    ProfileSections sections)
    : writeDelay_(std::max(writeDelay, std::chrono::milliseconds::zero()))
{
    channels_.emplace_back(
        SaveStore(std::move(root), std::move(fileStem), sections));
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

int AsyncSaveStore::addChannel(
    std::filesystem::path root,
    std::string fileStem,
    ProfileSections sections)
{
    const std::scoped_lock lock(mutex_);
    channels_.emplace_back(
        SaveStore(std::move(root), std::move(fileStem), sections));
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

bool AsyncSaveStore::replaceChannel(
    int channel,
    std::filesystem::path root,
    std::string fileStem,
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
    if (target.pending || !target.lastWriteSucceeded) {
        return false;
    }

    target.store = SaveStore(std::move(root), std::move(fileStem), sections);
    target.status.clear();
    target.lastWriteSucceeded = true;
    target.retryBlocked = false;
    return true;
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

void AsyncSaveStore::requestSave(int channel, PlayerProfile profile, Urgency urgency)
{
    {
        const std::scoped_lock lock(mutex_);
        Channel& target = channelAt(channel);
        ++target.requestCount;
        if (target.pending) {
            ++target.coalescedRequestCount;
        } else {
            target.deadline = std::chrono::steady_clock::now() + writeDelay_;
        }
        target.pending = std::move(profile);
        target.retryBlocked = false;
        if (urgency == Urgency::Immediate) {
            target.forceWrite = true;
        }
    }
    condition_.notify_one();
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
    }
    condition_.notify_all();
    return result;
}

bool AsyncSaveStore::flush()
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
    return std::ranges::none_of(channels_, [](const Channel& channel) {
        return channel.pending.has_value() || !channel.lastWriteSucceeded;
    });
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

const std::filesystem::path& AsyncSaveStore::primaryPath(int channel) const
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
        if (!succeeded && !channel.pending) {
            channel.pending = std::move(profile);
            channel.retryBlocked = true;
        }
        ++channel.completedWriteCount;
        condition_.notify_all();
    }
}

} // namespace sokoban

#pragma once

#include "engine/Log.hpp"

#include <chrono>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <functional>
#include <future>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace sokoban {

enum class LoadState {
    Unrequested,
    Queued,
    Loading,
    CpuReady,
    Uploading,
    Ready,
    Failed,
};

inline void throwIfFailed(
    LoadState state,
    const std::exception_ptr& failure,
    const std::filesystem::path& path,
    const char* kind)
{
    if (state != LoadState::Failed) {
        return;
    }
    try {
        if (failure) {
            std::rethrow_exception(failure);
        }
    } catch (const std::exception& error) {
        throw std::runtime_error(
            "Failed to load " + std::string(kind) + " asset '" +
            path.string() + "': " + error.what());
    }
    throw std::runtime_error(
        "Failed to load " + std::string(kind) + " asset '" +
        path.string() + "'");
}

enum class PublishGate { Stop, Proceed };

// Owns the asynchronous payload and every state transition shared by model,
// texture, and animation publication. GPU resources remain in their typed
// slots; this class owns the part that must survive an admission refusal and
// must be released exactly once when upload begins, publication fails, or a
// resident asset is retired.
template <typename Payload>
class PreparedAssetPublication {
public:
    PreparedAssetPublication() = default;

    PreparedAssetPublication(const PreparedAssetPublication&) = delete;
    PreparedAssetPublication& operator=(const PreparedAssetPublication&) =
        delete;
    PreparedAssetPublication(PreparedAssetPublication&&) = default;
    PreparedAssetPublication& operator=(
        PreparedAssetPublication&&) = default;

    [[nodiscard]] LoadState state() const { return state_; }
    [[nodiscard]] uint64_t preparedBytes() const { return preparedBytes_; }

    // Returns true when the request entered or remained in the queue. A
    // request for any later phase is already owned by the loader and needs no
    // second scheduler entry.
    [[nodiscard]] bool queue()
    {
        if (state_ != LoadState::Unrequested &&
            state_ != LoadState::Queued) {
            return false;
        }
        state_ = LoadState::Queued;
        return true;
    }

    void startDecoding(std::future<Payload> future)
    {
        requireState(LoadState::Queued, "start decoding");
        if (!future.valid()) {
            throw std::invalid_argument(
                "Cannot start asset decoding with an invalid future");
        }
        future_ = std::move(future);
        state_ = LoadState::Loading;
    }

    [[nodiscard]] bool canCollectDecoded(bool wait) const
    {
        return state_ == LoadState::Loading && future_.valid() &&
            (wait || future_.wait_for(std::chrono::seconds(0)) ==
                    std::future_status::ready);
    }

    [[nodiscard]] bool readyForPublication() const
    {
        return state_ == LoadState::CpuReady || canCollectDecoded(false);
    }

    template <typename SizeFunction>
    void collectDecoded(
        uint64_t estimatedBytes,
        std::string_view assetKind,
        SizeFunction&& sizeFunction)
    {
        requireState(LoadState::Loading, "collect decoded payload");
        if (!future_.valid()) {
            throw std::logic_error(
                "Cannot collect an asset without a decode future");
        }
        prepared_ = future_.get();
        const uint64_t actualBytes = std::invoke(
            std::forward<SizeFunction>(sizeFunction), *prepared_);
        if (estimatedBytes != 0 && actualBytes > estimatedBytes) {
            throw std::runtime_error(
                std::string(assetKind) +
                " decoded payload exceeded its prepared-memory reservation");
        }
        preparedBytes_ = actualBytes;
        state_ = LoadState::CpuReady;
    }

    [[nodiscard]] Payload& prepared()
    {
        requirePrepared();
        return *prepared_;
    }

    [[nodiscard]] const Payload& prepared() const
    {
        requirePrepared();
        return *prepared_;
    }

    // Admission refusal deliberately calls neither transition: the payload
    // remains CpuReady and the next publication pass retries the same object.
    void beginUpload()
    {
        requirePrepared();
        releasePrepared();
        state_ = LoadState::Uploading;
    }

    // Animation clips publish directly into their resident controller and do
    // not have a separate GPU-upload phase.
    void publishResident()
    {
        requirePrepared();
        releasePrepared();
        state_ = LoadState::Ready;
    }

    void finishUpload()
    {
        requireState(LoadState::Uploading, "finish upload");
        state_ = LoadState::Ready;
    }

    void cancelQueued()
    {
        requireState(LoadState::Queued, "cancel queued request");
        reset();
    }

    void retireResident()
    {
        requireState(LoadState::Ready, "retire resident asset");
        reset();
    }

    // Called from a catch block after asset-specific cleanup. It clears CPU
    // ownership before making the failure observable and preserves the
    // blocking caller's rethrow behavior.
    void fail(
        const std::filesystem::path& path,
        const char* kind,
        const char* phase,
        bool wait)
    {
        failure_ = std::current_exception();
        future_ = {};
        releasePrepared();
        state_ = LoadState::Failed;
        if (wait) {
            throwIfFailed(state_, failure_, path, kind);
        }
        log::error(log::Category::Assets)
            << "Background " << kind << ' ' << phase << " failed: "
            << path.string();
    }

    [[nodiscard]] PublishGate gate(
        const std::filesystem::path& path,
        const char* kind,
        bool wait) const
    {
        if (state_ == LoadState::Ready ||
            state_ == LoadState::Uploading) {
            return PublishGate::Stop;
        }
        if (state_ == LoadState::Failed) {
            if (wait) {
                throwIfFailed(state_, failure_, path, kind);
            }
            return PublishGate::Stop;
        }
        if (state_ == LoadState::Unrequested ||
            state_ == LoadState::Queued) {
            return PublishGate::Stop;
        }
        return PublishGate::Proceed;
    }

private:
    void requireState(LoadState expected, const char* action) const
    {
        if (state_ != expected) {
            throw std::logic_error(
                "Cannot " + std::string(action) +
                " from the current asset publication state");
        }
    }

    void requirePrepared() const
    {
        requireState(LoadState::CpuReady, "access prepared payload");
        if (!prepared_) {
            throw std::logic_error(
                "CPU-ready asset publication has no prepared payload");
        }
    }

    void releasePrepared()
    {
        prepared_.reset();
        preparedBytes_ = 0;
    }

    void reset()
    {
        state_ = LoadState::Unrequested;
        future_ = {};
        releasePrepared();
        failure_ = {};
    }

    LoadState state_ = LoadState::Unrequested;
    std::future<Payload> future_;
    std::optional<Payload> prepared_;
    uint64_t preparedBytes_ = 0;
    std::exception_ptr failure_;
};

} // namespace sokoban

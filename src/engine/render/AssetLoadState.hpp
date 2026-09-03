#pragma once

#include "engine/Log.hpp"

#include <exception>
#include <filesystem>
#include <stdexcept>
#include <string>

namespace sokoban {

// The load-state machine every asset kind runs, and the two decisions that
// bracket a publication attempt.
//
// The review found this implemented three times inside VulkanModelResources -
// once per asset kind, each copy slightly different in shape without being
// different in behaviour. The shared edges were merged there first; this is
// where they live now, as free functions over anything with the four fields
// they read. Nothing here knows what an asset is, which is the point: models,
// textures and animations disagree about almost everything else.

enum class LoadState {
    Unrequested,
    Queued,
    Loading,
    CpuReady,
    Uploading,
    Ready,
    Failed,
};

// Rethrows a slot's stored failure as a message naming the asset, or does
// nothing when the slot did not fail.
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

// Whether a publication attempt should do any work at all.
//
// All three publish functions opened with the same ladder, and the three copies
// had drifted in shape without drifting in behaviour: models fell through on
// Uploading and returned false further down, textures rejected it in the
// ladder, animations never reach it. Checked exhaustively over every load state
// and both values of `wait` before merging - forty reachable cases, all three
// reproduced exactly.
enum class PublishGate { Stop, Proceed };

template <typename Slot>
PublishGate publishGate(
    const Slot& slot,
    const std::filesystem::path& path,
    const char* kind,
    bool wait)
{
    // Uploading belongs here with Ready: its bytes are already charged and its
    // fence is already in flight, so there is nothing a second attempt could
    // usefully do.
    if (slot.state == LoadState::Ready || slot.state == LoadState::Uploading) {
        return PublishGate::Stop;
    }
    if (slot.state == LoadState::Failed) {
        if (wait) {
            throwIfFailed(slot.state, slot.failure, path, kind);
        }
        return PublishGate::Stop;
    }
    if (slot.state == LoadState::Unrequested ||
        slot.state == LoadState::Queued) {
        return PublishGate::Stop;
    }
    return PublishGate::Proceed;
}

// The tail every publication failure shares. Any cleanup particular to one
// asset kind happens at the call site before this; what is here is the part
// that must never differ - remember the exception, mark the slot failed,
// rethrow for a caller that is waiting, and otherwise say so in the log.
template <typename Slot>
void recordPublishFailure(
    Slot& slot,
    const std::filesystem::path& path,
    const char* kind,
    const char* phase,
    bool wait)
{
    slot.failure = std::current_exception();
    slot.state = LoadState::Failed;
    if (wait) {
        throwIfFailed(slot.state, slot.failure, path, kind);
    }
    log::error(log::Category::Assets)
        << "Background " << kind << " " << phase << " failed: "
        << path.string();
}

} // namespace sokoban

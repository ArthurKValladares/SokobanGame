#include "engine/render/UploadRingAllocator.hpp"

#include <limits>
#include <stdexcept>

namespace sokoban {

UploadRingAllocator::UploadRingAllocator(uint64_t capacity)
    : capacity_(capacity)
{
}

std::optional<UploadRingAllocator::Placement> UploadRingAllocator::place(
    uint64_t size,
    uint64_t alignment,
    uint64_t head,
    uint64_t tail,
    uint64_t usedBytes) const
{
    Placement placement;
    const uint64_t alignedHead = alignUp(head, alignment);

    if (head >= tail) {
        if (alignedHead <= capacity_ && size <= capacity_ - alignedHead) {
            placement.offset = alignedHead;
            placement.consumed = alignedHead - head + size;
            placement.end = alignedHead + size;
        } else if (size <= tail) {
            placement.offset = 0;
            placement.consumed = capacity_ - head + size;
            placement.end = size;
        } else {
            return std::nullopt;
        }
    } else if (alignedHead <= tail && size <= tail - alignedHead) {
        placement.offset = alignedHead;
        placement.consumed = alignedHead - head + size;
        placement.end = alignedHead + size;
    } else {
        return std::nullopt;
    }
    if (placement.consumed > capacity_ - usedBytes) {
        return std::nullopt;
    }
    return placement;
}

std::optional<UploadRingAllocator::Reservation> UploadRingAllocator::reserve(
    uint64_t size,
    uint64_t alignment)
{
    if (size == 0 || size > capacity_) {
        return std::nullopt;
    }
    if (entries_.empty()) {
        head_ = 0;
        tail_ = 0;
        usedBytes_ = 0;
    }

    const uint64_t previousHead = head_;
    const uint64_t previousUsed = usedBytes_;
    const std::optional<Placement> placement =
        place(size, alignment, head_, tail_, usedBytes_);
    if (!placement) {
        return std::nullopt;
    }

    Reservation reservation {
        .id = nextId_++,
        .offset = placement->offset,
        .size = size,
    };
    const uint64_t end = placement->end == capacity_ ? 0 : placement->end;
    entries_.push_back({
        .reservation = reservation,
        .previousHead = previousHead,
        .previousUsed = previousUsed,
        .consumed = placement->consumed,
        .end = end,
    });
    head_ = end;
    usedBytes_ += placement->consumed;
    return reservation;
}

bool UploadRingAllocator::canReserve(uint64_t size, uint64_t alignment) const
{
    if (size == 0 || size > capacity_) {
        return false;
    }
    // reserve() restarts an empty ring at offset zero before placing.
    return entries_.empty()
        ? place(size, alignment, 0, 0, 0).has_value()
        : place(size, alignment, head_, tail_, usedBytes_).has_value();
}

void UploadRingAllocator::commit(Reservation reservation)
{
    Entry* entry = find(reservation.id);
    if (entry == nullptr || entry->reservation != reservation || entry->committed) {
        throw std::invalid_argument("Invalid upload-ring submission");
    }
    entry->committed = true;
}

void UploadRingAllocator::complete(Reservation reservation)
{
    Entry* entry = find(reservation.id);
    if (entry == nullptr || entry->reservation != reservation || !entry->committed) {
        throw std::invalid_argument("Invalid upload-ring completion");
    }
    entry->completed = true;
    reclaimCompleted();
}

void UploadRingAllocator::abandon(Reservation reservation)
{
    if (entries_.empty()) {
        throw std::invalid_argument("Invalid upload-ring abandonment");
    }
    Entry& entry = entries_.back();
    if (entry.reservation != reservation || entry.committed) {
        throw std::invalid_argument("Only the latest unsubmitted upload may be abandoned");
    }
    head_ = entry.previousHead;
    usedBytes_ = entry.previousUsed;
    entries_.pop_back();
    if (entries_.empty()) {
        tail_ = head_;
    }
}

uint64_t UploadRingAllocator::alignUp(uint64_t value, uint64_t alignment)
{
    if (alignment == 0) {
        throw std::invalid_argument("Upload-ring alignment must be non-zero");
    }
    const uint64_t remainder = value % alignment;
    if (remainder == 0) {
        return value;
    }
    if (value > std::numeric_limits<uint64_t>::max() - (alignment - remainder)) {
        throw std::overflow_error("Upload-ring alignment overflow");
    }
    return value + alignment - remainder;
}

UploadRingAllocator::Entry* UploadRingAllocator::find(uint64_t id)
{
    for (Entry& entry : entries_) {
        if (entry.reservation.id == id) {
            return &entry;
        }
    }
    return nullptr;
}

void UploadRingAllocator::reclaimCompleted()
{
    while (!entries_.empty() && entries_.front().completed) {
        const Entry entry = entries_.front();
        entries_.pop_front();
        tail_ = entry.end;
        if (entries_.empty()) {
            head_ = 0;
            tail_ = 0;
            usedBytes_ = 0;
        } else {
            usedBytes_ -= entry.consumed;
        }
    }
}

} // namespace sokoban

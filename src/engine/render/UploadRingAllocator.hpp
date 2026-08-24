#pragma once

#include <cstdint>
#include <deque>
#include <optional>

namespace sokoban {

// Vulkan-independent state machine for a persistently mapped upload ring.
// A reservation is reclaimed only after it has been submitted and its GPU
// fence is complete. Completion notifications may arrive out of order.
class UploadRingAllocator {
public:
    struct Reservation {
        uint64_t id = 0;
        uint64_t offset = 0;
        uint64_t size = 0;

        [[nodiscard]] bool valid() const { return id != 0; }
        [[nodiscard]] bool operator==(const Reservation&) const = default;
    };

    explicit UploadRingAllocator(uint64_t capacity = 0);

    [[nodiscard]] std::optional<Reservation> reserve(
        uint64_t size,
        uint64_t alignment);
    void commit(Reservation reservation);
    void complete(Reservation reservation);
    // Valid only for the latest reservation before it has been submitted.
    void abandon(Reservation reservation);

    [[nodiscard]] uint64_t capacity() const { return capacity_; }
    [[nodiscard]] uint64_t usedBytes() const { return usedBytes_; }
    [[nodiscard]] uint64_t inFlightCount() const
    {
        return static_cast<uint64_t>(entries_.size());
    }

private:
    struct Entry {
        Reservation reservation {};
        uint64_t previousHead = 0;
        uint64_t previousUsed = 0;
        uint64_t consumed = 0;
        uint64_t end = 0;
        bool committed = false;
        bool completed = false;
    };

    [[nodiscard]] static uint64_t alignUp(uint64_t value, uint64_t alignment);
    [[nodiscard]] Entry* find(uint64_t id);
    void reclaimCompleted();

    uint64_t capacity_ = 0;
    uint64_t head_ = 0;
    uint64_t tail_ = 0;
    uint64_t usedBytes_ = 0;
    uint64_t nextId_ = 1;
    std::deque<Entry> entries_;
};

} // namespace sokoban

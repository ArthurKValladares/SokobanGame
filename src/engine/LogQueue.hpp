#pragma once

#include "engine/Log.hpp"

#include <chrono>
#include <cstddef>
#include <deque>
#include <optional>
#include <string>
#include <vector>

namespace sokoban::log::detail {

struct Record {
    std::chrono::system_clock::time_point timestamp;
    Level level = Level::Info;
    Category category = Category::General;
    std::string message;
};

struct PushResult {
    bool accepted = false;
    std::optional<Category> droppedCategory;
};

// Mutex-free bounded-storage policy; ProcessLogger serializes calls around it.
// Errors displace the oldest lower-severity record when possible. Every other
// full-queue insertion drops the incoming record.
class BoundedQueue {
public:
    explicit BoundedQueue(std::size_t capacity);

    [[nodiscard]] PushResult push(Record record);
    [[nodiscard]] std::vector<Record> takeAll();
    [[nodiscard]] std::size_t size() const { return records_.size(); }
    [[nodiscard]] std::size_t capacity() const { return capacity_; }
    [[nodiscard]] bool empty() const { return records_.empty(); }

private:
    std::size_t capacity_;
    std::deque<Record> records_;
};

} // namespace sokoban::log::detail

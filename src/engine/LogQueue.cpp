#include "engine/LogQueue.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace sokoban::log::detail {

BoundedQueue::BoundedQueue(std::size_t capacity)
    : capacity_(capacity)
{
    if (capacity == 0) {
        throw std::invalid_argument(
            "log queue capacity must be nonzero");
    }
}

PushResult BoundedQueue::push(Record record)
{
    if (records_.size() < capacity_) {
        records_.push_back(std::move(record));
        return { .accepted = true };
    }

    if (record.level == Level::Error) {
        const auto replace = std::ranges::find_if(
            records_,
            [](const Record& queued) {
                return queued.level != Level::Error;
            });
        if (replace != records_.end()) {
            const Category dropped = replace->category;
            records_.erase(replace);
            records_.push_back(std::move(record));
            return {
                .accepted = true,
                .droppedCategory = dropped,
            };
        }
    }

    return {
        .accepted = false,
        .droppedCategory = record.category,
    };
}

std::vector<Record> BoundedQueue::takeAll()
{
    std::vector<Record> result;
    result.reserve(records_.size());
    while (!records_.empty()) {
        result.push_back(std::move(records_.front()));
        records_.pop_front();
    }
    return result;
}

} // namespace sokoban::log::detail

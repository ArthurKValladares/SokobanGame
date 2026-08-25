#include "engine/render/OpaqueDrawSorter.hpp"

#include <algorithm>

namespace sokoban {

std::vector<OpaqueDrawBatch> sortOpaqueDraws(
    std::vector<OpaqueDrawSortItem>& items)
{
    std::sort(
        items.begin(), items.end(),
        [](const OpaqueDrawSortItem& left, const OpaqueDrawSortItem& right) {
            return left.key < right.key;
        });

    std::vector<OpaqueDrawBatch> result;
    for (std::size_t first = 0; first < items.size();) {
        std::size_t end = first + 1;
        if (items[first].instancable) {
            while (end < items.size() && items[end].instancable &&
                   items[end].key == items[first].key) {
                ++end;
            }
        }
        result.push_back({ .firstItem = first, .itemCount = end - first });
        first = end;
    }
    return result;
}

} // namespace sokoban

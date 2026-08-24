#pragma once

#include <vulkan/vulkan.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <vector>

namespace sokoban {

// The Vulkan driver owns the contents of a pipeline-cache payload. This
// envelope only identifies which driver/device may safely receive it and lets
// the game reject malformed data before passing it to Vulkan.
struct VulkanPipelineCacheIdentity {
    uint32_t vendorId = 0;
    uint32_t deviceId = 0;
    std::array<uint8_t, VK_UUID_SIZE> uuid {};
};

[[nodiscard]] std::vector<std::byte> encodeVulkanPipelineCacheFile(
    const VulkanPipelineCacheIdentity& identity,
    std::span<const std::byte> payload);
[[nodiscard]] std::optional<std::vector<std::byte>>
decodeVulkanPipelineCacheFile(
    std::span<const std::byte> file,
    const VulkanPipelineCacheIdentity& expectedIdentity);

class VulkanPipelineCache {
public:
    VulkanPipelineCache() = default;
    ~VulkanPipelineCache();

    VulkanPipelineCache(const VulkanPipelineCache&) = delete;
    VulkanPipelineCache& operator=(const VulkanPipelineCache&) = delete;

    void create(
        VkDevice device,
        const VkPhysicalDeviceProperties& physicalDeviceProperties,
        std::filesystem::path path);
    // Call only after the device is idle, so all pipeline compilation work is
    // represented in the cache snapshot. Persistence failure is logged and
    // deliberately never turns a clean renderer shutdown into a failure.
    void persist() noexcept;
    void destroy() noexcept;

    [[nodiscard]] VkPipelineCache handle() const { return cache_; }

private:
    [[nodiscard]] std::optional<std::vector<std::byte>> loadPayload() const;

    VkDevice device_ = VK_NULL_HANDLE;
    VkPipelineCache cache_ = VK_NULL_HANDLE;
    VulkanPipelineCacheIdentity identity_ {};
    std::filesystem::path path_;
};

} // namespace sokoban

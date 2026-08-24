#include "engine/render/VulkanPipelineCache.hpp"

#include "engine/AtomicFile.hpp"
#include "engine/Log.hpp"
#include "engine/render/VulkanDebugUtils.hpp"
#include "engine/render/VulkanResourceUtils.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>

namespace sokoban {
namespace {

constexpr std::array<std::byte, 8> pipelineCacheMagic {
    static_cast<std::byte>('S'), static_cast<std::byte>('O'),
    static_cast<std::byte>('K'), static_cast<std::byte>('O'),
    static_cast<std::byte>('B'), static_cast<std::byte>('A'),
    static_cast<std::byte>('N'), static_cast<std::byte>('P'),
};
constexpr uint32_t pipelineCacheFormatVersion = 1;
constexpr std::size_t maximumPayloadBytes = 64U * 1024U * 1024U;
constexpr std::size_t headerBytes = pipelineCacheMagic.size() +
    sizeof(uint32_t) * 3 + VK_UUID_SIZE + sizeof(uint64_t) * 2;

template <typename Integer>
void appendLittleEndian(std::vector<std::byte>& output, Integer value)
{
    static_assert(std::is_unsigned_v<Integer>);
    for (std::size_t index = 0; index < sizeof(Integer); ++index) {
        output.push_back(static_cast<std::byte>(
            (value >> (index * 8U)) & static_cast<Integer>(0xffU)));
    }
}

template <typename Integer>
bool readLittleEndian(
    std::span<const std::byte> input,
    std::size_t& offset,
    Integer& value)
{
    static_assert(std::is_unsigned_v<Integer>);
    if (offset > input.size() || input.size() - offset < sizeof(Integer)) {
        return false;
    }
    value = 0;
    for (std::size_t index = 0; index < sizeof(Integer); ++index) {
        value |= static_cast<Integer>(std::to_integer<uint8_t>(input[offset++]))
            << (index * 8U);
    }
    return true;
}

uint64_t payloadChecksum(std::span<const std::byte> payload)
{
    uint64_t checksum = 14695981039346656037ULL;
    for (std::byte byte : payload) {
        checksum ^= std::to_integer<uint8_t>(byte);
        checksum *= 1099511628211ULL;
    }
    return checksum;
}

VulkanPipelineCacheIdentity cacheIdentity(
    const VkPhysicalDeviceProperties& properties)
{
    VulkanPipelineCacheIdentity identity {
        .vendorId = properties.vendorID,
        .deviceId = properties.deviceID,
    };
    std::copy_n(
        properties.pipelineCacheUUID,
        identity.uuid.size(),
        identity.uuid.begin());
    return identity;
}

std::vector<std::byte> readFile(const std::filesystem::path& path)
{
    std::error_code error;
    const uintmax_t size = std::filesystem::file_size(path, error);
    if (error) {
        throw std::system_error(error, "cannot inspect " + path.string());
    }
    if (size > headerBytes + maximumPayloadBytes ||
        size > std::numeric_limits<std::size_t>::max()) {
        throw std::runtime_error("pipeline cache file is too large: " + path.string());
    }

    std::vector<std::byte> result(static_cast<std::size_t>(size));
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        throw std::runtime_error("cannot open " + path.string());
    }
    stream.read(
        reinterpret_cast<char*>(result.data()),
        static_cast<std::streamsize>(result.size()));
    if (!stream || stream.gcount() != static_cast<std::streamsize>(result.size())) {
        throw std::runtime_error("cannot read " + path.string());
    }
    return result;
}

} // namespace

std::vector<std::byte> encodeVulkanPipelineCacheFile(
    const VulkanPipelineCacheIdentity& identity,
    std::span<const std::byte> payload)
{
    if (payload.size() > maximumPayloadBytes) {
        throw std::invalid_argument("pipeline cache payload exceeds size limit");
    }

    std::vector<std::byte> file;
    file.reserve(headerBytes + payload.size());
    file.insert(file.end(), pipelineCacheMagic.begin(), pipelineCacheMagic.end());
    appendLittleEndian(file, pipelineCacheFormatVersion);
    appendLittleEndian(file, identity.vendorId);
    appendLittleEndian(file, identity.deviceId);
    for (uint8_t byte : identity.uuid) {
        file.push_back(static_cast<std::byte>(byte));
    }
    appendLittleEndian(file, static_cast<uint64_t>(payload.size()));
    appendLittleEndian(file, payloadChecksum(payload));
    file.insert(file.end(), payload.begin(), payload.end());
    return file;
}

std::optional<std::vector<std::byte>> decodeVulkanPipelineCacheFile(
    std::span<const std::byte> file,
    const VulkanPipelineCacheIdentity& expectedIdentity)
{
    if (file.size() < headerBytes ||
        !std::equal(pipelineCacheMagic.begin(), pipelineCacheMagic.end(), file.begin())) {
        return std::nullopt;
    }

    std::size_t offset = pipelineCacheMagic.size();
    uint32_t version = 0;
    uint32_t vendorId = 0;
    uint32_t deviceId = 0;
    if (!readLittleEndian(file, offset, version) ||
        !readLittleEndian(file, offset, vendorId) ||
        !readLittleEndian(file, offset, deviceId) ||
        version != pipelineCacheFormatVersion ||
        vendorId != expectedIdentity.vendorId ||
        deviceId != expectedIdentity.deviceId) {
        return std::nullopt;
    }

    std::array<uint8_t, VK_UUID_SIZE> uuid {};
    for (uint8_t& byte : uuid) {
        if (offset == file.size()) {
            return std::nullopt;
        }
        byte = std::to_integer<uint8_t>(file[offset++]);
    }
    if (uuid != expectedIdentity.uuid) {
        return std::nullopt;
    }

    uint64_t payloadSize = 0;
    uint64_t checksum = 0;
    if (!readLittleEndian(file, offset, payloadSize) ||
        !readLittleEndian(file, offset, checksum) ||
        payloadSize > maximumPayloadBytes ||
        payloadSize != file.size() - offset ||
        payloadChecksum(file.subspan(offset)) != checksum) {
        return std::nullopt;
    }
    return std::vector<std::byte>(
        file.begin() + static_cast<std::ptrdiff_t>(offset), file.end());
}

VulkanPipelineCache::~VulkanPipelineCache()
{
    destroy();
}

void VulkanPipelineCache::create(
    VkDevice device,
    const VkPhysicalDeviceProperties& physicalDeviceProperties,
    std::filesystem::path path)
{
    destroy();
    device_ = device;
    identity_ = cacheIdentity(physicalDeviceProperties);
    path_ = std::move(path);

    std::optional<std::vector<std::byte>> initialPayload = loadPayload();
    VkPipelineCacheCreateInfo createInfo {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO,
        .initialDataSize = initialPayload ? initialPayload->size() : 0U,
        .pInitialData = initialPayload ? initialPayload->data() : nullptr,
    };
    VkResult result = vkCreatePipelineCache(device_, &createInfo, nullptr, &cache_);
    vkCheck(result, "vkCreatePipelineCache failed");
    vulkanDebug::setObjectName(
        device_, VK_OBJECT_TYPE_PIPELINE_CACHE, cache_, "Sokoban pipeline cache");
}

void VulkanPipelineCache::persist() noexcept
{
    if (!device_ || !cache_ || path_.empty()) {
        return;
    }

    try {
        std::size_t payloadSize = 0;
        vkCheck(
            vkGetPipelineCacheData(device_, cache_, &payloadSize, nullptr),
            "vkGetPipelineCacheData size query failed");
        if (payloadSize > maximumPayloadBytes) {
            throw std::runtime_error("pipeline cache payload exceeds size limit");
        }
        std::vector<std::byte> payload(payloadSize);
        vkCheck(
            vkGetPipelineCacheData(
                device_, cache_, &payloadSize,
                payload.empty() ? nullptr : payload.data()),
            "vkGetPipelineCacheData failed");
        payload.resize(payloadSize);
        const std::vector<std::byte> file = encodeVulkanPipelineCacheFile(
            identity_, payload);
        if (!path_.parent_path().empty()) {
            std::filesystem::create_directories(path_.parent_path());
        }
        atomicFile::write(
            path_,
            std::string_view(
                reinterpret_cast<const char*>(file.data()), file.size()));
        log::debug(log::Category::Rendering)
            << "Persisted Vulkan pipeline cache (" << payload.size() << " bytes)";
    } catch (const std::exception& error) {
        log::warning(log::Category::Rendering)
            << "Vulkan pipeline cache was not persisted: " << error.what();
    }
}

void VulkanPipelineCache::destroy() noexcept
{
    if (cache_) {
        vkDestroyPipelineCache(device_, cache_, nullptr);
        cache_ = VK_NULL_HANDLE;
    }
    device_ = VK_NULL_HANDLE;
    identity_ = {};
    path_.clear();
}

std::optional<std::vector<std::byte>> VulkanPipelineCache::loadPayload() const
{
    try {
        std::error_code error;
        const bool exists = std::filesystem::exists(path_, error);
        if (error) {
            throw std::system_error(error, "cannot inspect " + path_.string());
        }
        if (!exists) {
            return std::nullopt;
        }
        const std::vector<std::byte> file = readFile(path_);
        std::optional<std::vector<std::byte>> payload =
            decodeVulkanPipelineCacheFile(file, identity_);
        if (!payload) {
            log::warning(log::Category::Rendering)
                << "Ignoring incompatible or corrupt Vulkan pipeline cache";
        }
        return payload;
    } catch (const std::exception& error) {
        log::warning(log::Category::Rendering)
            << "Ignoring unreadable Vulkan pipeline cache: " << error.what();
        return std::nullopt;
    }
}

} // namespace sokoban

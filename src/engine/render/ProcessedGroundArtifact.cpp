#include "engine/render/ProcessedGroundArtifact.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

namespace sokoban {
namespace {

constexpr std::array<std::byte, 8> identifier {
    std::byte { 'S' }, std::byte { 'K' }, std::byte { 'G' }, std::byte { 'R' },
    std::byte { 'I' }, std::byte { 'M' }, std::byte { '\r' }, std::byte { '\n' },
};
constexpr std::size_t headerBytes = 56;
constexpr std::size_t checksumOffset = 48;
constexpr std::size_t entryHeaderBytes = 40;
constexpr std::size_t patchBytes = 76;
constexpr uint64_t fnvOffset = 14695981039346656037ULL;
constexpr uint64_t fnvPrime = 1099511628211ULL;

[[noreturn]] void invalid(const std::filesystem::path& path, const char* reason)
{
    throw std::runtime_error("Invalid processed ground artifact " +
        (path.empty() ? std::string("<memory>") : path.string()) + ": " + reason);
}

template <typename Integer>
void appendInteger(std::vector<std::byte>& bytes, Integer value)
{
    for (std::size_t shift = 0; shift < sizeof(Integer) * 8; shift += 8) {
        bytes.push_back(static_cast<std::byte>(value >> shift));
    }
}

template <typename Integer>
Integer readInteger(std::span<const std::byte> bytes, std::size_t& cursor,
    const std::filesystem::path& path)
{
    if (cursor > bytes.size() || sizeof(Integer) > bytes.size() - cursor) {
        invalid(path, "truncated integer or payload");
    }
    Integer value = 0;
    for (std::size_t shift = 0; shift < sizeof(Integer) * 8; shift += 8) {
        value |= static_cast<Integer>(std::to_integer<uint8_t>(bytes[cursor++])) << shift;
    }
    return value;
}

void appendFloat(std::vector<std::byte>& bytes, float value)
{
    appendInteger(bytes, std::bit_cast<uint32_t>(value));
}

float readFloat(std::span<const std::byte> bytes, std::size_t& cursor,
    const std::filesystem::path& path)
{
    return std::bit_cast<float>(readInteger<uint32_t>(bytes, cursor, path));
}

void appendVec3(std::vector<std::byte>& bytes, Vec3 value)
{
    appendFloat(bytes, value.x);
    appendFloat(bytes, value.y);
    appendFloat(bytes, value.z);
}

Vec3 readVec3(std::span<const std::byte> bytes, std::size_t& cursor,
    const std::filesystem::path& path)
{
    const float x = readFloat(bytes, cursor, path);
    const float y = readFloat(bytes, cursor, path);
    const float z = readFloat(bytes, cursor, path);
    return { x, y, z };
}

uint64_t checksum(std::span<const std::byte> bytes)
{
    uint64_t digest = fnvOffset;
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        if (index >= checksumOffset && index < checksumOffset + sizeof(uint64_t)) continue;
        digest ^= std::to_integer<uint8_t>(bytes[index]);
        digest *= fnvPrime;
    }
    return digest;
}

bool finite(Vec3 value)
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

void validateEntry(const ProcessedGroundArtifactEntry& entry,
    const std::filesystem::path& path)
{
    const auto& key = entry.key;
    const Vec2 origin { std::bit_cast<float>(key[1]), std::bit_cast<float>(key[2]) };
    const float base = std::bit_cast<float>(key[3]);
    const float height = std::bit_cast<float>(key[4]);
    const GroundRimProfile profile {
        .exposedSides = static_cast<uint8_t>(key[5]),
        .concaveCorners = static_cast<uint8_t>(key[6]),
        .width = std::bit_cast<float>(key[7]),
        .depth = std::bit_cast<float>(key[8]),
        .origin = origin,
    };
    const float top = base + height;
    if (key[0] == 0 || key[5] > groundAllSides || key[6] > groundAllSides ||
        (key[5] == 0 && key[6] == 0) || !groundRimProfileValid(profile) ||
        !std::isfinite(base) || !std::isfinite(height) || height <= 0 || !std::isfinite(top)) {
        invalid(path, "invalid geometry key or profile");
    }
    if (entry.surface.count == 0 || entry.surface.count > GroundRimSurface::capacity) {
        invalid(path, "invalid patch count");
    }
    constexpr float tolerance = 0.0001f;
    double area = 0;
    for (std::size_t index = 0; index < entry.surface.count; ++index) {
        const auto& patch = entry.surface.patches[index];
        for (std::size_t vertex = 0; vertex < patch.vertices.size(); ++vertex) {
            const Vec3 point = patch.vertices[vertex];
            const float coverage = patch.wallCoverage[vertex];
            if (!finite(point) || point.x - origin.x < -tolerance ||
                point.x - origin.x > 1.0f + tolerance || point.y - origin.y < -tolerance ||
                point.y - origin.y > 1.0f + tolerance || point.z > top + tolerance ||
                point.z < top - profile.depth * 1.35f - tolerance ||
                !std::isfinite(coverage) || coverage < 0 || coverage > 1) {
                invalid(path, "invalid vertex bounds or wall coverage");
            }
        }
        const Vec3 primary = cross(patch.vertices[1] - patch.vertices[0],
            patch.vertices[2] - patch.vertices[0]);
        if (!finite(patch.normal) || !finite(primary) || primary.z <= 0 ||
            patch.normal.z <= 0 || std::abs(dot(patch.normal, patch.normal) - 1.0f) > tolerance ||
            dot(patch.normal, normalize(primary)) < 1.0f - tolerance) {
            invalid(path, "invalid patch winding or normal");
        }
        area += static_cast<double>(primary.z) * 0.5;
        if (patch.vertices[2] != patch.vertices[3]) {
            const Vec3 secondary = cross(patch.vertices[2] - patch.vertices[0],
                patch.vertices[3] - patch.vertices[0]);
            if (!finite(secondary) || secondary.z <= 0 ||
                dot(patch.normal, normalize(secondary)) < 1.0f - tolerance) {
                invalid(path, "invalid quad winding or normal");
            }
            area += static_cast<double>(secondary.z) * 0.5;
        }
    }
    if (std::abs(area - 1.0) > static_cast<double>(tolerance)) {
        invalid(path, "patches do not cover one tile");
    }
}

std::size_t validatedSize(const ProcessedGroundArtifact& artifact,
    const std::filesystem::path& path)
{
    if (artifact.entries.size() > processedGroundArtifactMaxEntries) {
        invalid(path, "too many geometry entries");
    }
    std::size_t size = headerBytes;
    const GroundRimSurfaceKey* previous = nullptr;
    for (const auto& entry : artifact.entries) {
        if (previous && !(*previous < entry.key)) invalid(path, "keys are not sorted and unique");
        validateEntry(entry, path);
        const std::size_t bytes = entryHeaderBytes + entry.surface.count * patchBytes;
        if (bytes > processedGroundArtifactMaxBytes - size) invalid(path, "artifact is too large");
        size += bytes;
        previous = &entry.key;
    }
    return size;
}

} // namespace

const GroundRimSurface* ProcessedGroundArtifact::find(const GroundRimSurfaceKey& key) const noexcept
{
    const auto found = std::ranges::lower_bound(entries, key, {}, &ProcessedGroundArtifactEntry::key);
    return found != entries.end() && found->key == key ? &found->surface : nullptr;
}

ProcessedGroundArtifact buildProcessedGroundArtifact(
    std::span<const RenderFrameData::Tile> tiles, uint64_t sourceFingerprint)
{
    ProcessedGroundArtifact result { .sourceFingerprint = sourceFingerprint };
    for (const auto& tile : tiles) {
        if (!hasGroundRimSurface(tile)) continue;
        if (result.entries.size() == processedGroundArtifactMaxEntries) {
            invalid({}, "too many geometry entries");
        }
        result.entries.push_back({ groundRimSurfaceKey(tile), buildGroundRimSurface(tile) });
    }
    std::ranges::sort(result.entries, {}, &ProcessedGroundArtifactEntry::key);
    result.entries.erase(std::unique(result.entries.begin(), result.entries.end(),
        [](const auto& left, const auto& right) { return left.key == right.key; }), result.entries.end());
    (void)validatedSize(result, {});
    return result;
}

std::vector<std::byte> serializeProcessedGroundArtifact(const ProcessedGroundArtifact& artifact)
{
    const std::size_t size = validatedSize(artifact, {});
    std::vector<std::byte> bytes;
    bytes.reserve(size);
    bytes.insert(bytes.end(), identifier.begin(), identifier.end());
    appendInteger(bytes, processedGroundArtifactFormatVersion);
    appendInteger(bytes, processedGroundArtifactCompilerRevision);
    appendInteger(bytes, artifact.sourceFingerprint);
    appendInteger(bytes, static_cast<uint32_t>(artifact.entries.size()));
    appendInteger(bytes, uint32_t { 0 }); // Reserved; unsupported flags are rejected.
    appendFloat(bytes, processedGroundArtifactRimWidth);
    appendFloat(bytes, processedGroundArtifactRimDepth);
    appendInteger(bytes, static_cast<uint64_t>(size - headerBytes));
    appendInteger(bytes, uint64_t { 0 });
    for (const auto& entry : artifact.entries) {
        for (const uint32_t word : entry.key) appendInteger(bytes, word);
        appendInteger(bytes, static_cast<uint32_t>(entry.surface.count));
        for (std::size_t index = 0; index < entry.surface.count; ++index) {
            const auto& patch = entry.surface.patches[index];
            for (const Vec3 vertex : patch.vertices) appendVec3(bytes, vertex);
            for (const float coverage : patch.wallCoverage) appendFloat(bytes, coverage);
            appendVec3(bytes, patch.normal);
        }
    }
    const uint64_t digest = checksum(bytes);
    for (std::size_t lane = 0; lane < sizeof(digest); ++lane) {
        bytes[checksumOffset + lane] = static_cast<std::byte>(digest >> (lane * 8));
    }
    return bytes;
}

ProcessedGroundArtifact parseProcessedGroundArtifact(std::span<const std::byte> bytes,
    const std::filesystem::path& diagnosticPath)
{
    if (bytes.size() < headerBytes || bytes.size() > processedGroundArtifactMaxBytes ||
        !std::equal(identifier.begin(), identifier.end(), bytes.begin())) {
        invalid(diagnosticPath, "bad identifier, truncated header, or excessive size");
    }
    std::size_t cursor = identifier.size();
    const uint32_t version = readInteger<uint32_t>(bytes, cursor, diagnosticPath);
    const uint32_t revision = readInteger<uint32_t>(bytes, cursor, diagnosticPath);
    if (version != processedGroundArtifactFormatVersion ||
        revision != processedGroundArtifactCompilerRevision) {
        invalid(diagnosticPath, "unsupported format or compiler revision");
    }
    ProcessedGroundArtifact result {
        .sourceFingerprint = readInteger<uint64_t>(bytes, cursor, diagnosticPath),
    };
    const uint32_t count = readInteger<uint32_t>(bytes, cursor, diagnosticPath);
    const uint32_t reserved = readInteger<uint32_t>(bytes, cursor, diagnosticPath);
    const uint32_t bakedWidth = readInteger<uint32_t>(bytes, cursor, diagnosticPath);
    const uint32_t bakedDepth = readInteger<uint32_t>(bytes, cursor, diagnosticPath);
    const uint64_t payloadSize = readInteger<uint64_t>(bytes, cursor, diagnosticPath);
    const uint64_t storedChecksum = readInteger<uint64_t>(bytes, cursor, diagnosticPath);
    if (count > processedGroundArtifactMaxEntries || reserved != 0 ||
        bakedWidth != std::bit_cast<uint32_t>(processedGroundArtifactRimWidth) ||
        bakedDepth != std::bit_cast<uint32_t>(processedGroundArtifactRimDepth) ||
        payloadSize != bytes.size() - headerBytes ||
        static_cast<uint64_t>(count) * entryHeaderBytes > payloadSize ||
        storedChecksum != checksum(bytes)) {
        invalid(diagnosticPath, "invalid counts, reserved fields, payload size, or checksum");
    }
    result.entries.reserve(count);
    for (uint32_t index = 0; index < count; ++index) {
        ProcessedGroundArtifactEntry entry;
        for (auto& word : entry.key) word = readInteger<uint32_t>(bytes, cursor, diagnosticPath);
        const uint32_t patches = readInteger<uint32_t>(bytes, cursor, diagnosticPath);
        if (patches == 0 || patches > GroundRimSurface::capacity ||
            static_cast<uint64_t>(patches) * patchBytes > bytes.size() - cursor) {
            invalid(diagnosticPath, "invalid patch count or truncated patches");
        }
        entry.surface.count = patches;
        for (uint32_t patchIndex = 0; patchIndex < patches; ++patchIndex) {
            auto& patch = entry.surface.patches[patchIndex];
            for (auto& vertex : patch.vertices) vertex = readVec3(bytes, cursor, diagnosticPath);
            for (auto& coverage : patch.wallCoverage) coverage = readFloat(bytes, cursor, diagnosticPath);
            patch.normal = readVec3(bytes, cursor, diagnosticPath);
        }
        validateEntry(entry, diagnosticPath);
        if (!result.entries.empty() && !(result.entries.back().key < entry.key)) {
            invalid(diagnosticPath, "keys are not sorted and unique");
        }
        result.entries.push_back(std::move(entry));
    }
    if (cursor != bytes.size()) invalid(diagnosticPath, "trailing or unreferenced payload");
    return result;
}

ProcessedGroundArtifact loadProcessedGroundArtifact(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) throw std::runtime_error("Cannot open processed ground artifact: " + path.string());
    const std::streampos end = input.tellg();
    if (end < 0 || static_cast<uint64_t>(end) > processedGroundArtifactMaxBytes) {
        invalid(path, "file exceeds size bound");
    }
    std::vector<std::byte> bytes(static_cast<std::size_t>(end));
    input.seekg(0);
    input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!input) throw std::runtime_error("Cannot read processed ground artifact: " + path.string());
    return parseProcessedGroundArtifact(bytes, path);
}

} // namespace sokoban

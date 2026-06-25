#include "engine/render/GltfMesh.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <optional>
#include <regex>
#include <stdexcept>
#include <string>
#include <string_view>

namespace sokoban {
namespace {

struct BufferView {
    size_t byteOffset = 0;
    size_t byteLength = 0;
    size_t byteStride = 0;
};

struct Accessor {
    size_t bufferView = 0;
    size_t byteOffset = 0;
    size_t count = 0;
    uint32_t componentType = 0;
    std::string type;
};

std::string readTextFile(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        throw std::runtime_error("Failed to open glTF file: " + path.string());
    }

    return {
        std::istreambuf_iterator<char>(stream),
        std::istreambuf_iterator<char>(),
    };
}

std::vector<std::byte> readBinaryFile(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream) {
        throw std::runtime_error("Failed to open glTF buffer: " + path.string());
    }

    const std::streamsize size = stream.tellg();
    if (size < 0) {
        throw std::runtime_error("Failed to determine glTF buffer size: " + path.string());
    }

    std::vector<std::byte> bytes(static_cast<size_t>(size));
    stream.seekg(0);
    if (!bytes.empty() && !stream.read(reinterpret_cast<char*>(bytes.data()), size)) {
        throw std::runtime_error("Failed to read glTF buffer: " + path.string());
    }
    return bytes;
}

std::string_view jsonArray(std::string_view json, std::string_view key)
{
    const std::string quotedKey = "\"" + std::string(key) + "\"";
    const size_t keyPosition = json.find(quotedKey);
    if (keyPosition == std::string_view::npos) {
        throw std::runtime_error("Missing glTF array: " + std::string(key));
    }

    const size_t start = json.find('[', keyPosition + quotedKey.size());
    if (start == std::string_view::npos) {
        throw std::runtime_error("Invalid glTF array: " + std::string(key));
    }

    size_t depth = 0;
    bool inString = false;
    bool escaped = false;
    for (size_t index = start; index < json.size(); ++index) {
        const char character = json[index];
        if (inString) {
            if (escaped) {
                escaped = false;
            } else if (character == '\\') {
                escaped = true;
            } else if (character == '"') {
                inString = false;
            }
            continue;
        }

        if (character == '"') {
            inString = true;
        } else if (character == '[') {
            ++depth;
        } else if (character == ']') {
            if (--depth == 0) {
                return json.substr(start + 1, index - start - 1);
            }
        }
    }

    throw std::runtime_error("Unterminated glTF array: " + std::string(key));
}

std::vector<std::string_view> jsonObjects(std::string_view array)
{
    std::vector<std::string_view> objects;
    size_t depth = 0;
    size_t start = 0;
    bool inString = false;
    bool escaped = false;

    for (size_t index = 0; index < array.size(); ++index) {
        const char character = array[index];
        if (inString) {
            if (escaped) {
                escaped = false;
            } else if (character == '\\') {
                escaped = true;
            } else if (character == '"') {
                inString = false;
            }
            continue;
        }

        if (character == '"') {
            inString = true;
        } else if (character == '{') {
            if (depth++ == 0) {
                start = index;
            }
        } else if (character == '}' && depth > 0 && --depth == 0) {
            objects.push_back(array.substr(start, index - start + 1));
        }
    }
    return objects;
}

std::optional<size_t> unsignedField(std::string_view object, std::string_view key)
{
    const std::regex pattern(
        "\"" + std::string(key) + "\"\\s*:\\s*([0-9]+)",
        std::regex::ECMAScript);
    std::cmatch match;
    if (!std::regex_search(object.data(), object.data() + object.size(), match, pattern)) {
        return std::nullopt;
    }
    return static_cast<size_t>(std::stoull(match[1].str()));
}

std::optional<std::string> stringField(std::string_view object, std::string_view key)
{
    const std::regex pattern(
        "\"" + std::string(key) + "\"\\s*:\\s*\"([^\"]+)\"",
        std::regex::ECMAScript);
    std::cmatch match;
    if (!std::regex_search(object.data(), object.data() + object.size(), match, pattern)) {
        return std::nullopt;
    }
    return match[1].str();
}

size_t requiredUnsignedField(std::string_view object, std::string_view key)
{
    const std::optional<size_t> value = unsignedField(object, key);
    if (!value) {
        throw std::runtime_error("Missing glTF integer field: " + std::string(key));
    }
    return *value;
}

std::string requiredStringField(std::string_view object, std::string_view key)
{
    const std::optional<std::string> value = stringField(object, key);
    if (!value) {
        throw std::runtime_error("Missing glTF string field: " + std::string(key));
    }
    return *value;
}

template <typename Value>
Value readValue(const std::vector<std::byte>& bytes, size_t offset)
{
    if (offset + sizeof(Value) > bytes.size()) {
        throw std::runtime_error("glTF accessor exceeds its binary buffer");
    }

    Value value {};
    std::memcpy(&value, bytes.data() + offset, sizeof(Value));
    return value;
}

Vec3 subtract(Vec3 left, Vec3 right)
{
    return { left.x - right.x, left.y - right.y, left.z - right.z };
}

Vec3 cross(Vec3 left, Vec3 right)
{
    return {
        left.y * right.z - left.z * right.y,
        left.z * right.x - left.x * right.z,
        left.x * right.y - left.y * right.x,
    };
}

Vec3 normalize(Vec3 value)
{
    const float length = std::sqrt(
        value.x * value.x +
        value.y * value.y +
        value.z * value.z);
    if (length <= 0.000001f) {
        return { 0.0f, 0.0f, 1.0f };
    }
    return { value.x / length, value.y / length, value.z / length };
}

} // namespace

MeshData loadGltfMesh(const std::filesystem::path& path)
{
    const std::string json = readTextFile(path);
    const std::vector<std::string_view> bufferObjects = jsonObjects(jsonArray(json, "buffers"));
    const std::vector<std::string_view> viewObjects = jsonObjects(jsonArray(json, "bufferViews"));
    const std::vector<std::string_view> accessorObjects = jsonObjects(jsonArray(json, "accessors"));
    if (bufferObjects.size() != 1 || viewObjects.empty() || accessorObjects.empty()) {
        throw std::runtime_error("Only single-buffer glTF meshes are currently supported");
    }

    const std::filesystem::path bufferPath =
        path.parent_path() / requiredStringField(bufferObjects.front(), "uri");
    const std::vector<std::byte> bytes = readBinaryFile(bufferPath);

    std::vector<BufferView> bufferViews;
    bufferViews.reserve(viewObjects.size());
    for (std::string_view object : viewObjects) {
        if (requiredUnsignedField(object, "buffer") != 0) {
            throw std::runtime_error("Only glTF buffer 0 is currently supported");
        }
        bufferViews.push_back({
            .byteOffset = unsignedField(object, "byteOffset").value_or(0),
            .byteLength = requiredUnsignedField(object, "byteLength"),
            .byteStride = unsignedField(object, "byteStride").value_or(0),
        });
    }

    std::vector<Accessor> accessors;
    accessors.reserve(accessorObjects.size());
    for (std::string_view object : accessorObjects) {
        accessors.push_back({
            .bufferView = requiredUnsignedField(object, "bufferView"),
            .byteOffset = unsignedField(object, "byteOffset").value_or(0),
            .count = requiredUnsignedField(object, "count"),
            .componentType = static_cast<uint32_t>(requiredUnsignedField(object, "componentType")),
            .type = requiredStringField(object, "type"),
        });
    }

    const size_t positionAccessorIndex = requiredUnsignedField(json, "POSITION");
    const size_t indexAccessorIndex = requiredUnsignedField(json, "indices");
    if (positionAccessorIndex >= accessors.size() || indexAccessorIndex >= accessors.size()) {
        throw std::runtime_error("glTF primitive references an invalid accessor");
    }

    const Accessor& positionAccessor = accessors[positionAccessorIndex];
    const Accessor& indexAccessor = accessors[indexAccessorIndex];
    if (positionAccessor.bufferView >= bufferViews.size() ||
        indexAccessor.bufferView >= bufferViews.size() ||
        positionAccessor.componentType != 5126 ||
        positionAccessor.type != "VEC3" ||
        indexAccessor.type != "SCALAR") {
        throw std::runtime_error("Unsupported glTF position or index accessor format");
    }

    const BufferView& positionView = bufferViews[positionAccessor.bufferView];
    const size_t positionStride = positionView.byteStride == 0
        ? sizeof(float) * 3
        : positionView.byteStride;
    std::vector<Vec3> positions;
    positions.reserve(positionAccessor.count);

    Vec3 minimum {
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max(),
    };
    Vec3 maximum {
        std::numeric_limits<float>::lowest(),
        std::numeric_limits<float>::lowest(),
        std::numeric_limits<float>::lowest(),
    };
    for (size_t index = 0; index < positionAccessor.count; ++index) {
        const size_t offset =
            positionView.byteOffset +
            positionAccessor.byteOffset +
            index * positionStride;
        const Vec3 position {
            readValue<float>(bytes, offset),
            readValue<float>(bytes, offset + sizeof(float)),
            readValue<float>(bytes, offset + sizeof(float) * 2),
        };
        positions.push_back(position);
        minimum.x = std::min(minimum.x, position.x);
        minimum.y = std::min(minimum.y, position.y);
        minimum.z = std::min(minimum.z, position.z);
        maximum.x = std::max(maximum.x, position.x);
        maximum.y = std::max(maximum.y, position.y);
        maximum.z = std::max(maximum.z, position.z);
    }

    const Vec3 extent {
        std::max(maximum.x - minimum.x, 0.000001f),
        std::max(maximum.y - minimum.y, 0.000001f),
        std::max(maximum.z - minimum.z, 0.000001f),
    };
    for (Vec3& position : positions) {
        position = {
            (position.x - minimum.x) / extent.x,
            (maximum.z - position.z) / extent.z,
            (position.y - minimum.y) / extent.y,
        };
    }

    const BufferView& indexView = bufferViews[indexAccessor.bufferView];
    const size_t componentSize = indexAccessor.componentType == 5123
        ? sizeof(uint16_t)
        : indexAccessor.componentType == 5125
            ? sizeof(uint32_t)
            : 0;
    if (componentSize == 0) {
        throw std::runtime_error("Only 16-bit and 32-bit glTF indices are currently supported");
    }
    const size_t indexStride = indexView.byteStride == 0 ? componentSize : indexView.byteStride;

    std::vector<uint32_t> sourceIndices;
    sourceIndices.reserve(indexAccessor.count);
    for (size_t index = 0; index < indexAccessor.count; ++index) {
        const size_t offset =
            indexView.byteOffset +
            indexAccessor.byteOffset +
            index * indexStride;
        const uint32_t value = indexAccessor.componentType == 5123
            ? readValue<uint16_t>(bytes, offset)
            : readValue<uint32_t>(bytes, offset);
        if (value >= positions.size()) {
            throw std::runtime_error("glTF index references an invalid vertex");
        }
        sourceIndices.push_back(value);
    }
    if (sourceIndices.size() % 3 != 0) {
        throw std::runtime_error("Only triangle-list glTF primitives are currently supported");
    }

    MeshData mesh;
    mesh.vertices.reserve(sourceIndices.size());
    mesh.indices.reserve(sourceIndices.size());
    for (size_t index = 0; index < sourceIndices.size(); index += 3) {
        const uint32_t first = sourceIndices[index];
        const uint32_t second = sourceIndices[index + 1];
        const uint32_t third = sourceIndices[index + 2];
        const Vec3 normal = normalize(cross(
            subtract(positions[second], positions[first]),
            subtract(positions[third], positions[first])));
        const uint32_t baseVertex = static_cast<uint32_t>(mesh.vertices.size());
        mesh.vertices.push_back({ .position = positions[first], .normal = normal });
        mesh.vertices.push_back({ .position = positions[second], .normal = normal });
        mesh.vertices.push_back({ .position = positions[third], .normal = normal });
        mesh.indices.push_back(baseVertex);
        mesh.indices.push_back(baseVertex + 1);
        mesh.indices.push_back(baseVertex + 2);
    }
    return mesh;
}

} // namespace sokoban

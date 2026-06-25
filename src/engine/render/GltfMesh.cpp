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

constexpr uint32_t glbMagic = 0x46546C67;
constexpr uint32_t glbJsonChunk = 0x4E4F534A;
constexpr uint32_t glbBinaryChunk = 0x004E4942;

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

struct GltfDocument {
    std::string json;
    std::vector<std::byte> buffer;
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

template <typename Value>
Value readValue(const std::vector<std::byte>& bytes, size_t offset)
{
    if (offset + sizeof(Value) > bytes.size()) {
        throw std::runtime_error("glTF data exceeds its binary buffer");
    }
    Value value {};
    std::memcpy(&value, bytes.data() + offset, sizeof(Value));
    return value;
}

GltfDocument loadDocument(const std::filesystem::path& path)
{
    if (path.extension() != ".glb") {
        GltfDocument document;
        document.json = readTextFile(path);
        return document;
    }

    const std::vector<std::byte> file = readBinaryFile(path);
    if (file.size() < 20 || readValue<uint32_t>(file, 0) != glbMagic) {
        throw std::runtime_error("Invalid GLB header: " + path.string());
    }

    GltfDocument document;
    size_t offset = 12;
    while (offset + 8 <= file.size()) {
        const uint32_t chunkLength = readValue<uint32_t>(file, offset);
        const uint32_t chunkType = readValue<uint32_t>(file, offset + 4);
        offset += 8;
        if (offset + chunkLength > file.size()) {
            throw std::runtime_error("Invalid GLB chunk length: " + path.string());
        }

        if (chunkType == glbJsonChunk) {
            document.json.assign(
                reinterpret_cast<const char*>(file.data() + offset),
                chunkLength);
            while (!document.json.empty() &&
                (document.json.back() == '\0' || document.json.back() == ' ')) {
                document.json.pop_back();
            }
        } else if (chunkType == glbBinaryChunk) {
            document.buffer.assign(
                file.begin() + static_cast<std::ptrdiff_t>(offset),
                file.begin() + static_cast<std::ptrdiff_t>(offset + chunkLength));
        }
        offset += chunkLength;
    }

    if (document.json.empty() || document.buffer.empty()) {
        throw std::runtime_error("GLB is missing JSON or binary data: " + path.string());
    }
    return document;
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
        } else if (character == ']' && --depth == 0) {
            return json.substr(start + 1, index - start - 1);
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

size_t componentCount(std::string_view type)
{
    if (type == "SCALAR") {
        return 1;
    }
    if (type == "VEC2") {
        return 2;
    }
    if (type == "VEC3") {
        return 3;
    }
    if (type == "VEC4") {
        return 4;
    }
    throw std::runtime_error("Unsupported glTF accessor type: " + std::string(type));
}

size_t componentSize(uint32_t componentType)
{
    switch (componentType) {
    case 5121:
        return sizeof(uint8_t);
    case 5123:
        return sizeof(uint16_t);
    case 5125:
        return sizeof(uint32_t);
    case 5126:
        return sizeof(float);
    default:
        throw std::runtime_error("Unsupported glTF component type");
    }
}

size_t accessorStride(const Accessor& accessor, const BufferView& view)
{
    return view.byteStride == 0
        ? componentSize(accessor.componentType) * componentCount(accessor.type)
        : view.byteStride;
}

Vec3 readVec3(
    const Accessor& accessor,
    const BufferView& view,
    const std::vector<std::byte>& bytes,
    size_t index)
{
    if (accessor.componentType != 5126 || accessor.type != "VEC3") {
        throw std::runtime_error("Expected a float VEC3 glTF accessor");
    }
    const size_t offset = view.byteOffset + accessor.byteOffset + index * accessorStride(accessor, view);
    return {
        readValue<float>(bytes, offset),
        readValue<float>(bytes, offset + sizeof(float)),
        readValue<float>(bytes, offset + sizeof(float) * 2),
    };
}

Vec2 readVec2(
    const Accessor& accessor,
    const BufferView& view,
    const std::vector<std::byte>& bytes,
    size_t index)
{
    if (accessor.componentType != 5126 || accessor.type != "VEC2") {
        throw std::runtime_error("Expected a float VEC2 glTF accessor");
    }
    const size_t offset = view.byteOffset + accessor.byteOffset + index * accessorStride(accessor, view);
    return {
        readValue<float>(bytes, offset),
        readValue<float>(bytes, offset + sizeof(float)),
    };
}

uint32_t readIndex(
    const Accessor& accessor,
    const BufferView& view,
    const std::vector<std::byte>& bytes,
    size_t index)
{
    const size_t offset = view.byteOffset + accessor.byteOffset + index * accessorStride(accessor, view);
    switch (accessor.componentType) {
    case 5121:
        return readValue<uint8_t>(bytes, offset);
    case 5123:
        return readValue<uint16_t>(bytes, offset);
    case 5125:
        return readValue<uint32_t>(bytes, offset);
    default:
        throw std::runtime_error("Unsupported glTF index component type");
    }
}

} // namespace

MeshData loadGltfMesh(const std::filesystem::path& path, GltfMeshLoadOptions options)
{
    GltfDocument document = loadDocument(path);
    const std::vector<std::string_view> bufferObjects = jsonObjects(jsonArray(document.json, "buffers"));
    const std::vector<std::string_view> viewObjects = jsonObjects(jsonArray(document.json, "bufferViews"));
    const std::vector<std::string_view> accessorObjects = jsonObjects(jsonArray(document.json, "accessors"));
    const std::vector<std::string_view> meshObjects = jsonObjects(jsonArray(document.json, "meshes"));
    if (bufferObjects.size() != 1 || viewObjects.empty() || accessorObjects.empty() || meshObjects.empty()) {
        throw std::runtime_error("Unsupported or empty glTF document");
    }

    if (document.buffer.empty()) {
        const std::string uri = requiredStringField(bufferObjects.front(), "uri");
        document.buffer = readBinaryFile(path.parent_path() / uri);
    }

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

    MeshData mesh;
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

    for (std::string_view meshObject : meshObjects) {
        for (std::string_view primitive : jsonObjects(jsonArray(meshObject, "primitives"))) {
            const size_t positionIndex = requiredUnsignedField(primitive, "POSITION");
            const size_t normalIndex = requiredUnsignedField(primitive, "NORMAL");
            const size_t uvIndex = requiredUnsignedField(primitive, "TEXCOORD_0");
            const size_t indicesIndex = requiredUnsignedField(primitive, "indices");
            if (positionIndex >= accessors.size() ||
                normalIndex >= accessors.size() ||
                uvIndex >= accessors.size() ||
                indicesIndex >= accessors.size()) {
                throw std::runtime_error("glTF primitive references an invalid accessor");
            }

            const Accessor& positions = accessors[positionIndex];
            const Accessor& normals = accessors[normalIndex];
            const Accessor& uvs = accessors[uvIndex];
            const Accessor& indices = accessors[indicesIndex];
            if (positions.count != normals.count || positions.count != uvs.count ||
                positions.bufferView >= bufferViews.size() ||
                normals.bufferView >= bufferViews.size() ||
                uvs.bufferView >= bufferViews.size() ||
                indices.bufferView >= bufferViews.size()) {
                throw std::runtime_error("Incompatible glTF primitive accessors");
            }

            const uint32_t baseVertex = static_cast<uint32_t>(mesh.vertices.size());
            mesh.vertices.reserve(mesh.vertices.size() + positions.count);
            for (size_t index = 0; index < positions.count; ++index) {
                const Vec3 position = readVec3(
                    positions,
                    bufferViews[positions.bufferView],
                    document.buffer,
                    index);
                const Vec3 normal = readVec3(
                    normals,
                    bufferViews[normals.bufferView],
                    document.buffer,
                    index);
                const Vec2 uv = readVec2(
                    uvs,
                    bufferViews[uvs.bufferView],
                    document.buffer,
                    index);
                mesh.vertices.push_back({
                    .position = position,
                    .normal = normal,
                    .uv = uv,
                });
                minimum.x = std::min(minimum.x, position.x);
                minimum.y = std::min(minimum.y, position.y);
                minimum.z = std::min(minimum.z, position.z);
                maximum.x = std::max(maximum.x, position.x);
                maximum.y = std::max(maximum.y, position.y);
                maximum.z = std::max(maximum.z, position.z);
            }

            mesh.indices.reserve(mesh.indices.size() + indices.count);
            for (size_t index = 0; index < indices.count; ++index) {
                const uint32_t sourceIndex = readIndex(
                    indices,
                    bufferViews[indices.bufferView],
                    document.buffer,
                    index);
                if (sourceIndex >= positions.count) {
                    throw std::runtime_error("glTF index references an invalid vertex");
                }
                mesh.indices.push_back(baseVertex + sourceIndex);
            }
        }
    }

    if (mesh.vertices.empty() || mesh.indices.empty() || mesh.indices.size() % 3 != 0) {
        throw std::runtime_error("Only non-empty triangle-list glTF meshes are supported");
    }

    const float sourceHeight = std::max(maximum.y - minimum.y, 0.000001f);
    const Vec3 extent {
        std::max(maximum.x - minimum.x, 0.000001f),
        sourceHeight,
        std::max(maximum.z - minimum.z, 0.000001f),
    };
    const Vec3 center {
        (minimum.x + maximum.x) * 0.5f,
        (minimum.y + maximum.y) * 0.5f,
        (minimum.z + maximum.z) * 0.5f,
    };

    for (MeshVertex& vertex : mesh.vertices) {
        if (options.preserveAspectRatio) {
            vertex.position = {
                0.5f + (vertex.position.x - center.x) / sourceHeight,
                0.5f - (vertex.position.z - center.z) / sourceHeight,
                (vertex.position.y - minimum.y) / sourceHeight,
            };
        } else {
            vertex.position = {
                (vertex.position.x - minimum.x) / extent.x,
                (maximum.z - vertex.position.z) / extent.z,
                (vertex.position.y - minimum.y) / extent.y,
            };
        }
        vertex.normal = normalize({
            vertex.normal.x,
            -vertex.normal.z,
            vertex.normal.y,
        });
        if (options.rotateHalfTurn) {
            vertex.position.x = 1.0f - vertex.position.x;
            vertex.position.y = 1.0f - vertex.position.y;
            vertex.normal.x = -vertex.normal.x;
            vertex.normal.y = -vertex.normal.y;
        }
    }
    return mesh;
}

} // namespace sokoban

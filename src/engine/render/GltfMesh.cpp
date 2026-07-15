#include "engine/render/GltfMesh.hpp"

#include "engine/TaskSystem.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <optional>
#include <regex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>

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
    bool normalized = false;
};

struct GltfDocument {
    std::string json;
    std::vector<std::byte> buffer;
};

struct GltfNode {
    std::string name;
    std::vector<uint32_t> children;
    Vec3 translation {};
    Vec4 rotation { 0.0f, 0.0f, 0.0f, 1.0f };
    Vec3 scale { 1.0f, 1.0f, 1.0f };
};

struct SourceBounds {
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

std::string_view jsonArrayFromOpenBracket(std::string_view json, size_t start, std::string_view key)
{
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

std::string_view topLevelJsonArray(std::string_view json, std::string_view key)
{
    size_t objectDepth = 0;
    size_t arrayDepth = 0;
    bool inString = false;
    bool escaped = false;
    size_t stringStart = 0;

    for (size_t index = 0; index < json.size(); ++index) {
        const char character = json[index];
        if (inString) {
            if (escaped) {
                escaped = false;
            } else if (character == '\\') {
                escaped = true;
            } else if (character == '"') {
                inString = false;
                if (objectDepth == 1 && arrayDepth == 0 && json.substr(stringStart, index - stringStart) == key) {
                    size_t colon = index + 1;
                    while (colon < json.size() && std::isspace(static_cast<unsigned char>(json[colon]))) {
                        ++colon;
                    }
                    if (colon >= json.size() || json[colon] != ':') {
                        continue;
                    }
                    size_t start = colon + 1;
                    while (start < json.size() && std::isspace(static_cast<unsigned char>(json[start]))) {
                        ++start;
                    }
                    if (start < json.size() && json[start] == '[') {
                        return jsonArrayFromOpenBracket(json, start, key);
                    }
                }
            }
            continue;
        }

        if (character == '"') {
            inString = true;
            escaped = false;
            stringStart = index + 1;
        } else if (character == '{') {
            ++objectDepth;
        } else if (character == '}' && objectDepth > 0) {
            --objectDepth;
        } else if (character == '[') {
            ++arrayDepth;
        } else if (character == ']' && arrayDepth > 0) {
            --arrayDepth;
        }
    }

    throw std::runtime_error("Missing top-level glTF array: " + std::string(key));
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

std::optional<bool> boolField(std::string_view object, std::string_view key)
{
    const std::regex pattern(
        "\"" + std::string(key) + "\"\\s*:\\s*(true|false)",
        std::regex::ECMAScript);
    std::cmatch match;
    if (!std::regex_search(object.data(), object.data() + object.size(), match, pattern)) {
        return std::nullopt;
    }
    return match[1].str() == "true";
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

std::vector<float> floatArrayField(std::string_view object, std::string_view key)
{
    const std::regex pattern(
        "\"" + std::string(key) + "\"\\s*:\\s*\\[([^\\]]*)\\]",
        std::regex::ECMAScript);
    std::cmatch match;
    if (!std::regex_search(object.data(), object.data() + object.size(), match, pattern)) {
        return {};
    }

    std::vector<float> values;
    const std::string text = match[1].str();
    const std::regex numberPattern("-?(?:[0-9]+\\.?[0-9]*|\\.[0-9]+)(?:[eE][-+]?[0-9]+)?");
    for (std::sregex_iterator it(text.begin(), text.end(), numberPattern), end; it != end; ++it) {
        values.push_back(std::stof((*it)[0].str()));
    }
    return values;
}

std::vector<uint32_t> unsignedArrayField(std::string_view object, std::string_view key)
{
    const std::regex pattern(
        "\"" + std::string(key) + "\"\\s*:\\s*\\[([^\\]]*)\\]",
        std::regex::ECMAScript);
    std::cmatch match;
    if (!std::regex_search(object.data(), object.data() + object.size(), match, pattern)) {
        return {};
    }

    std::vector<uint32_t> values;
    const std::string text = match[1].str();
    const std::regex numberPattern("[0-9]+");
    for (std::sregex_iterator it(text.begin(), text.end(), numberPattern), end; it != end; ++it) {
        values.push_back(static_cast<uint32_t>(std::stoul((*it)[0].str())));
    }
    return values;
}

Vec3 vec3FromArray(const std::vector<float>& values, Vec3 fallback)
{
    if (values.size() < 3) {
        return fallback;
    }
    return { values[0], values[1], values[2] };
}

Vec4 vec4FromArray(const std::vector<float>& values, Vec4 fallback)
{
    if (values.size() < 4) {
        return fallback;
    }
    return { values[0], values[1], values[2], values[3] };
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

Vec4 normalize(Vec4 value)
{
    const float length = std::sqrt(
        value.x * value.x +
        value.y * value.y +
        value.z * value.z +
        value.w * value.w);
    if (length <= 0.000001f) {
        return { 0.0f, 0.0f, 0.0f, 1.0f };
    }
    return { value.x / length, value.y / length, value.z / length, value.w / length };
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
    if (type == "MAT4") {
        return 16;
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

Vec4 readVec4(
    const Accessor& accessor,
    const BufferView& view,
    const std::vector<std::byte>& bytes,
    size_t index)
{
    if (accessor.componentType != 5126 || accessor.type != "VEC4") {
        throw std::runtime_error("Expected a float VEC4 glTF accessor");
    }
    const size_t offset = view.byteOffset + accessor.byteOffset + index * accessorStride(accessor, view);
    return {
        readValue<float>(bytes, offset),
        readValue<float>(bytes, offset + sizeof(float)),
        readValue<float>(bytes, offset + sizeof(float) * 2),
        readValue<float>(bytes, offset + sizeof(float) * 3),
    };
}

float readScalarFloat(
    const Accessor& accessor,
    const BufferView& view,
    const std::vector<std::byte>& bytes,
    size_t index)
{
    if (accessor.componentType != 5126 || accessor.type != "SCALAR") {
        throw std::runtime_error("Expected a float scalar glTF accessor");
    }
    const size_t offset = view.byteOffset + accessor.byteOffset + index * accessorStride(accessor, view);
    return readValue<float>(bytes, offset);
}

Mat4 readMat4(
    const Accessor& accessor,
    const BufferView& view,
    const std::vector<std::byte>& bytes,
    size_t index)
{
    if (accessor.componentType != 5126 || accessor.type != "MAT4") {
        throw std::runtime_error("Expected a float MAT4 glTF accessor");
    }
    const size_t offset = view.byteOffset + accessor.byteOffset + index * accessorStride(accessor, view);
    Mat4 matrix;
    for (size_t i = 0; i < matrix.values.size(); ++i) {
        matrix.values[i] = readValue<float>(bytes, offset + sizeof(float) * i);
    }
    return matrix;
}

std::array<uint16_t, 4> readJoints(
    const Accessor& accessor,
    const BufferView& view,
    const std::vector<std::byte>& bytes,
    size_t index)
{
    if (accessor.type != "VEC4" || (accessor.componentType != 5121 && accessor.componentType != 5123)) {
        throw std::runtime_error("Expected an integer VEC4 JOINTS_0 glTF accessor");
    }
    const size_t offset = view.byteOffset + accessor.byteOffset + index * accessorStride(accessor, view);
    if (accessor.componentType == 5121) {
        return {
            readValue<uint8_t>(bytes, offset),
            readValue<uint8_t>(bytes, offset + sizeof(uint8_t)),
            readValue<uint8_t>(bytes, offset + sizeof(uint8_t) * 2),
            readValue<uint8_t>(bytes, offset + sizeof(uint8_t) * 3),
        };
    }
    return {
        readValue<uint16_t>(bytes, offset),
        readValue<uint16_t>(bytes, offset + sizeof(uint16_t)),
        readValue<uint16_t>(bytes, offset + sizeof(uint16_t) * 2),
        readValue<uint16_t>(bytes, offset + sizeof(uint16_t) * 3),
    };
}

std::array<float, 4> readWeights(
    const Accessor& accessor,
    const BufferView& view,
    const std::vector<std::byte>& bytes,
    size_t index)
{
    if (accessor.type != "VEC4") {
        throw std::runtime_error("Expected a VEC4 WEIGHTS_0 glTF accessor");
    }
    const size_t offset = view.byteOffset + accessor.byteOffset + index * accessorStride(accessor, view);
    if (accessor.componentType == 5126) {
        return {
            readValue<float>(bytes, offset),
            readValue<float>(bytes, offset + sizeof(float)),
            readValue<float>(bytes, offset + sizeof(float) * 2),
            readValue<float>(bytes, offset + sizeof(float) * 3),
        };
    }
    if (accessor.componentType == 5121) {
        return {
            static_cast<float>(readValue<uint8_t>(bytes, offset)) / 255.0f,
            static_cast<float>(readValue<uint8_t>(bytes, offset + sizeof(uint8_t))) / 255.0f,
            static_cast<float>(readValue<uint8_t>(bytes, offset + sizeof(uint8_t) * 2)) / 255.0f,
            static_cast<float>(readValue<uint8_t>(bytes, offset + sizeof(uint8_t) * 3)) / 255.0f,
        };
    }
    if (accessor.componentType == 5123) {
        return {
            static_cast<float>(readValue<uint16_t>(bytes, offset)) / 65535.0f,
            static_cast<float>(readValue<uint16_t>(bytes, offset + sizeof(uint16_t))) / 65535.0f,
            static_cast<float>(readValue<uint16_t>(bytes, offset + sizeof(uint16_t) * 2)) / 65535.0f,
            static_cast<float>(readValue<uint16_t>(bytes, offset + sizeof(uint16_t) * 3)) / 65535.0f,
        };
    }
    throw std::runtime_error("Unsupported glTF weight component type");
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

Mat4 identityMatrix()
{
    Mat4 result;
    result.values = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f,
    };
    return result;
}

Mat4 multiply(Mat4 left, Mat4 right)
{
    Mat4 result;
    for (size_t column = 0; column < 4; ++column) {
        for (size_t row = 0; row < 4; ++row) {
            float value = 0.0f;
            for (size_t index = 0; index < 4; ++index) {
                value += left.values[index * 4 + row] * right.values[column * 4 + index];
            }
            result.values[column * 4 + row] = value;
        }
    }
    return result;
}

Mat4 matrixFromTrs(Vec3 translation, Vec4 rotation, Vec3 scale)
{
    rotation = normalize(rotation);
    const float x = rotation.x;
    const float y = rotation.y;
    const float z = rotation.z;
    const float w = rotation.w;
    const float xx = x * x;
    const float yy = y * y;
    const float zz = z * z;
    const float xy = x * y;
    const float xz = x * z;
    const float yz = y * z;
    const float wx = w * x;
    const float wy = w * y;
    const float wz = w * z;

    Mat4 result = identityMatrix();
    result.values[0] = (1.0f - 2.0f * (yy + zz)) * scale.x;
    result.values[1] = (2.0f * (xy + wz)) * scale.x;
    result.values[2] = (2.0f * (xz - wy)) * scale.x;

    result.values[4] = (2.0f * (xy - wz)) * scale.y;
    result.values[5] = (1.0f - 2.0f * (xx + zz)) * scale.y;
    result.values[6] = (2.0f * (yz + wx)) * scale.y;

    result.values[8] = (2.0f * (xz + wy)) * scale.z;
    result.values[9] = (2.0f * (yz - wx)) * scale.z;
    result.values[10] = (1.0f - 2.0f * (xx + yy)) * scale.z;

    result.values[12] = translation.x;
    result.values[13] = translation.y;
    result.values[14] = translation.z;
    return result;
}

Vec3 transformPoint(Mat4 matrix, Vec3 point)
{
    return {
        matrix.values[0] * point.x + matrix.values[4] * point.y + matrix.values[8] * point.z + matrix.values[12],
        matrix.values[1] * point.x + matrix.values[5] * point.y + matrix.values[9] * point.z + matrix.values[13],
        matrix.values[2] * point.x + matrix.values[6] * point.y + matrix.values[10] * point.z + matrix.values[14],
    };
}

Vec3 transformVector(Mat4 matrix, Vec3 vector)
{
    return {
        matrix.values[0] * vector.x + matrix.values[4] * vector.y + matrix.values[8] * vector.z,
        matrix.values[1] * vector.x + matrix.values[5] * vector.y + matrix.values[9] * vector.z,
        matrix.values[2] * vector.x + matrix.values[6] * vector.y + matrix.values[10] * vector.z,
    };
}

Vec3 add(Vec3 left, Vec3 right)
{
    return { left.x + right.x, left.y + right.y, left.z + right.z };
}

Vec3 multiply(Vec3 value, float scalar)
{
    return { value.x * scalar, value.y * scalar, value.z * scalar };
}

Vec4 lerp(Vec4 left, Vec4 right, float t)
{
    return {
        left.x + (right.x - left.x) * t,
        left.y + (right.y - left.y) * t,
        left.z + (right.z - left.z) * t,
        left.w + (right.w - left.w) * t,
    };
}

Vec4 slerp(Vec4 left, Vec4 right, float t)
{
    left = normalize(left);
    right = normalize(right);
    float dot = left.x * right.x + left.y * right.y + left.z * right.z + left.w * right.w;
    if (dot < 0.0f) {
        right = { -right.x, -right.y, -right.z, -right.w };
        dot = -dot;
    }
    if (dot > 0.9995f) {
        return normalize(lerp(left, right, t));
    }

    const float theta0 = std::acos(std::clamp(dot, -1.0f, 1.0f));
    const float theta = theta0 * t;
    const float sinTheta = std::sin(theta);
    const float sinTheta0 = std::sin(theta0);
    const float scaleLeft = std::cos(theta) - dot * sinTheta / sinTheta0;
    const float scaleRight = sinTheta / sinTheta0;
    return {
        left.x * scaleLeft + right.x * scaleRight,
        left.y * scaleLeft + right.y * scaleRight,
        left.z * scaleLeft + right.z * scaleRight,
        left.w * scaleLeft + right.w * scaleRight,
    };
}

Vec4 sampleKeyframes(const AnimationKeyframes& keyframes, float timeSeconds, bool rotation)
{
    if (keyframes.times.empty() || keyframes.values.empty()) {
        return rotation ? Vec4 { 0.0f, 0.0f, 0.0f, 1.0f } : Vec4 {};
    }
    if (keyframes.times.size() == 1 || timeSeconds <= keyframes.times.front()) {
        return keyframes.values.front();
    }
    if (timeSeconds >= keyframes.times.back()) {
        return keyframes.values.back();
    }

    const auto upper = std::upper_bound(keyframes.times.begin(), keyframes.times.end(), timeSeconds);
    const size_t rightIndex = static_cast<size_t>(std::distance(keyframes.times.begin(), upper));
    const size_t leftIndex = rightIndex - 1;
    const float leftTime = keyframes.times[leftIndex];
    const float rightTime = keyframes.times[rightIndex];
    const float t = rightTime > leftTime
        ? (timeSeconds - leftTime) / (rightTime - leftTime)
        : 0.0f;
    return rotation
        ? slerp(keyframes.values[leftIndex], keyframes.values[rightIndex], t)
        : lerp(keyframes.values[leftIndex], keyframes.values[rightIndex], t);
}

void includeBounds(SourceBounds& bounds, Vec3 position)
{
    bounds.minimum.x = std::min(bounds.minimum.x, position.x);
    bounds.minimum.y = std::min(bounds.minimum.y, position.y);
    bounds.minimum.z = std::min(bounds.minimum.z, position.z);
    bounds.maximum.x = std::max(bounds.maximum.x, position.x);
    bounds.maximum.y = std::max(bounds.maximum.y, position.y);
    bounds.maximum.z = std::max(bounds.maximum.z, position.z);
}

MeshVertex normalizedVertex(
    Vec3 position,
    Vec3 normal,
    Vec2 uv,
    float textureIndex,
    SourceBounds bounds,
    GltfMeshLoadOptions options)
{
    const float sourceHeight = std::max(bounds.maximum.y - bounds.minimum.y, 0.000001f);
    const Vec3 extent {
        std::max(bounds.maximum.x - bounds.minimum.x, 0.000001f),
        sourceHeight,
        std::max(bounds.maximum.z - bounds.minimum.z, 0.000001f),
    };
    const Vec3 center {
        (bounds.minimum.x + bounds.maximum.x) * 0.5f,
        (bounds.minimum.y + bounds.maximum.y) * 0.5f,
        (bounds.minimum.z + bounds.maximum.z) * 0.5f,
    };

    MeshVertex vertex;
    if (options.preserveAspectRatio) {
        vertex.position = {
            0.5f + (position.x - center.x) / sourceHeight,
            0.5f - (position.z - center.z) / sourceHeight,
            (position.y - bounds.minimum.y) / sourceHeight,
        };
    } else {
        vertex.position = {
            (position.x - bounds.minimum.x) / extent.x,
            (bounds.maximum.z - position.z) / extent.z,
            (position.y - bounds.minimum.y) / extent.y,
        };
    }
    vertex.normal = normalize(Vec3 {
        normal.x,
        -normal.z,
        normal.y,
    });
    if (options.rotateHalfTurn) {
        vertex.position.x = 1.0f - vertex.position.x;
        vertex.position.y = 1.0f - vertex.position.y;
        vertex.normal.x = -vertex.normal.x;
        vertex.normal.y = -vertex.normal.y;
    }
    vertex.uv = uv;
    vertex.textureIndex = textureIndex;
    return vertex;
}

std::vector<BufferView> parseBufferViews(const std::vector<std::string_view>& viewObjects)
{
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
    return bufferViews;
}

std::vector<Accessor> parseAccessors(const std::vector<std::string_view>& accessorObjects)
{
    std::vector<Accessor> accessors;
    accessors.reserve(accessorObjects.size());
    for (std::string_view object : accessorObjects) {
        accessors.push_back({
            .bufferView = requiredUnsignedField(object, "bufferView"),
            .byteOffset = unsignedField(object, "byteOffset").value_or(0),
            .count = requiredUnsignedField(object, "count"),
            .componentType = static_cast<uint32_t>(requiredUnsignedField(object, "componentType")),
            .type = requiredStringField(object, "type"),
            .normalized = boolField(object, "normalized").value_or(false),
        });
    }
    return accessors;
}

std::vector<GltfNode> parseNodes(const std::string& json)
{
    std::vector<GltfNode> nodes;
    const std::vector<std::string_view> nodeObjects = jsonObjects(topLevelJsonArray(json, "nodes"));
    nodes.reserve(nodeObjects.size());
    for (std::string_view object : nodeObjects) {
        nodes.push_back({
            .name = stringField(object, "name").value_or(""),
            .children = unsignedArrayField(object, "children"),
            .translation = vec3FromArray(floatArrayField(object, "translation"), {}),
            .rotation = normalize(vec4FromArray(floatArrayField(object, "rotation"), { 0.0f, 0.0f, 0.0f, 1.0f })),
            .scale = vec3FromArray(floatArrayField(object, "scale"), { 1.0f, 1.0f, 1.0f }),
        });
    }
    return nodes;
}

std::vector<int> parentIndicesForNodes(const std::vector<GltfNode>& nodes)
{
    std::vector<int> parents(nodes.size(), -1);
    for (size_t nodeIndex = 0; nodeIndex < nodes.size(); ++nodeIndex) {
        for (uint32_t child : nodes[nodeIndex].children) {
            if (child < parents.size()) {
                parents[child] = static_cast<int>(nodeIndex);
            }
        }
    }
    return parents;
}

void ensureBufferLoaded(GltfDocument& document, const std::filesystem::path& path, const std::vector<std::string_view>& bufferObjects)
{
    if (!document.buffer.empty()) {
        return;
    }
    if (bufferObjects.size() != 1) {
        throw std::runtime_error("Only single-buffer glTF documents are supported");
    }
    const std::string uri = requiredStringField(bufferObjects.front(), "uri");
    document.buffer = readBinaryFile(path.parent_path() / uri);
}

} // namespace

MeshData loadGltfMesh(const std::filesystem::path& path, GltfMeshLoadOptions options)
{
    GltfDocument document = loadDocument(path);
    const std::vector<std::string_view> bufferObjects = jsonObjects(topLevelJsonArray(document.json, "buffers"));
    const std::vector<std::string_view> viewObjects = jsonObjects(topLevelJsonArray(document.json, "bufferViews"));
    const std::vector<std::string_view> accessorObjects = jsonObjects(topLevelJsonArray(document.json, "accessors"));
    const std::vector<std::string_view> meshObjects = jsonObjects(topLevelJsonArray(document.json, "meshes"));
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
            const float textureIndex = options.usePrimitiveMaterialTextures
                ? static_cast<float>(unsignedField(primitive, "material").value_or(0) + 1)
                : 0.0f;
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
                    .textureIndex = textureIndex,
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
        vertex.normal = normalize(Vec3 {
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

SkinnedMeshData loadGltfSkinnedMesh(const std::filesystem::path& path, GltfMeshLoadOptions options)
{
    GltfDocument document = loadDocument(path);
    const std::vector<std::string_view> bufferObjects = jsonObjects(topLevelJsonArray(document.json, "buffers"));
    const std::vector<std::string_view> viewObjects = jsonObjects(topLevelJsonArray(document.json, "bufferViews"));
    const std::vector<std::string_view> accessorObjects = jsonObjects(topLevelJsonArray(document.json, "accessors"));
    const std::vector<std::string_view> meshObjects = jsonObjects(topLevelJsonArray(document.json, "meshes"));
    const std::vector<std::string_view> skinObjects = jsonObjects(topLevelJsonArray(document.json, "skins"));
    if (bufferObjects.size() != 1 || viewObjects.empty() || accessorObjects.empty() || meshObjects.empty() || skinObjects.empty()) {
        throw std::runtime_error("Unsupported or empty skinned glTF document");
    }

    ensureBufferLoaded(document, path, bufferObjects);
    const std::vector<BufferView> bufferViews = parseBufferViews(viewObjects);
    const std::vector<Accessor> accessors = parseAccessors(accessorObjects);
    const std::vector<GltfNode> sourceNodes = parseNodes(document.json);
    const std::vector<int> parents = parentIndicesForNodes(sourceNodes);

    SkinnedMeshData mesh;
    mesh.preserveAspectRatio = options.preserveAspectRatio;
    mesh.rotateHalfTurn = options.rotateHalfTurn;
    mesh.nodes.reserve(sourceNodes.size());
    for (size_t i = 0; i < sourceNodes.size(); ++i) {
        mesh.nodes.push_back({
            .name = sourceNodes[i].name,
            .parent = parents[i],
            .translation = sourceNodes[i].translation,
            .rotation = sourceNodes[i].rotation,
            .scale = sourceNodes[i].scale,
        });
    }

    mesh.jointNodeIndices = unsignedArrayField(skinObjects.front(), "joints");
    if (mesh.jointNodeIndices.empty()) {
        throw std::runtime_error("Skinned glTF document has no joints");
    }

    const size_t inverseBindAccessorIndex = requiredUnsignedField(skinObjects.front(), "inverseBindMatrices");
    if (inverseBindAccessorIndex >= accessors.size()) {
        throw std::runtime_error("Skin references an invalid inverse bind accessor");
    }
    const Accessor& inverseBindAccessor = accessors[inverseBindAccessorIndex];
    if (inverseBindAccessor.bufferView >= bufferViews.size() ||
        inverseBindAccessor.count != mesh.jointNodeIndices.size()) {
        throw std::runtime_error("Skin inverse bind matrix count does not match joints");
    }
    mesh.inverseBindMatrices.reserve(inverseBindAccessor.count);
    for (size_t i = 0; i < inverseBindAccessor.count; ++i) {
        mesh.inverseBindMatrices.push_back(readMat4(
            inverseBindAccessor,
            bufferViews[inverseBindAccessor.bufferView],
            document.buffer,
            i));
    }

    SourceBounds bounds;
    for (std::string_view meshObject : meshObjects) {
        for (std::string_view primitive : jsonObjects(jsonArray(meshObject, "primitives"))) {
            const size_t positionIndex = requiredUnsignedField(primitive, "POSITION");
            const size_t normalIndex = requiredUnsignedField(primitive, "NORMAL");
            const size_t uvIndex = requiredUnsignedField(primitive, "TEXCOORD_0");
            const size_t jointsIndex = requiredUnsignedField(primitive, "JOINTS_0");
            const size_t weightsIndex = requiredUnsignedField(primitive, "WEIGHTS_0");
            const size_t indicesIndex = requiredUnsignedField(primitive, "indices");
            if (positionIndex >= accessors.size() ||
                normalIndex >= accessors.size() ||
                uvIndex >= accessors.size() ||
                jointsIndex >= accessors.size() ||
                weightsIndex >= accessors.size() ||
                indicesIndex >= accessors.size()) {
                throw std::runtime_error("glTF skinned primitive references an invalid accessor");
            }

            const Accessor& positions = accessors[positionIndex];
            const Accessor& normals = accessors[normalIndex];
            const Accessor& uvs = accessors[uvIndex];
            const Accessor& joints = accessors[jointsIndex];
            const Accessor& weights = accessors[weightsIndex];
            const Accessor& indices = accessors[indicesIndex];
            if (positions.count != normals.count ||
                positions.count != uvs.count ||
                positions.count != joints.count ||
                positions.count != weights.count ||
                positions.bufferView >= bufferViews.size() ||
                normals.bufferView >= bufferViews.size() ||
                uvs.bufferView >= bufferViews.size() ||
                joints.bufferView >= bufferViews.size() ||
                weights.bufferView >= bufferViews.size() ||
                indices.bufferView >= bufferViews.size()) {
                throw std::runtime_error("Incompatible skinned glTF primitive accessors");
            }

            const uint32_t baseVertex = static_cast<uint32_t>(mesh.vertices.size());
            mesh.vertices.reserve(mesh.vertices.size() + positions.count);
            for (size_t index = 0; index < positions.count; ++index) {
                const Vec3 position = readVec3(positions, bufferViews[positions.bufferView], document.buffer, index);
                const Vec3 normal = readVec3(normals, bufferViews[normals.bufferView], document.buffer, index);
                auto jointValues = readJoints(joints, bufferViews[joints.bufferView], document.buffer, index);
                auto weightValues = readWeights(weights, bufferViews[weights.bufferView], document.buffer, index);
                const float totalWeight = weightValues[0] + weightValues[1] + weightValues[2] + weightValues[3];
                if (totalWeight > 0.000001f) {
                    for (float& weight : weightValues) {
                        weight /= totalWeight;
                    }
                } else {
                    jointValues = {};
                    weightValues = { 1.0f, 0.0f, 0.0f, 0.0f };
                }
                mesh.vertices.push_back({
                    .position = position,
                    .normal = normal,
                    .uv = readVec2(uvs, bufferViews[uvs.bufferView], document.buffer, index),
                    .joints = jointValues,
                    .weights = weightValues,
                });
                includeBounds(bounds, position);
            }

            mesh.indices.reserve(mesh.indices.size() + indices.count);
            for (size_t index = 0; index < indices.count; ++index) {
                const uint32_t sourceIndex = readIndex(indices, bufferViews[indices.bufferView], document.buffer, index);
                if (sourceIndex >= positions.count) {
                    throw std::runtime_error("glTF index references an invalid skinned vertex");
                }
                mesh.indices.push_back(baseVertex + sourceIndex);
            }
        }
    }

    if (mesh.vertices.empty() || mesh.indices.empty() || mesh.indices.size() % 3 != 0) {
        throw std::runtime_error("Only non-empty triangle-list skinned glTF meshes are supported");
    }
    mesh.sourceMinimum = bounds.minimum;
    mesh.sourceMaximum = bounds.maximum;
    return mesh;
}

std::vector<std::string> listGltfAnimationNames(const std::filesystem::path& path)
{
    std::vector<std::string> names;
    try {
        const GltfDocument document = loadDocument(path);
        const std::vector<std::string_view> animationObjects =
            jsonObjects(topLevelJsonArray(document.json, "animations"));
        names.reserve(animationObjects.size());
        for (size_t i = 0; i < animationObjects.size(); ++i) {
            names.push_back(stringField(animationObjects[i], "name")
                    .value_or("animation " + std::to_string(i + 1)));
        }
    } catch (const std::exception&) {
        names.clear();
    }

    return names;
}

GltfAnimationClip loadGltfAnimationClip(const std::filesystem::path& path, uint32_t animationIndex)
{
    GltfDocument document = loadDocument(path);
    const std::vector<std::string_view> bufferObjects = jsonObjects(topLevelJsonArray(document.json, "buffers"));
    const std::vector<std::string_view> viewObjects = jsonObjects(topLevelJsonArray(document.json, "bufferViews"));
    const std::vector<std::string_view> accessorObjects = jsonObjects(topLevelJsonArray(document.json, "accessors"));
    const std::vector<std::string_view> animationObjects = jsonObjects(topLevelJsonArray(document.json, "animations"));
    if (bufferObjects.size() != 1 || viewObjects.empty() || accessorObjects.empty() || animationObjects.empty()) {
        throw std::runtime_error("Unsupported or empty animation glTF document");
    }
    if (animationIndex >= animationObjects.size()) {
        throw std::runtime_error("Requested glTF animation index is out of range");
    }

    ensureBufferLoaded(document, path, bufferObjects);
    const std::vector<BufferView> bufferViews = parseBufferViews(viewObjects);
    const std::vector<Accessor> accessors = parseAccessors(accessorObjects);
    const std::vector<GltfNode> nodes = parseNodes(document.json);
    const std::string_view animationObject = animationObjects[animationIndex];
    const std::vector<std::string_view> channelObjects = jsonObjects(jsonArray(animationObject, "channels"));
    const std::vector<std::string_view> samplerObjects = jsonObjects(jsonArray(animationObject, "samplers"));

    GltfAnimationClip clip;
    clip.name = stringField(animationObject, "name").value_or("");
    clip.channels.reserve(channelObjects.size());
    for (std::string_view channelObject : channelObjects) {
        const size_t samplerIndex = requiredUnsignedField(channelObject, "sampler");
        const size_t nodeIndex = requiredUnsignedField(channelObject, "node");
        const std::string pathName = requiredStringField(channelObject, "path");
        if (samplerIndex >= samplerObjects.size() || nodeIndex >= nodes.size()) {
            throw std::runtime_error(
                "glTF animation channel references an invalid sampler or node: sampler " +
                std::to_string(samplerIndex) +
                " / " +
                std::to_string(samplerObjects.size()) +
                ", node " +
                std::to_string(nodeIndex) +
                " / " +
                std::to_string(nodes.size()));
        }

        AnimationChannelPath channelPath = AnimationChannelPath::Translation;
        if (pathName == "rotation") {
            channelPath = AnimationChannelPath::Rotation;
        } else if (pathName == "scale") {
            channelPath = AnimationChannelPath::Scale;
        } else if (pathName != "translation") {
            continue;
        }

        const std::string_view samplerObject = samplerObjects[samplerIndex];
        const size_t inputIndex = requiredUnsignedField(samplerObject, "input");
        const size_t outputIndex = requiredUnsignedField(samplerObject, "output");
        if (inputIndex >= accessors.size() || outputIndex >= accessors.size()) {
            throw std::runtime_error("glTF animation sampler references an invalid accessor");
        }
        const Accessor& input = accessors[inputIndex];
        const Accessor& output = accessors[outputIndex];
        if (input.bufferView >= bufferViews.size() || output.bufferView >= bufferViews.size() || input.count != output.count) {
            throw std::runtime_error("Incompatible glTF animation sampler accessors");
        }

        AnimationChannel channel;
        channel.targetNodeName = nodes[nodeIndex].name;
        channel.path = channelPath;
        channel.keyframes.times.reserve(input.count);
        channel.keyframes.values.reserve(output.count);
        for (size_t i = 0; i < input.count; ++i) {
            const float keyTime = readScalarFloat(input, bufferViews[input.bufferView], document.buffer, i);
            channel.keyframes.times.push_back(keyTime);
            clip.durationSeconds = std::max(clip.durationSeconds, keyTime);
            if (channelPath == AnimationChannelPath::Rotation) {
                channel.keyframes.values.push_back(normalize(readVec4(output, bufferViews[output.bufferView], document.buffer, i)));
            } else {
                const Vec3 value = readVec3(output, bufferViews[output.bufferView], document.buffer, i);
                channel.keyframes.values.push_back({ value.x, value.y, value.z, 0.0f });
            }
        }
        clip.channels.push_back(std::move(channel));
    }

    return clip;
}

namespace {

struct NodePose {
    Vec3 translation {};
    Vec4 rotation { 0.0f, 0.0f, 0.0f, 1.0f };
    Vec3 scale { 1.0f, 1.0f, 1.0f };
};

std::vector<NodePose> sampleAnimationPoses(const SkinnedMeshData& mesh, const GltfAnimationClip& animation, float timeSeconds)
{
    if (animation.durationSeconds > 0.000001f) {
        timeSeconds = std::fmod(timeSeconds, animation.durationSeconds);
        if (timeSeconds < 0.0f) {
            timeSeconds += animation.durationSeconds;
        }
    } else {
        timeSeconds = 0.0f;
    }

    std::vector<NodePose> poses;
    poses.reserve(mesh.nodes.size());
    std::unordered_map<std::string, size_t> nodeByName;
    for (size_t i = 0; i < mesh.nodes.size(); ++i) {
        poses.push_back({
            .translation = mesh.nodes[i].translation,
            .rotation = mesh.nodes[i].rotation,
            .scale = mesh.nodes[i].scale,
        });
        if (!mesh.nodes[i].name.empty()) {
            nodeByName[mesh.nodes[i].name] = i;
        }
    }

    for (const AnimationChannel& channel : animation.channels) {
        const auto nodeIt = nodeByName.find(channel.targetNodeName);
        if (nodeIt == nodeByName.end()) {
            continue;
        }
        NodePose& pose = poses[nodeIt->second];
        const Vec4 value = sampleKeyframes(channel.keyframes, timeSeconds, channel.path == AnimationChannelPath::Rotation);
        switch (channel.path) {
        case AnimationChannelPath::Translation:
            pose.translation = { value.x, value.y, value.z };
            break;
        case AnimationChannelPath::Rotation:
            pose.rotation = normalize(value);
            break;
        case AnimationChannelPath::Scale:
            pose.scale = { value.x, value.y, value.z };
            break;
        }
    }

    return poses;
}

Vec3 lerpVec3(Vec3 a, Vec3 b, float t)
{
    return {
        a.x + (b.x - a.x) * t,
        a.y + (b.y - a.y) * t,
        a.z + (b.z - a.z) * t,
    };
}

Vec4 blendRotation(Vec4 a, Vec4 b, float t)
{
    // Normalized lerp along the shortest arc; plenty for short crossfades.
    const float dot = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    if (dot < 0.0f) {
        b = { -b.x, -b.y, -b.z, -b.w };
    }
    return normalize(Vec4 {
        a.x + (b.x - a.x) * t,
        a.y + (b.y - a.y) * t,
        a.z + (b.z - a.z) * t,
        a.w + (b.w - a.w) * t,
    });
}

MeshData skinWithPoses(const SkinnedMeshData& mesh, const std::vector<NodePose>& poses)
{
    if (mesh.nodes.empty() || mesh.jointNodeIndices.empty() || mesh.inverseBindMatrices.size() != mesh.jointNodeIndices.size()) {
        throw std::runtime_error("Cannot skin an incomplete glTF mesh");
    }

    std::vector<Mat4> localMatrices(mesh.nodes.size(), identityMatrix());
    std::vector<Mat4> globalMatrices(mesh.nodes.size(), identityMatrix());
    for (size_t i = 0; i < mesh.nodes.size(); ++i) {
        localMatrices[i] = matrixFromTrs(poses[i].translation, poses[i].rotation, poses[i].scale);
    }
    std::vector<bool> globalComputed(mesh.nodes.size(), false);
    auto computeGlobal = [&](auto&& self, size_t nodeIndex) -> Mat4 {
        if (globalComputed[nodeIndex]) {
            return globalMatrices[nodeIndex];
        }

        const int parent = mesh.nodes[nodeIndex].parent;
        globalMatrices[nodeIndex] = parent >= 0 && static_cast<size_t>(parent) < globalMatrices.size()
            ? multiply(self(self, static_cast<size_t>(parent)), localMatrices[nodeIndex])
            : localMatrices[nodeIndex];
        globalComputed[nodeIndex] = true;
        return globalMatrices[nodeIndex];
    };
    for (size_t i = 0; i < mesh.nodes.size(); ++i) {
        computeGlobal(computeGlobal, i);
    }

    std::vector<Mat4> jointMatrices;
    jointMatrices.reserve(mesh.jointNodeIndices.size());
    for (size_t i = 0; i < mesh.jointNodeIndices.size(); ++i) {
        const uint32_t nodeIndex = mesh.jointNodeIndices[i];
        jointMatrices.push_back(nodeIndex < globalMatrices.size()
                ? multiply(globalMatrices[nodeIndex], mesh.inverseBindMatrices[i])
                : identityMatrix());
    }

    SourceBounds bounds;
    bounds.minimum = mesh.sourceMinimum;
    bounds.maximum = mesh.sourceMaximum;
    GltfMeshLoadOptions options {
        .preserveAspectRatio = mesh.preserveAspectRatio,
        .rotateHalfTurn = mesh.rotateHalfTurn,
    };

    MeshData result;
    result.indices = mesh.indices;
    result.vertices.resize(mesh.vertices.size());
    // Each vertex writes only its own output slot, so chunks parallelize
    // freely; small meshes run inline via the minChunk threshold.
    taskSystem().parallelFor(mesh.vertices.size(), 2048, [&](size_t begin, size_t end) {
        for (size_t vertexIndex = begin; vertexIndex < end; ++vertexIndex) {
            const SkinnedVertex& source = mesh.vertices[vertexIndex];
            Vec3 skinnedPosition {};
            Vec3 skinnedNormal {};
            for (size_t i = 0; i < 4; ++i) {
                const float weight = source.weights[i];
                const uint16_t joint = source.joints[i];
                if (weight <= 0.0f || joint >= jointMatrices.size()) {
                    continue;
                }
                skinnedPosition = add(skinnedPosition, multiply(transformPoint(jointMatrices[joint], source.position), weight));
                skinnedNormal = add(skinnedNormal, multiply(transformVector(jointMatrices[joint], source.normal), weight));
            }
            if (skinnedNormal.x == 0.0f && skinnedNormal.y == 0.0f && skinnedNormal.z == 0.0f) {
                skinnedNormal = source.normal;
            }
            result.vertices[vertexIndex] = normalizedVertex(
                skinnedPosition,
                normalize(skinnedNormal),
                source.uv,
                0.0f,
                bounds,
                options);
        }
    });

    return result;
}

} // namespace

MeshData skinGltfMesh(const SkinnedMeshData& mesh, const GltfAnimationClip& animation, float timeSeconds)
{
    return skinWithPoses(mesh, sampleAnimationPoses(mesh, animation, timeSeconds));
}

MeshData skinGltfMeshBlended(
    const SkinnedMeshData& mesh,
    const GltfAnimationClip& fromAnimation,
    float fromTimeSeconds,
    const GltfAnimationClip& toAnimation,
    float toTimeSeconds,
    float blend)
{
    blend = std::clamp(blend, 0.0f, 1.0f);
    std::vector<NodePose> poses = sampleAnimationPoses(mesh, fromAnimation, fromTimeSeconds);
    const std::vector<NodePose> target = sampleAnimationPoses(mesh, toAnimation, toTimeSeconds);
    for (size_t i = 0; i < poses.size() && i < target.size(); ++i) {
        poses[i].translation = lerpVec3(poses[i].translation, target[i].translation, blend);
        poses[i].rotation = blendRotation(poses[i].rotation, target[i].rotation, blend);
        poses[i].scale = lerpVec3(poses[i].scale, target[i].scale, blend);
    }
    return skinWithPoses(mesh, poses);
}

} // namespace sokoban

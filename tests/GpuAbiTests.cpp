// Proves the compiled shaders and the C++ structs agree on GPU memory layout.
//
// VulkanRenderConstants.hpp already pins the CPU side: sizes, alignments and
// every field offset carry a static_assert. Nothing pinned the GPU side. The
// shaders declare the same records again in GLSL, and the only check that the
// two descriptions agreed was a person running spirv-dis once and reading the
// offsets - which is the right check, it just never ran again.
//
// So this test runs it. It reads the offsets straight out of the .spv modules
// the build produced and compares them to offsetof() on the matching struct.
// That catches more than a shared C++/GLSL header would: a header guarantees
// the field list, while this guarantees the layout the driver will actually
// see. Add a bare `float` to a block some day and C++ packs it at offset 4
// while std140 gives the next vec4 offset 16 - same field list, different
// memory, and only this catches it.
//
// Every value on the C++ side is read out of the type with offsetof, sizeof
// and decltype rather than spelled as a literal, so changing a struct moves
// the expectation with it instead of requiring this file to be edited to
// agree. Every .spv the build produced is inspected, not a chosen few, so a
// block that gets re-declared locally in one shader is compared too.
//
// Verified against deliberate mutations of the GLSL side. Caught: a member
// added to a block; a bare float that changes an std140 array stride; a
// vec4/uvec4 swap; a block moved to a different binding.
//
// Known blind spot: swapping two members of the *same* type - two vec4 lanes,
// say - produces identical offsets and identical component types, so nothing
// here can see it. Catching that needs member names, and names are exactly
// what -O strips from the modules the game actually ships. Compiling a second,
// unoptimized copy purely to read names back was judged not worth doubling the
// shader build for; if that trade ever changes, this is the place it changes.
//
// Blocks are matched by descriptor binding for the same reason: with names
// gone, the binding is the only stable identity a module still carries.

#include "engine/render/GpuSkinning.hpp"
#include "engine/render/VulkanRenderConstants.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#ifndef SOKOBAN_TEST_SHADER_DIR
#error "SOKOBAN_TEST_SHADER_DIR must name the directory holding the built .spv modules"
#endif

namespace {

using namespace sokoban;

int failures = 0;
int checks = 0;
const char* currentTest = "";

void checkImpl(bool ok, const char* expression, int line)
{
    ++checks;
    if (!ok) {
        ++failures;
        std::cerr << "FAIL [" << currentTest << "] line "
                  << line << ": " << expression << '\n';
    }
}

#define CHECK(expression) checkImpl((expression), #expression, __LINE__)
#define TEST(name) currentTest = name

// offsetof is only guaranteed on standard-layout types. Every block below is
// one; saying so here means a future member that breaks it fails to compile
// rather than producing an unspecified offset.
static_assert(std::is_standard_layout_v<PointLightUniform>);
static_assert(std::is_standard_layout_v<SceneFrameUniform>);
static_assert(std::is_standard_layout_v<GpuDrawInstance>);
static_assert(std::is_standard_layout_v<GpuMaterial>);
static_assert(std::is_standard_layout_v<GpuSkinningInstance>);

// ---------------------------------------------------------------- SPIR-V

// SPIR-V is a flat stream of 32-bit words: a five-word header, then
// instructions whose first word packs a length and an opcode. Only the
// decorations that describe layout are read here, so this stays a reader
// rather than a parser - it never has to understand the program.
constexpr uint32_t spirvMagic = 0x07230203u;

constexpr uint32_t opTypeInt = 21u;
constexpr uint32_t opTypeFloat = 22u;
constexpr uint32_t opTypeVector = 23u;
constexpr uint32_t opTypeMatrix = 24u;
constexpr uint32_t opTypeArray = 28u;
constexpr uint32_t opTypeRuntimeArray = 29u;
constexpr uint32_t opTypeStruct = 30u;
constexpr uint32_t opTypePointer = 32u;
constexpr uint32_t opVariable = 59u;
constexpr uint32_t opDecorate = 71u;
constexpr uint32_t opMemberDecorate = 72u;

// What a member is made of, once arrays, matrices and vectors are peeled off.
// Offsets alone cannot tell a vec4 from a uvec4 - they occupy the same lane -
// so a swap between the two would otherwise be invisible.
enum class ScalarKind { Unknown, Float, Signed, Unsigned, Aggregate };

const char* nameOf(ScalarKind kind)
{
    switch (kind) {
    case ScalarKind::Float: return "float";
    case ScalarKind::Signed: return "int";
    case ScalarKind::Unsigned: return "uint";
    case ScalarKind::Aggregate: return "struct";
    case ScalarKind::Unknown: break;
    }
    return "unknown";
}

// The C++ member type decides the expectation, so changing a member's type
// moves both sides at once rather than needing the test edited to match.
template <typename T>
constexpr ScalarKind scalarKindOf();
template <>
constexpr ScalarKind scalarKindOf<Vec4>() { return ScalarKind::Float; }
template <>
constexpr ScalarKind scalarKindOf<Mat4>() { return ScalarKind::Float; }
template <>
constexpr ScalarKind scalarKindOf<GpuMaterialUint4>()
{
    return ScalarKind::Unsigned;
}
template <>
constexpr ScalarKind scalarKindOf<PointLightUniform>()
{
    return ScalarKind::Aggregate;
}

// Peels std::array<T, N> down to T so a member can be described by what it
// ultimately holds.
template <typename T>
struct ElementOf {
    using type = T;
};
template <typename T, std::size_t N>
struct ElementOf<std::array<T, N>> {
    using type = T;
};

#define SOKOBAN_MEMBER_KIND(Struct, member) \
    scalarKindOf<ElementOf<decltype(Struct::member)>::type>()

constexpr uint32_t decorationArrayStride = 6u;
constexpr uint32_t decorationBinding = 33u;
constexpr uint32_t decorationOffset = 35u;

class SpirvModule {
public:
    // One interface block as the module declares it.
    struct Block {
        std::vector<uint32_t> memberOffsets;
        // Stride between elements when the block wraps an array of records,
        // which is how every storage buffer here is shaped. Absent for a
        // uniform block, whose members sit directly in the block.
        std::optional<uint32_t> elementStride;
        // Stride of an array member inside the block, by member index. This
        // is where std140 differs from C++ most often.
        std::map<uint32_t, uint32_t> memberArrayStrides;
        // What each member is made of, in member order.
        std::vector<ScalarKind> memberKinds;
    };

    explicit SpirvModule(const std::filesystem::path& path)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file) {
            return;
        }
        std::vector<char> bytes(
            (std::istreambuf_iterator<char>(file)),
            std::istreambuf_iterator<char>());
        if (bytes.size() < 20 || bytes.size() % 4 != 0) {
            return;
        }
        std::vector<uint32_t> words(bytes.size() / 4);
        for (std::size_t i = 0; i < words.size(); ++i) {
            // Always little-endian: the magic number is what would reveal a
            // byte-swapped module, and this rejects that below rather than
            // silently misreading it.
            words[i] = static_cast<uint32_t>(
                    static_cast<unsigned char>(bytes[i * 4])) |
                (static_cast<uint32_t>(
                     static_cast<unsigned char>(bytes[i * 4 + 1])) << 8) |
                (static_cast<uint32_t>(
                     static_cast<unsigned char>(bytes[i * 4 + 2])) << 16) |
                (static_cast<uint32_t>(
                     static_cast<unsigned char>(bytes[i * 4 + 3])) << 24);
        }
        if (words[0] != spirvMagic) {
            return;
        }
        valid_ = read(words);
    }

    [[nodiscard]] bool valid() const { return valid_; }

    // The block bound at `binding` in descriptor set 0, if this module
    // declares one. A module that never uses a block does not declare it,
    // which is why the caller checks coverage across the whole set.
    [[nodiscard]] std::optional<Block> blockAtBinding(uint32_t binding) const
    {
        for (const auto& [variable, bound] : bindings_) {
            if (bound != binding) {
                continue;
            }
            const auto pointer = variableTypes_.find(variable);
            if (pointer == variableTypes_.end()) {
                continue;
            }
            const auto pointee = pointees_.find(pointer->second);
            if (pointee == pointees_.end()) {
                continue;
            }
            return blockFrom(pointee->second);
        }
        return std::nullopt;
    }

private:
    bool read(const std::vector<uint32_t>& words)
    {
        std::size_t index = 5;
        while (index < words.size()) {
            const uint32_t header = words[index];
            const uint32_t length = header >> 16;
            const uint32_t opcode = header & 0xFFFFu;
            if (length == 0 || index + length > words.size()) {
                return false;
            }
            const uint32_t* operands = words.data() + index + 1;
            const uint32_t count = length - 1;
            switch (opcode) {
            case opDecorate:
                if (count >= 3 && operands[1] == decorationArrayStride) {
                    strides_[operands[0]] = operands[2];
                } else if (count >= 3 && operands[1] == decorationBinding) {
                    bindings_[operands[0]] = operands[2];
                }
                break;
            case opMemberDecorate:
                if (count >= 4 && operands[2] == decorationOffset) {
                    offsets_[operands[0]][operands[1]] = operands[3];
                }
                break;
            case opTypeStruct:
                structMembers_[operands[0]].assign(
                    operands + 1, operands + count);
                break;
            case opTypeArray:
            case opTypeRuntimeArray:
                if (count >= 2) {
                    arrayElements_[operands[0]] = operands[1];
                }
                break;
            case opTypeFloat:
                if (count >= 1) {
                    scalars_[operands[0]] = ScalarKind::Float;
                }
                break;
            case opTypeInt:
                if (count >= 3) {
                    scalars_[operands[0]] = operands[2] != 0
                        ? ScalarKind::Signed
                        : ScalarKind::Unsigned;
                }
                break;
            case opTypeVector:
            case opTypeMatrix:
                // Both name their component type in the same operand slot.
                if (count >= 2) {
                    components_[operands[0]] = operands[1];
                }
                break;
            case opTypePointer:
                if (count >= 3) {
                    pointees_[operands[0]] = operands[2];
                }
                break;
            case opVariable:
                // Result *type* first, then the result id.
                if (count >= 2) {
                    variableTypes_[operands[1]] = operands[0];
                }
                break;
            default:
                break;
            }
            index += length;
        }
        return true;
    }

    [[nodiscard]] Block blockFrom(uint32_t blockType) const
    {
        uint32_t layoutType = blockType;
        std::optional<uint32_t> elementStride;
        // A storage buffer is a block wrapping one runtime array of records;
        // a uniform block holds its members directly. Descend only in the
        // first shape, and decide by structure rather than by storage class
        // so an added member cannot silently change which struct is read.
        const auto members = structMembers_.find(blockType);
        if (members != structMembers_.end() && members->second.size() == 1) {
            const auto element = arrayElements_.find(members->second[0]);
            if (element != arrayElements_.end()) {
                layoutType = element->second;
                const auto stride = strides_.find(members->second[0]);
                if (stride != strides_.end()) {
                    elementStride = stride->second;
                }
            }
        }

        Block block;
        block.elementStride = elementStride;
        const auto found = offsets_.find(layoutType);
        if (found != offsets_.end()) {
            for (const auto& [member, offset] : found->second) {
                block.memberOffsets.push_back(offset);
            }
        }
        const auto layoutMembers = structMembers_.find(layoutType);
        if (layoutMembers != structMembers_.end()) {
            for (uint32_t member = 0;
                 member < layoutMembers->second.size();
                 ++member) {
                const uint32_t memberType = layoutMembers->second[member];
                const auto stride = strides_.find(memberType);
                if (stride != strides_.end()) {
                    block.memberArrayStrides[member] = stride->second;
                }
                block.memberKinds.push_back(scalarKindFor(memberType));
            }
        }
        return block;
    }

    // Follows arrays, matrices and vectors down to the scalar they hold. A
    // member that bottoms out in another struct reports Aggregate rather than
    // descending, because its own members are checked as their own block.
    [[nodiscard]] ScalarKind scalarKindFor(uint32_t type) const
    {
        for (int step = 0; step < 8; ++step) {
            const auto scalar = scalars_.find(type);
            if (scalar != scalars_.end()) {
                return scalar->second;
            }
            if (structMembers_.count(type) != 0) {
                return ScalarKind::Aggregate;
            }
            const auto array = arrayElements_.find(type);
            if (array != arrayElements_.end()) {
                type = array->second;
                continue;
            }
            const auto component = components_.find(type);
            if (component != components_.end()) {
                type = component->second;
                continue;
            }
            break;
        }
        return ScalarKind::Unknown;
    }

    bool valid_ = false;
    // std::map keeps member offsets in member order without sorting later.
    std::map<uint32_t, std::map<uint32_t, uint32_t>> offsets_;
    std::map<uint32_t, uint32_t> strides_;
    std::map<uint32_t, uint32_t> bindings_;
    std::map<uint32_t, uint32_t> variableTypes_;
    std::map<uint32_t, uint32_t> pointees_;
    std::map<uint32_t, uint32_t> arrayElements_;
    std::map<uint32_t, std::vector<uint32_t>> structMembers_;
    std::map<uint32_t, ScalarKind> scalars_;
    std::map<uint32_t, uint32_t> components_;
};

// ------------------------------------------------------------- expected

// Every value here comes from the C++ type. Spelling an offset as a literal
// would make the test agree with itself rather than with the struct.
struct ExpectedBlock {
    uint32_t binding;
    const char* name;
    std::vector<uint32_t> memberOffsets;
    std::optional<uint32_t> elementStride;
    std::map<uint32_t, uint32_t> memberArrayStrides;
    std::vector<ScalarKind> memberKinds;
};

std::vector<ExpectedBlock> expectedBlocks()
{
    return {
        ExpectedBlock {
            .binding = 7,
            .name = "SceneFrameUniform",
            .memberOffsets = {
                offsetof(SceneFrameUniform, clipFromWorld),
                offsetof(SceneFrameUniform, shadowFromWorld),
                offsetof(SceneFrameUniform, cameraPositionAndNearPlane),
                offsetof(SceneFrameUniform, pointLights),
                offsetof(SceneFrameUniform, pointLightMeta),
            },
            // A uniform block: its members sit directly in the block.
            .elementStride = std::nullopt,
            // The std140 trap. An array of 48-byte structs keeps a 48-byte
            // stride only because 48 is already a multiple of 16; a member
            // that broke that would silently move every light after the first.
            .memberArrayStrides = { { 3u, sizeof(PointLightUniform) } },
            .memberKinds = {
                SOKOBAN_MEMBER_KIND(SceneFrameUniform, clipFromWorld),
                SOKOBAN_MEMBER_KIND(SceneFrameUniform, shadowFromWorld),
                SOKOBAN_MEMBER_KIND(SceneFrameUniform, cameraPositionAndNearPlane),
                SOKOBAN_MEMBER_KIND(SceneFrameUniform, pointLights),
                SOKOBAN_MEMBER_KIND(SceneFrameUniform, pointLightMeta),
            },
        },
        ExpectedBlock {
            .binding = 9,
            .name = "GpuSkinningInstance",
            .memberOffsets = {
                offsetof(GpuSkinningInstance, palette),
                offsetof(GpuSkinningInstance, modelFromSource),
                offsetof(GpuSkinningInstance, normalFromSource),
            },
            .elementStride = sizeof(GpuSkinningInstance),
            .memberArrayStrides = { { 0u, sizeof(Mat4) } },
            .memberKinds = {
                SOKOBAN_MEMBER_KIND(GpuSkinningInstance, palette),
                SOKOBAN_MEMBER_KIND(GpuSkinningInstance, modelFromSource),
                SOKOBAN_MEMBER_KIND(GpuSkinningInstance, normalFromSource),
            },
        },
        ExpectedBlock {
            .binding = 10,
            .name = "GpuDrawInstance",
            .memberOffsets = {
                offsetof(GpuDrawInstance, vertices),
                offsetof(GpuDrawInstance, passData),
                offsetof(GpuDrawInstance, color),
                offsetof(GpuDrawInstance, normalAndAmbientRed),
                offsetof(GpuDrawInstance, sunDirectionAndAmbientGreen),
                offsetof(GpuDrawInstance, sunRadianceAndAmbientBlue),
                offsetof(GpuDrawInstance, shadowOptions),
                offsetof(GpuDrawInstance, materialOptions),
                offsetof(GpuDrawInstance, gridColor),
                offsetof(GpuDrawInstance, textureOptions),
            },
            .elementStride = sizeof(GpuDrawInstance),
            .memberArrayStrides = {
                { 0u, sizeof(Vec4) },
                { 1u, sizeof(Vec4) },
            },
            .memberKinds = {
                SOKOBAN_MEMBER_KIND(GpuDrawInstance, vertices),
                SOKOBAN_MEMBER_KIND(GpuDrawInstance, passData),
                SOKOBAN_MEMBER_KIND(GpuDrawInstance, color),
                SOKOBAN_MEMBER_KIND(GpuDrawInstance, normalAndAmbientRed),
                SOKOBAN_MEMBER_KIND(GpuDrawInstance, sunDirectionAndAmbientGreen),
                SOKOBAN_MEMBER_KIND(GpuDrawInstance, sunRadianceAndAmbientBlue),
                SOKOBAN_MEMBER_KIND(GpuDrawInstance, shadowOptions),
                SOKOBAN_MEMBER_KIND(GpuDrawInstance, materialOptions),
                SOKOBAN_MEMBER_KIND(GpuDrawInstance, gridColor),
                SOKOBAN_MEMBER_KIND(GpuDrawInstance, textureOptions),
            },
        },
        ExpectedBlock {
            .binding = 12,
            .name = "GpuMaterial",
            .memberOffsets = {
                offsetof(GpuMaterial, baseColorFactor),
                offsetof(GpuMaterial, emissiveAndMetallic),
                offsetof(GpuMaterial, materialScalars),
                offsetof(GpuMaterial, primaryTextureHandles),
                offsetof(GpuMaterial, occlusionTextureAndPadding),
                offsetof(GpuMaterial, textureUvSets),
                offsetof(GpuMaterial, materialState),
            },
            .elementStride = sizeof(GpuMaterial),
            .memberArrayStrides = {},
            // The lane that matters most: three float lanes then four uint
            // lanes. Offsets alone cannot tell those apart.
            .memberKinds = {
                SOKOBAN_MEMBER_KIND(GpuMaterial, baseColorFactor),
                SOKOBAN_MEMBER_KIND(GpuMaterial, emissiveAndMetallic),
                SOKOBAN_MEMBER_KIND(GpuMaterial, materialScalars),
                SOKOBAN_MEMBER_KIND(GpuMaterial, primaryTextureHandles),
                SOKOBAN_MEMBER_KIND(GpuMaterial, occlusionTextureAndPadding),
                SOKOBAN_MEMBER_KIND(GpuMaterial, textureUvSets),
                SOKOBAN_MEMBER_KIND(GpuMaterial, materialState),
            },
        },
    };
}

std::string describe(const std::vector<uint32_t>& values)
{
    std::string result;
    for (const uint32_t value : values) {
        if (!result.empty()) {
            result += ", ";
        }
        result += std::to_string(value);
    }
    return "{ " + result + " }";
}

void compare(
    const std::string& module,
    const ExpectedBlock& expected,
    const SpirvModule::Block& actual)
{
    if (actual.memberOffsets != expected.memberOffsets) {
        ++failures;
        std::cerr << "FAIL [" << currentTest << "] " << module << ": "
                  << expected.name << " member offsets are "
                  << describe(actual.memberOffsets) << ", the C++ struct says "
                  << describe(expected.memberOffsets) << '\n';
    }
    ++checks;

    if (actual.elementStride != expected.elementStride) {
        ++failures;
        std::cerr << "FAIL [" << currentTest << "] " << module << ": "
                  << expected.name << " element stride is "
                  << (actual.elementStride
                          ? std::to_string(*actual.elementStride)
                          : std::string("absent"))
                  << ", the C++ struct says "
                  << (expected.elementStride
                          ? std::to_string(*expected.elementStride)
                          : std::string("absent"))
                  << '\n';
    }
    ++checks;

    if (actual.memberKinds != expected.memberKinds) {
        ++failures;
        const auto spell = [](const std::vector<ScalarKind>& kinds) {
            std::string result;
            for (const ScalarKind kind : kinds) {
                if (!result.empty()) {
                    result += ", ";
                }
                result += nameOf(kind);
            }
            return "{ " + result + " }";
        };
        std::cerr << "FAIL [" << currentTest << "] " << module << ": "
                  << expected.name << " member types are "
                  << spell(actual.memberKinds) << ", the C++ struct says "
                  << spell(expected.memberKinds) << '\n';
    }
    ++checks;

    for (const auto& [member, stride] : expected.memberArrayStrides) {
        const auto found = actual.memberArrayStrides.find(member);
        const bool matches =
            found != actual.memberArrayStrides.end() && found->second == stride;
        if (!matches) {
            ++failures;
            std::cerr << "FAIL [" << currentTest << "] " << module << ": "
                      << expected.name << " member " << member
                      << " array stride is "
                      << (found != actual.memberArrayStrides.end()
                              ? std::to_string(found->second)
                              : std::string("absent"))
                      << ", the C++ struct says " << stride << '\n';
        }
        ++checks;
    }
}

void testCompiledShadersMatchTheCppLayout()
{
    TEST("compiledShadersMatchTheCppLayout");

    const std::filesystem::path shaderDirectory { SOKOBAN_TEST_SHADER_DIR };
    CHECK(std::filesystem::is_directory(shaderDirectory));
    if (!std::filesystem::is_directory(shaderDirectory)) {
        std::cerr << "  no shader directory at " << shaderDirectory.string()
                  << "; build the sokoban_shaders target first\n";
        return;
    }

    const std::vector<ExpectedBlock> expected = expectedBlocks();
    std::map<uint32_t, uint32_t> timesSeen;
    uint32_t modules = 0;

    for (const auto& entry :
         std::filesystem::directory_iterator(shaderDirectory)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".spv") {
            continue;
        }
        const std::string name = entry.path().filename().string();
        const SpirvModule module(entry.path());
        if (!module.valid()) {
            ++failures;
            ++checks;
            std::cerr << "FAIL [" << currentTest << "] " << name
                      << " is not a readable SPIR-V module\n";
            continue;
        }
        ++modules;
        for (const ExpectedBlock& block : expected) {
            const std::optional<SpirvModule::Block> actual =
                module.blockAtBinding(block.binding);
            if (!actual) {
                continue;
            }
            ++timesSeen[block.binding];
            compare(name, block, *actual);
        }
    }

    // Without these, an empty or mis-pointed directory would let every check
    // above pass by never running.
    CHECK(modules > 0);
    for (const ExpectedBlock& block : expected) {
        const bool seen = timesSeen[block.binding] > 0;
        if (!seen) {
            ++failures;
            std::cerr << "FAIL [" << currentTest << "] no compiled shader "
                      << "declares " << block.name << " at binding "
                      << block.binding << ", so its layout went unchecked\n";
        }
        ++checks;
    }

    std::cout << "  inspected " << modules << " modules; ";
    for (const ExpectedBlock& block : expected) {
        std::cout << block.name << " x" << timesSeen[block.binding] << ' ';
    }
    std::cout << '\n';
}

// The reader must reject a file it cannot understand rather than reporting an
// empty layout, which would look exactly like a shader that omits the block.
void testMalformedModulesAreRejected()
{
    TEST("malformedModulesAreRejected");

    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() / "sokoban-gpu-abi-tests";
    std::filesystem::create_directories(directory);

    const auto write = [&directory](
                           const char* name,
                           const std::vector<unsigned char>& bytes) {
        const std::filesystem::path path = directory / name;
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file.write(
            reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
        return path;
    };

    CHECK(!SpirvModule(directory / "absent.spv").valid());
    CHECK(!SpirvModule(write("empty.spv", {})).valid());
    CHECK(!SpirvModule(write("short.spv", { 0x03, 0x02, 0x23, 0x07 })).valid());
    // A correct header whose magic number is wrong: a byte-swapped module.
    CHECK(!SpirvModule(write("swapped.spv",
              { 0x07, 0x23, 0x02, 0x03, 0, 0, 0, 0, 0, 0, 0, 0,
                0, 0, 0, 0, 0, 0, 0, 0 }))
               .valid());
    // Valid header, then an instruction claiming to run past the end.
    CHECK(!SpirvModule(write("truncated.spv",
              { 0x03, 0x02, 0x23, 0x07, 0, 0, 0, 0, 0, 0, 0, 0,
                0, 0, 0, 0, 0, 0, 0, 0,
                0x11, 0x00, 0xFF, 0x00 }))
               .valid());

    std::error_code error;
    std::filesystem::remove_all(directory, error);
}

} // namespace

int main()
{
    testCompiledShadersMatchTheCppLayout();
    testMalformedModulesAreRejected();

    if (failures != 0) {
        std::cerr << failures << " of " << checks << " checks failed\n";
        return 1;
    }
    std::cout << "gpu_abi: " << checks << " checks passed\n";
    return 0;
}

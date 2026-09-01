// The wording of the renderer's Vulkan failure messages.
//
// Six places record a one-shot command buffer and submit it once - geometry
// upload, texture upload, compressed texture upload, frame capture, tile
// thumbnails and UI image upload. Each used to spell out five diagnostic
// literals of the form "<vkFunction> <label> failed"; they now come from
// vkCheck(result, call, label) inside beginOneShotCommands and
// submitOneShotCommands.
//
// That text is the contract. It is what a crash report shows and what someone
// greps for, so a refactor that quietly changed "vkQueueSubmit2 texture upload
// failed" into anything else would be a regression that compiles. This suite
// pins the composition rule and every label in use.
//
// It needs no Vulkan device: vkCheck only formats and throws.

#include "engine/render/VulkanResourceUtils.hpp"

#include "TestHarness.hpp"

#include <string>

using namespace sokoban;

namespace {

std::string thrownBy(VkResult result, const char* call, std::string_view label)
{
    try {
        vkCheck(result, call, label);
    } catch (const VulkanError& error) {
        return error.what();
    }
    return {};
}

std::string thrownBy(VkResult result, const char* message)
{
    try {
        vkCheck(result, message);
    } catch (const VulkanError& error) {
        return error.what();
    }
    return {};
}

// Every label a one-shot site passes, and every call the two helpers make
// through it. Both lists are exhaustive by construction: adding a call to
// either helper without adding it here leaves that message unpinned.
constexpr const char* labels[] {
    "geometry upload",
    "texture upload",
    "compressed texture upload",
    "capture",
    "thumbnail",
    "UI image upload",
};

constexpr const char* calls[] {
    "vkAllocateCommandBuffers",
    "vkBeginCommandBuffer",
    "vkEndCommandBuffer",
    "vkCreateFence",
    "vkQueueSubmit2",
};

void testComposedMessagesMatchTheLiteralsTheyReplaced()
{
    TEST("composed messages");
    for (const char* label : labels) {
        for (const char* call : calls) {
            const std::string literal =
                std::string(call) + ' ' + label + " failed";
            CHECK(
                thrownBy(VK_ERROR_DEVICE_LOST, call, label)
                == thrownBy(VK_ERROR_DEVICE_LOST, literal.c_str()));
        }
    }
}

void testTheResultIsCarriedAndReported()
{
    TEST("VkResult in the message and on the exception");
    const std::string message =
        thrownBy(VK_ERROR_OUT_OF_DEVICE_MEMORY, "vkCreateFence", "thumbnail");
    CHECK(message == "vkCreateFence thumbnail failed (VkResult "
              + std::to_string(static_cast<int>(VK_ERROR_OUT_OF_DEVICE_MEMORY))
              + ")");

    // The typed result survives too, because callers switch on it.
    try {
        vkCheck(VK_ERROR_DEVICE_LOST, "vkQueueSubmit2", "capture");
        CHECK(false);
    } catch (const VulkanError& error) {
        CHECK(error.result() == VK_ERROR_DEVICE_LOST);
    }
}

void testSuccessThrowsNothing()
{
    TEST("success is silent");
    CHECK(thrownBy(VK_SUCCESS, "vkQueueSubmit2", "texture upload").empty());
    CHECK(thrownBy(VK_SUCCESS, "any literal at all").empty());
}

void testAnEmptyLabelStillReadsAsASentence()
{
    TEST("degenerate labels");
    // Not used by anything today, but the separator is unconditional and a
    // reader should know what that produces rather than assume it is guarded.
    CHECK(thrownBy(VK_ERROR_DEVICE_LOST, "vkCreateFence", "")
        == "vkCreateFence  failed (VkResult "
            + std::to_string(static_cast<int>(VK_ERROR_DEVICE_LOST)) + ")");
}

} // namespace

int main()
{
    testComposedMessagesMatchTheLiteralsTheyReplaced();
    testTheResultIsCarriedAndReported();
    testSuccessThrowsNothing();
    testAnEmptyLabelStillReadsAsASentence();

    if (failures != 0) {
        std::cerr << failures << " of " << checks << " checks failed\n";
        return 1;
    }
    std::cout << "vulkan_diagnostics: " << checks << " checks passed\n";
    return 0;
}

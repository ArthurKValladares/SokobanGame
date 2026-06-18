#include "engine/render/VulkanRenderer.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include <algorithm>
#include <array>
#include <fstream>
#include <optional>
#include <set>
#include <stdexcept>

namespace sokoban {
namespace {

constexpr std::array<const char*, 4> requiredDeviceExtensions {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME,
    VK_KHR_PIPELINE_LIBRARY_EXTENSION_NAME,
    VK_EXT_GRAPHICS_PIPELINE_LIBRARY_EXTENSION_NAME,
    VK_EXT_EXTENDED_DYNAMIC_STATE_EXTENSION_NAME,
};

std::vector<const char*> validationLayers()
{
#if SOKOBAN_ENABLE_VALIDATION
    return { "VK_LAYER_KHRONOS_validation" };
#else
    return {};
#endif
}

void vkCheck(VkResult result, const char* message)
{
    if (result != VK_SUCCESS) {
        throw std::runtime_error(std::string(message) + " (VkResult " + std::to_string(result) + ")");
    }
}

std::vector<char> readFile(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file) {
        throw std::runtime_error("Failed to open file: " + path.string());
    }

    const auto size = static_cast<size_t>(file.tellg());
    std::vector<char> data(size);
    file.seekg(0);
    file.read(data.data(), static_cast<std::streamsize>(data.size()));
    return data;
}

bool supportsValidationLayer()
{
    uint32_t layerCount = 0;
    vkCheck(vkEnumerateInstanceLayerProperties(&layerCount, nullptr), "vkEnumerateInstanceLayerProperties failed");

    std::vector<VkLayerProperties> layers(layerCount);
    vkCheck(vkEnumerateInstanceLayerProperties(&layerCount, layers.data()), "vkEnumerateInstanceLayerProperties failed");

    for (const auto& layer : layers) {
        if (std::string(layer.layerName) == "VK_LAYER_KHRONOS_validation") {
            return true;
        }
    }
    return false;
}

} // namespace

VulkanRenderer::VulkanRenderer(SDL_Window* window, std::filesystem::path assetRoot)
    : window_(window)
    , assetRoot_(std::move(assetRoot))
{
    createInstance();
    createSurface();
    pickPhysicalDevice();
    createDevice();
    createSwapchain();
    createImageViews();
    createCommandPool();
    createPipeline();
    createFrameResources();
}

VulkanRenderer::~VulkanRenderer()
{
    if (device_) {
        vkDeviceWaitIdle(device_);
    }

    for (auto& frame : frames_) {
        if (frame.imageAvailable) {
            vkDestroySemaphore(device_, frame.imageAvailable, nullptr);
        }
        if (frame.renderFinished) {
            vkDestroySemaphore(device_, frame.renderFinished, nullptr);
        }
        if (frame.inFlight) {
            vkDestroyFence(device_, frame.inFlight, nullptr);
        }
    }

    if (pipeline_) {
        vkDestroyPipeline(device_, pipeline_, nullptr);
    }
    if (pipelineLibrary_) {
        vkDestroyPipeline(device_, pipelineLibrary_, nullptr);
    }
    if (pipelineLayout_) {
        vkDestroyPipelineLayout(device_, pipelineLayout_, nullptr);
    }
    if (commandPool_) {
        vkDestroyCommandPool(device_, commandPool_, nullptr);
    }

    cleanupSwapchain();

    if (device_) {
        vkDestroyDevice(device_, nullptr);
    }
    if (surface_) {
        SDL_Vulkan_DestroySurface(instance_, surface_, nullptr);
    }
    if (instance_) {
        vkDestroyInstance(instance_, nullptr);
    }
}

void VulkanRenderer::drawFrame(const RenderFrameData& frameData)
{
    auto& frame = frames_[currentFrame_];
    vkCheck(vkWaitForFences(device_, 1, &frame.inFlight, VK_TRUE, UINT64_MAX), "vkWaitForFences failed");

    uint32_t imageIndex = 0;
    VkResult acquired = vkAcquireNextImageKHR(device_, swapchain_, UINT64_MAX, frame.imageAvailable, VK_NULL_HANDLE, &imageIndex);
    if (acquired == VK_ERROR_OUT_OF_DATE_KHR) {
        recreateSwapchain();
        return;
    }
    if (acquired != VK_SUCCESS && acquired != VK_SUBOPTIMAL_KHR) {
        vkCheck(acquired, "vkAcquireNextImageKHR failed");
    }

    vkCheck(vkResetFences(device_, 1, &frame.inFlight), "vkResetFences failed");
    vkCheck(vkResetCommandBuffer(frame.commandBuffer, 0), "vkResetCommandBuffer failed");
    recordCommandBuffer(frame.commandBuffer, imageIndex, frameData);

    VkSemaphoreSubmitInfo waitSemaphore {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
        .semaphore = frame.imageAvailable,
        .stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
    };

    VkCommandBufferSubmitInfo commandBuffer {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
        .commandBuffer = frame.commandBuffer,
    };

    VkSemaphoreSubmitInfo signalSemaphore {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
        .semaphore = frame.renderFinished,
        .stageMask = VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT,
    };

    VkSubmitInfo2 submit {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
        .waitSemaphoreInfoCount = 1,
        .pWaitSemaphoreInfos = &waitSemaphore,
        .commandBufferInfoCount = 1,
        .pCommandBufferInfos = &commandBuffer,
        .signalSemaphoreInfoCount = 1,
        .pSignalSemaphoreInfos = &signalSemaphore,
    };

    vkCheck(vkQueueSubmit2(graphicsQueue_, 1, &submit, frame.inFlight), "vkQueueSubmit2 failed");

    VkPresentInfoKHR present {
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &frame.renderFinished,
        .swapchainCount = 1,
        .pSwapchains = &swapchain_,
        .pImageIndices = &imageIndex,
    };

    const VkResult presented = vkQueuePresentKHR(presentQueue_, &present);
    if (presented == VK_ERROR_OUT_OF_DATE_KHR || presented == VK_SUBOPTIMAL_KHR) {
        recreateSwapchain();
    } else {
        vkCheck(presented, "vkQueuePresentKHR failed");
    }

    currentFrame_ = (currentFrame_ + 1) % maxFramesInFlight_;
}

void VulkanRenderer::waitIdle() const
{
    if (device_) {
        vkDeviceWaitIdle(device_);
    }
}

void VulkanRenderer::createInstance()
{
    VkApplicationInfo appInfo {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "Sokoban 3D",
        .applicationVersion = VK_MAKE_API_VERSION(0, 0, 1, 0),
        .pEngineName = "Sokoban Engine",
        .engineVersion = VK_MAKE_API_VERSION(0, 0, 1, 0),
        .apiVersion = VK_API_VERSION_1_4,
    };

    uint32_t sdlExtensionCount = 0;
    const char* const* sdlExtensions = SDL_Vulkan_GetInstanceExtensions(&sdlExtensionCount);
    if (!sdlExtensions) {
        throw std::runtime_error(std::string("SDL_Vulkan_GetInstanceExtensions failed: ") + SDL_GetError());
    }

    std::vector<const char*> extensions(sdlExtensions, sdlExtensions + sdlExtensionCount);

    auto layers = validationLayers();
    if (!layers.empty() && !supportsValidationLayer()) {
        layers.clear();
    }

    VkInstanceCreateInfo createInfo {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &appInfo,
        .enabledLayerCount = static_cast<uint32_t>(layers.size()),
        .ppEnabledLayerNames = layers.data(),
        .enabledExtensionCount = static_cast<uint32_t>(extensions.size()),
        .ppEnabledExtensionNames = extensions.data(),
    };

    vkCheck(vkCreateInstance(&createInfo, nullptr, &instance_), "vkCreateInstance failed");
}

void VulkanRenderer::createSurface()
{
    if (!SDL_Vulkan_CreateSurface(window_, instance_, nullptr, &surface_)) {
        throw std::runtime_error(std::string("SDL_Vulkan_CreateSurface failed: ") + SDL_GetError());
    }
}

void VulkanRenderer::pickPhysicalDevice()
{
    uint32_t deviceCount = 0;
    vkCheck(vkEnumeratePhysicalDevices(instance_, &deviceCount, nullptr), "vkEnumeratePhysicalDevices failed");
    if (deviceCount == 0) {
        throw std::runtime_error("No Vulkan-capable GPU found");
    }

    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkCheck(vkEnumeratePhysicalDevices(instance_, &deviceCount, devices.data()), "vkEnumeratePhysicalDevices failed");

    for (VkPhysicalDevice device : devices) {
        if (isDeviceSuitable(device)) {
            physicalDevice_ = device;
            queueFamilies_ = findQueueFamilies(device);
            return;
        }
    }

    throw std::runtime_error("No GPU supports the required Vulkan 1.4 feature set");
}

void VulkanRenderer::createDevice()
{
    std::set<uint32_t> uniqueQueueFamilies { queueFamilies_.graphics, queueFamilies_.present };
    std::vector<VkDeviceQueueCreateInfo> queueInfos;
    const float queuePriority = 1.0f;

    for (uint32_t queueFamily : uniqueQueueFamilies) {
        queueInfos.push_back({
            .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .queueFamilyIndex = queueFamily,
            .queueCount = 1,
            .pQueuePriorities = &queuePriority,
        });
    }

    VkPhysicalDeviceGraphicsPipelineLibraryFeaturesEXT graphicsPipelineLibrary {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_GRAPHICS_PIPELINE_LIBRARY_FEATURES_EXT,
        .graphicsPipelineLibrary = VK_TRUE,
    };

    VkPhysicalDeviceExtendedDynamicStateFeaturesEXT extendedDynamicState {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT,
        .pNext = &graphicsPipelineLibrary,
        .extendedDynamicState = VK_TRUE,
    };

    VkPhysicalDeviceVulkan13Features vulkan13 {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
        .pNext = &extendedDynamicState,
        .synchronization2 = VK_TRUE,
        .dynamicRendering = VK_TRUE,
    };

    VkDeviceCreateInfo createInfo {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = &vulkan13,
        .queueCreateInfoCount = static_cast<uint32_t>(queueInfos.size()),
        .pQueueCreateInfos = queueInfos.data(),
        .enabledExtensionCount = static_cast<uint32_t>(requiredDeviceExtensions.size()),
        .ppEnabledExtensionNames = requiredDeviceExtensions.data(),
    };

    vkCheck(vkCreateDevice(physicalDevice_, &createInfo, nullptr, &device_), "vkCreateDevice failed");

    vkGetDeviceQueue(device_, queueFamilies_.graphics, 0, &graphicsQueue_);
    vkGetDeviceQueue(device_, queueFamilies_.present, 0, &presentQueue_);
}

void VulkanRenderer::createSwapchain()
{
    VkSurfaceCapabilitiesKHR capabilities {};
    vkCheck(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice_, surface_, &capabilities), "vkGetPhysicalDeviceSurfaceCapabilitiesKHR failed");

    uint32_t formatCount = 0;
    vkCheck(vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice_, surface_, &formatCount, nullptr), "vkGetPhysicalDeviceSurfaceFormatsKHR failed");
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    vkCheck(vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice_, surface_, &formatCount, formats.data()), "vkGetPhysicalDeviceSurfaceFormatsKHR failed");

    uint32_t presentModeCount = 0;
    vkCheck(vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice_, surface_, &presentModeCount, nullptr), "vkGetPhysicalDeviceSurfacePresentModesKHR failed");
    std::vector<VkPresentModeKHR> presentModes(presentModeCount);
    vkCheck(vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice_, surface_, &presentModeCount, presentModes.data()), "vkGetPhysicalDeviceSurfacePresentModesKHR failed");

    const VkSurfaceFormatKHR surfaceFormat = chooseSurfaceFormat(formats);
    const VkPresentModeKHR presentMode = choosePresentMode(presentModes);
    const VkExtent2D extent = chooseSwapchainExtent(capabilities);

    uint32_t imageCount = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount > 0 && imageCount > capabilities.maxImageCount) {
        imageCount = capabilities.maxImageCount;
    }

    std::array queueFamilyIndices { queueFamilies_.graphics, queueFamilies_.present };
    const bool sharedQueues = queueFamilies_.graphics != queueFamilies_.present;

    VkSwapchainCreateInfoKHR createInfo {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = surface_,
        .minImageCount = imageCount,
        .imageFormat = surfaceFormat.format,
        .imageColorSpace = surfaceFormat.colorSpace,
        .imageExtent = extent,
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .imageSharingMode = sharedQueues ? VK_SHARING_MODE_CONCURRENT : VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = sharedQueues ? static_cast<uint32_t>(queueFamilyIndices.size()) : 0U,
        .pQueueFamilyIndices = sharedQueues ? queueFamilyIndices.data() : nullptr,
        .preTransform = capabilities.currentTransform,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = presentMode,
        .clipped = VK_TRUE,
        .oldSwapchain = swapchain_,
    };

    VkSwapchainKHR oldSwapchain = swapchain_;
    vkCheck(vkCreateSwapchainKHR(device_, &createInfo, nullptr, &swapchain_), "vkCreateSwapchainKHR failed");

    if (oldSwapchain) {
        vkDestroySwapchainKHR(device_, oldSwapchain, nullptr);
    }

    swapchainFormat_ = surfaceFormat.format;
    swapchainExtent_ = extent;

    uint32_t swapchainImageCount = 0;
    vkCheck(vkGetSwapchainImagesKHR(device_, swapchain_, &swapchainImageCount, nullptr), "vkGetSwapchainImagesKHR failed");
    std::vector<VkImage> images(swapchainImageCount);
    vkCheck(vkGetSwapchainImagesKHR(device_, swapchain_, &swapchainImageCount, images.data()), "vkGetSwapchainImagesKHR failed");

    swapchainImages_.resize(images.size());
    for (size_t i = 0; i < images.size(); ++i) {
        swapchainImages_[i].image = images[i];
    }
}

void VulkanRenderer::createImageViews()
{
    for (auto& image : swapchainImages_) {
        VkImageViewCreateInfo createInfo {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = image.image,
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = swapchainFormat_,
            .components = {
                .r = VK_COMPONENT_SWIZZLE_IDENTITY,
                .g = VK_COMPONENT_SWIZZLE_IDENTITY,
                .b = VK_COMPONENT_SWIZZLE_IDENTITY,
                .a = VK_COMPONENT_SWIZZLE_IDENTITY,
            },
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
        };

        vkCheck(vkCreateImageView(device_, &createInfo, nullptr, &image.view), "vkCreateImageView failed");
    }
}

void VulkanRenderer::createCommandPool()
{
    VkCommandPoolCreateInfo createInfo {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = queueFamilies_.graphics,
    };

    vkCheck(vkCreateCommandPool(device_, &createInfo, nullptr, &commandPool_), "vkCreateCommandPool failed");
}

void VulkanRenderer::createPipeline()
{
    VkPushConstantRange pushConstantRange {
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
        .offset = 0,
        .size = sizeof(Vec2),
    };

    VkPipelineLayoutCreateInfo layoutInfo {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &pushConstantRange,
    };
    vkCheck(vkCreatePipelineLayout(device_, &layoutInfo, nullptr, &pipelineLayout_), "vkCreatePipelineLayout failed");

    VkShaderModule vertexShader = createShaderModule(assetRoot_ / "shaders/triangle.vert.glsl.spv");
    VkShaderModule fragmentShader = createShaderModule(assetRoot_ / "shaders/triangle.frag.glsl.spv");

    pipelineLibrary_ = createGraphicsPipelineLibrary(vertexShader, fragmentShader);

    vkDestroyShaderModule(device_, fragmentShader, nullptr);
    vkDestroyShaderModule(device_, vertexShader, nullptr);

    VkPipeline libraries[] { pipelineLibrary_ };
    VkPipelineLibraryCreateInfoKHR libraryInfo {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LIBRARY_CREATE_INFO_KHR,
        .libraryCount = 1,
        .pLibraries = libraries,
    };

    VkGraphicsPipelineCreateInfo linkedPipeline {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext = &libraryInfo,
        .flags = VK_PIPELINE_CREATE_LINK_TIME_OPTIMIZATION_BIT_EXT,
        .layout = pipelineLayout_,
    };

    vkCheck(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &linkedPipeline, nullptr, &pipeline_), "vkCreateGraphicsPipelines linked pipeline failed");
}

void VulkanRenderer::createFrameResources()
{
    std::array<VkCommandBuffer, maxFramesInFlight_> commandBuffers {};
    VkCommandBufferAllocateInfo allocateInfo {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = commandPool_,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = static_cast<uint32_t>(commandBuffers.size()),
    };
    vkCheck(vkAllocateCommandBuffers(device_, &allocateInfo, commandBuffers.data()), "vkAllocateCommandBuffers failed");

    VkSemaphoreCreateInfo semaphoreInfo {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
    };
    VkFenceCreateInfo fenceInfo {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT,
    };

    for (size_t i = 0; i < frames_.size(); ++i) {
        frames_[i].commandBuffer = commandBuffers[i];
        vkCheck(vkCreateSemaphore(device_, &semaphoreInfo, nullptr, &frames_[i].imageAvailable), "vkCreateSemaphore failed");
        vkCheck(vkCreateSemaphore(device_, &semaphoreInfo, nullptr, &frames_[i].renderFinished), "vkCreateSemaphore failed");
        vkCheck(vkCreateFence(device_, &fenceInfo, nullptr, &frames_[i].inFlight), "vkCreateFence failed");
    }
}

void VulkanRenderer::recreateSwapchain()
{
    int width = 0;
    int height = 0;
    SDL_GetWindowSizeInPixels(window_, &width, &height);
    if (width == 0 || height == 0) {
        return;
    }

    vkDeviceWaitIdle(device_);
    cleanupSwapchain();
    createSwapchain();
    createImageViews();
}

void VulkanRenderer::cleanupSwapchain()
{
    for (auto& image : swapchainImages_) {
        if (image.view) {
            vkDestroyImageView(device_, image.view, nullptr);
            image.view = VK_NULL_HANDLE;
        }
    }
    swapchainImages_.clear();

    if (swapchain_) {
        vkDestroySwapchainKHR(device_, swapchain_, nullptr);
        swapchain_ = VK_NULL_HANDLE;
    }
}

void VulkanRenderer::recordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex, const RenderFrameData& frameData)
{
    VkCommandBufferBeginInfo beginInfo {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
    };
    vkCheck(vkBeginCommandBuffer(commandBuffer, &beginInfo), "vkBeginCommandBuffer failed");

    VkImageMemoryBarrier2 toColorAttachment {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_NONE,
        .srcAccessMask = VK_ACCESS_2_NONE,
        .dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        .dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = swapchainImages_[imageIndex].image,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
    };
    VkDependencyInfo toColorDependency {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &toColorAttachment,
    };
    vkCmdPipelineBarrier2(commandBuffer, &toColorDependency);

    VkClearValue clearValue {
        .color = { { 0.03f, 0.04f, 0.06f, 1.0f } },
    };

    VkRenderingAttachmentInfo colorAttachment {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = swapchainImages_[imageIndex].view,
        .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue = clearValue,
    };

    VkRenderingInfo renderingInfo {
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea = { .offset = { 0, 0 }, .extent = swapchainExtent_ },
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &colorAttachment,
    };

    vkCmdBeginRendering(commandBuffer, &renderingInfo);

    VkViewport viewport {
        .x = 0.0f,
        .y = 0.0f,
        .width = static_cast<float>(swapchainExtent_.width),
        .height = static_cast<float>(swapchainExtent_.height),
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };
    VkRect2D scissor {
        .offset = { 0, 0 },
        .extent = swapchainExtent_,
    };

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
    vkCmdSetCullMode(commandBuffer, VK_CULL_MODE_NONE);
    vkCmdSetFrontFace(commandBuffer, VK_FRONT_FACE_COUNTER_CLOCKWISE);
    vkCmdSetPrimitiveTopology(commandBuffer, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    vkCmdPushConstants(commandBuffer, pipelineLayout_, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(Vec2), &frameData.triangleOffset);
    vkCmdDraw(commandBuffer, 3, 1, 0, 0);

    vkCmdEndRendering(commandBuffer);

    VkImageMemoryBarrier2 toPresent {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        .srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_NONE,
        .dstAccessMask = VK_ACCESS_2_NONE,
        .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = swapchainImages_[imageIndex].image,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
    };
    VkDependencyInfo toPresentDependency {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &toPresent,
    };
    vkCmdPipelineBarrier2(commandBuffer, &toPresentDependency);

    vkCheck(vkEndCommandBuffer(commandBuffer), "vkEndCommandBuffer failed");
}

VulkanRenderer::QueueFamilyIndices VulkanRenderer::findQueueFamilies(VkPhysicalDevice device) const
{
    QueueFamilyIndices indices;

    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);
    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

    for (uint32_t i = 0; i < queueFamilyCount; ++i) {
        if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            indices.graphics = i;
        }

        VkBool32 presentSupport = VK_FALSE;
        vkCheck(vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface_, &presentSupport), "vkGetPhysicalDeviceSurfaceSupportKHR failed");
        if (presentSupport) {
            indices.present = i;
        }

        if (indices.complete()) {
            break;
        }
    }

    return indices;
}

bool VulkanRenderer::isDeviceSuitable(VkPhysicalDevice device) const
{
    VkPhysicalDeviceProperties properties {};
    vkGetPhysicalDeviceProperties(device, &properties);
    if (VK_API_VERSION_MAJOR(properties.apiVersion) < 1 ||
        (VK_API_VERSION_MAJOR(properties.apiVersion) == 1 && VK_API_VERSION_MINOR(properties.apiVersion) < 4)) {
        return false;
    }

    const QueueFamilyIndices indices = findQueueFamilies(device);
    if (!indices.complete()) {
        return false;
    }

    uint32_t extensionCount = 0;
    vkCheck(vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr), "vkEnumerateDeviceExtensionProperties failed");
    std::vector<VkExtensionProperties> extensions(extensionCount);
    vkCheck(vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, extensions.data()), "vkEnumerateDeviceExtensionProperties failed");

    std::set<std::string> missing(requiredDeviceExtensions.begin(), requiredDeviceExtensions.end());
    for (const auto& extension : extensions) {
        missing.erase(extension.extensionName);
    }
    if (!missing.empty()) {
        return false;
    }

    uint32_t formatCount = 0;
    uint32_t presentModeCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface_, &formatCount, nullptr);
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface_, &presentModeCount, nullptr);
    if (formatCount == 0 || presentModeCount == 0) {
        return false;
    }

    VkPhysicalDeviceGraphicsPipelineLibraryFeaturesEXT graphicsPipelineLibrary {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_GRAPHICS_PIPELINE_LIBRARY_FEATURES_EXT,
    };
    VkPhysicalDeviceExtendedDynamicStateFeaturesEXT extendedDynamicState {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT,
        .pNext = &graphicsPipelineLibrary,
    };
    VkPhysicalDeviceVulkan13Features vulkan13 {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
        .pNext = &extendedDynamicState,
    };
    VkPhysicalDeviceFeatures2 features {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .pNext = &vulkan13,
    };

    vkGetPhysicalDeviceFeatures2(device, &features);

    return vulkan13.dynamicRendering &&
        vulkan13.synchronization2 &&
        extendedDynamicState.extendedDynamicState &&
        graphicsPipelineLibrary.graphicsPipelineLibrary;
}

VkSurfaceFormatKHR VulkanRenderer::chooseSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& formats) const
{
    const auto preferred = std::ranges::find_if(formats, [](const VkSurfaceFormatKHR& format) {
        return format.format == VK_FORMAT_B8G8R8A8_SRGB &&
            format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    });

    return preferred != formats.end() ? *preferred : formats.front();
}

VkPresentModeKHR VulkanRenderer::choosePresentMode(const std::vector<VkPresentModeKHR>& modes) const
{
    return std::ranges::find(modes, VK_PRESENT_MODE_MAILBOX_KHR) != modes.end()
        ? VK_PRESENT_MODE_MAILBOX_KHR
        : VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D VulkanRenderer::chooseSwapchainExtent(const VkSurfaceCapabilitiesKHR& capabilities) const
{
    if (capabilities.currentExtent.width != UINT32_MAX) {
        return capabilities.currentExtent;
    }

    int width = 0;
    int height = 0;
    SDL_GetWindowSizeInPixels(window_, &width, &height);

    VkExtent2D extent {
        .width = static_cast<uint32_t>(std::max(width, 1)),
        .height = static_cast<uint32_t>(std::max(height, 1)),
    };

    extent.width = std::clamp(extent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
    extent.height = std::clamp(extent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
    return extent;
}

VkShaderModule VulkanRenderer::createShaderModule(const std::filesystem::path& path) const
{
    const std::vector<char> code = readFile(path);

    VkShaderModuleCreateInfo createInfo {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = code.size(),
        .pCode = reinterpret_cast<const uint32_t*>(code.data()),
    };

    VkShaderModule shaderModule = VK_NULL_HANDLE;
    vkCheck(vkCreateShaderModule(device_, &createInfo, nullptr, &shaderModule), "vkCreateShaderModule failed");
    return shaderModule;
}

VkPipeline VulkanRenderer::createGraphicsPipelineLibrary(VkShaderModule vertexShader, VkShaderModule fragmentShader) const
{
    VkPipelineShaderStageCreateInfo shaderStages[] {
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .module = vertexShader,
            .pName = "main",
        },
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = fragmentShader,
            .pName = "main",
        },
    };

    VkPipelineVertexInputStateCreateInfo vertexInput {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
    };

    VkPipelineInputAssemblyStateCreateInfo inputAssembly {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    };

    VkPipelineViewportStateCreateInfo viewportState {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .scissorCount = 1,
    };

    VkPipelineRasterizationStateCreateInfo rasterizer {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_NONE,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .lineWidth = 1.0f,
    };

    VkPipelineMultisampleStateCreateInfo multisampling {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    };

    VkPipelineColorBlendAttachmentState colorBlendAttachment {
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
            VK_COLOR_COMPONENT_G_BIT |
            VK_COLOR_COMPONENT_B_BIT |
            VK_COLOR_COMPONENT_A_BIT,
    };

    VkPipelineColorBlendStateCreateInfo colorBlending {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &colorBlendAttachment,
    };

    VkDynamicState dynamicStates[] {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
        VK_DYNAMIC_STATE_CULL_MODE,
        VK_DYNAMIC_STATE_FRONT_FACE,
        VK_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY,
    };

    VkPipelineDynamicStateCreateInfo dynamicState {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = static_cast<uint32_t>(std::size(dynamicStates)),
        .pDynamicStates = dynamicStates,
    };

    VkPipelineRenderingCreateInfo rendering {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .colorAttachmentCount = 1,
        .pColorAttachmentFormats = &swapchainFormat_,
    };

    VkGraphicsPipelineLibraryCreateInfoEXT libraryInfo {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_LIBRARY_CREATE_INFO_EXT,
        .pNext = &rendering,
        .flags = VK_GRAPHICS_PIPELINE_LIBRARY_VERTEX_INPUT_INTERFACE_BIT_EXT |
            VK_GRAPHICS_PIPELINE_LIBRARY_PRE_RASTERIZATION_SHADERS_BIT_EXT |
            VK_GRAPHICS_PIPELINE_LIBRARY_FRAGMENT_SHADER_BIT_EXT |
            VK_GRAPHICS_PIPELINE_LIBRARY_FRAGMENT_OUTPUT_INTERFACE_BIT_EXT,
    };

    VkGraphicsPipelineCreateInfo pipelineInfo {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext = &libraryInfo,
        .flags = VK_PIPELINE_CREATE_LIBRARY_BIT_KHR |
            VK_PIPELINE_CREATE_RETAIN_LINK_TIME_OPTIMIZATION_INFO_BIT_EXT,
        .stageCount = static_cast<uint32_t>(std::size(shaderStages)),
        .pStages = shaderStages,
        .pVertexInputState = &vertexInput,
        .pInputAssemblyState = &inputAssembly,
        .pViewportState = &viewportState,
        .pRasterizationState = &rasterizer,
        .pMultisampleState = &multisampling,
        .pColorBlendState = &colorBlending,
        .pDynamicState = &dynamicState,
        .layout = pipelineLayout_,
    };

    VkPipeline pipeline = VK_NULL_HANDLE;
    vkCheck(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline), "vkCreateGraphicsPipelines library failed");
    return pipeline;
}

} // namespace sokoban

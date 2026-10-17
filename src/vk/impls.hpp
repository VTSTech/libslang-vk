#pragma once

// libslang-vk internal — shared pimpl definitions.
//
// The runner's Impl structs live here (not in the .cpp that owns the public
// methods) because runner translation units cross-reference each other:
// Image/Chain need Device::Impl's allocator + submit machinery, Chain reads
// Preset::Impl's compiled artifacts. Included only by src/vk/*.cpp.

#include <functional>

#include "vk_internal.hpp"

namespace slv {

// Core-layer vocabulary + internal Vulkan helpers, unqualified in these TUs.
using slv::preset::ParamInfo;
using slv::preset::PassDef;
using slv::preset::PassSpirv;
using slv::preset::SlangTranslation;
using vk::ValidationLog;

struct Device::Impl {
        bool adopted = false;
        VkInstance instance = VK_NULL_HANDLE;
        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
        VkDevice device = VK_NULL_HANDLE;
        VkQueue graphicsQueue = VK_NULL_HANDLE;
        uint32_t graphicsFamily = 0;
        VkPhysicalDeviceProperties properties{};
        VmaAllocator allocator = VK_NULL_HANDLE;
        VkCommandPool commandPool = VK_NULL_HANDLE;
        VkDebugUtilsMessengerEXT messenger = VK_NULL_HANDLE;
        ValidationLog validation;
        VmaVulkanFunctions vulkanFunctions{}; // volk-bound fetchers (see adopt/create).

        ~Impl()
        {
                // Destroy in reverse dependency order: pool's command buffers
                // must die before the device (VUID-vkDestroyDevice-device-05137).
                if (messenger != VK_NULL_HANDLE && vkDestroyDebugUtilsMessengerEXT != nullptr)
                        vkDestroyDebugUtilsMessengerEXT(instance, messenger, nullptr);
                if (commandPool != VK_NULL_HANDLE)
                        vkDestroyCommandPool(device, commandPool, nullptr);
                if (allocator != VK_NULL_HANDLE)
                        vmaDestroyAllocator(allocator);
                if (device != VK_NULL_HANDLE && !adopted)
                        vkDestroyDevice(device, nullptr);
                if (instance != VK_NULL_HANDLE && !adopted)
                        vkDestroyInstance(instance, nullptr);
        }

        // One-shot command recording: allocates from the pool, records, submits
        // with a fence and waits. M1 is deliberately synchronous (plan §6).
        auto SubmitAndWait(const std::function<void(VkCommandBuffer)> &record) -> std::expected<void, Error>
        {
                VkCommandBufferAllocateInfo allocInfo{};
                allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
                allocInfo.commandPool = commandPool;
                allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
                allocInfo.commandBufferCount = 1;
                VkCommandBuffer cmd = VK_NULL_HANDLE;
                SLV_VK(vkAllocateCommandBuffers(device, &allocInfo, &cmd), Errc::vk_error);

                VkCommandBufferBeginInfo beginInfo{};
                beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
                beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
                SLV_VK(vkBeginCommandBuffer(cmd, &beginInfo), Errc::vk_error);
                record(cmd);
                SLV_VK(vkEndCommandBuffer(cmd), Errc::vk_error);

                VkFenceCreateInfo fenceInfo{};
                fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
                VkFence fence = VK_NULL_HANDLE;
                SLV_VK(vkCreateFence(device, &fenceInfo, nullptr, &fence), Errc::vk_error);

                VkSubmitInfo submitInfo{};
                submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
                submitInfo.commandBufferCount = 1;
                submitInfo.pCommandBuffers = &cmd;
                const VkResult submitted = vkQueueSubmit(graphicsQueue, 1, &submitInfo, fence);
                if (submitted != VK_SUCCESS) {
                        vkDestroyFence(device, fence, nullptr);
                        return std::unexpected(SLV_VKERR(submitted, Errc::vk_error, "vkQueueSubmit"));
                }
                const VkResult waited = vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX);
                vkDestroyFence(device, fence, nullptr);
                SLV_VK(waited, Errc::vk_error);
                SLV_VK(vkResetCommandPool(device, commandPool, 0), Errc::vk_error);
                return {};
        }
};

struct Preset::Impl {
        std::vector<PassDef> passDefs;
        std::vector<SlangTranslation> translations;
        std::vector<PassSpirv> spirv;
        std::vector<ParameterInfo> parameters;
        slv::preset::Preset parsed; // Core data model (textures, feedback, histories).
};

} // namespace slv

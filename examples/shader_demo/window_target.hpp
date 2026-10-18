#pragma once

// libslang-vk shader_demo — windowed target (GLFW window + swapchain).
//
// Compiled only when SLANGVK_DEMO_WINDOW is defined (CMake option
// SLANGVK_DEMO_WINDOW, default ON). This is the M4 presentation contract
// played from the host side: the demo owns the window, the VkSurfaceKHR,
// the swapchain and the acquire/present pair; Chain::render() waits the
// acquire semaphore at submit and hands the image back in PRESENT_SRC_KHR
// ready for vkQueuePresentKHR — and because render() is synchronous
// (fenced), the present needs no wait semaphore.
//
// GLFW is the only extra dependency (FetchContent). Every Vulkan entry point
// the demo needs beyond the library is resolved through
// glfwGetInstanceProcAddress, so the executable keeps no link-time dependency
// on the Vulkan loader — the same runtime-loading policy the library itself
// follows through volk. Surface creation goes through
// glfwCreateWindowSurface, so no VK_USE_PLATFORM_* setup is needed here.

#ifdef SLANGVK_DEMO_WINDOW

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#endif

// GLFW_INCLUDE_VULKAN pulls vulkan/vulkan.h in; GLFW_INCLUDE_NONE suppresses
// the OpenGL header chain (the Vulkan define alone does not).
#define GLFW_INCLUDE_VULKAN
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <cstdint>
#include <string>
#include <vector>

#include <vulkan/vulkan.h>

#include <slangvk/runner.hpp>

namespace demo {

class WindowTarget {
public:
        WindowTarget() = default;
        ~WindowTarget() { Destroy(); }
        WindowTarget(const WindowTarget &) = delete;
        auto operator=(const WindowTarget &) -> WindowTarget & = delete;

        static auto InitLibrary() -> bool { return glfwInit() == GLFW_TRUE; }
        static void ShutdownLibrary() { glfwTerminate(); }

        /**
         * @brief Opens the window, creates the surface, checks presentation
         * support on the device's graphics queue family and builds the first
         * swapchain.
         *
         * On failure the target is left empty (Destroys its partial state)
         * and err explains what happened — the caller can fall back to the
         * headless path.
         */
        auto Create(const slv::Device &device, uint32_t width, uint32_t height, const char *title, std::string &err)
            -> bool
        {
                if (!LoadFunctions(err))
                        return false;

                instance_ = device.instance();
                physicalDevice_ = device.physicalDevice();
                device_ = device.device();

                glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
                glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
                window_ = glfwCreateWindow(int(width), int(height), title, nullptr, nullptr);
                if (window_ == nullptr) {
                        err = "glfwCreateWindow failed (no display available?)";
                        return false;
                }
                glfwSetWindowUserPointer(window_, this);
                glfwSetFramebufferSizeCallback(window_,
                    [](GLFWwindow *w, int, int) {
                            static_cast<WindowTarget *>(glfwGetWindowUserPointer(w))->recreatePending_ = true;
                    });

                if (glfwCreateWindowSurface(instance_, window_, nullptr, &surface_) != VK_SUCCESS) {
                        err = "glfwCreateWindowSurface failed (driver rejects the surface?)";
                        CleanupWindow();
                        return false;
                }

                VkBool32 supported = VK_FALSE;
                vkGetPhysicalDeviceSurfaceSupportKHR_(
                    device.physicalDevice(), device.graphicsFamily(), surface_, &supported);
                if (supported != VK_TRUE) {
                        err = "graphics queue family does not support presentation on this surface";
                        CleanupAll();
                        return false;
                }

                if (!CreateSwapchain(err)) {
                        CleanupAll();
                        return false;
                }
                return true;
        }

        void Destroy()
        {
                DestroySwapchainObjects();
                CleanupWindow();
        }

        auto Valid() const -> bool { return window_ != nullptr && swapchain_ != VK_NULL_HANDLE; }
        auto Window() const -> GLFWwindow * { return window_; }
        auto Format() const -> VkFormat { return format_; }
        auto Extent() const -> VkExtent2D { return extent_; }
        auto Images() const -> const std::vector<VkImage> & { return images_; }
        /** @brief True when swapchain images allow TRANSFER_SRC (screenshot readback). */
        auto TransferSrcSwapchain() const -> bool { return transferSrc_; }

        auto ShouldClose() const -> bool { return glfwWindowShouldClose(window_) == GLFW_TRUE; }
        void PollEvents() { glfwPollEvents(); }

        /** @brief The acquire semaphore the host signals (render() waits it). */
        auto AcquireSemaphore() const -> VkSemaphore { return acquireSem_; }

        /** @brief Build + wait the M4 SwapchainFrame output for image `index`. */
        auto Frame(uint32_t index) const -> slv::SwapchainFrame
        {
                slv::SwapchainFrame frame;
                frame.swapchainImageView = views_[index];
                frame.format = format_;
                frame.width = extent_.width;
                frame.height = extent_.height;
                frame.outputRect = {}; // zero rect = fit + center letterbox
                frame.acquireSemaphore = acquireSem_;
                return frame;
        }

        /** @brief True once when GLFW (or a SUBOPTIMAL present) flagged a resize. */
        auto PollResize() -> bool
        {
                const bool pending = recreatePending_;
                recreatePending_ = false;
                return pending;
        }

        /** @brief Flags a swapchain recreation for the next PollResize() (called
         * when vkQueuePresentKHR reports OUT_OF_DATE/SUBOPTIMAL). */
        void FlagRecreate() { recreatePending_ = true; }

        /**
         * @brief Destroys and rebuilds the swapchain for the current
         * framebuffer size (idempotent). Call after PollResize() or when
         * acquire reports VK_ERROR_OUT_OF_DATE_KHR.
         */
        auto RecreateSwapchain(std::string &err) -> bool
        {
                if (vkDeviceWaitIdle_ != nullptr && device_ != VK_NULL_HANDLE)
                        vkDeviceWaitIdle_(device_);
                DestroySwapchainObjects();
                return CreateSwapchain(err);
        }

        auto Acquire(uint32_t *index) -> VkResult
        {
                return vkAcquireNextImageKHR_(device_, swapchain_, UINT64_MAX, acquireSem_, VK_NULL_HANDLE, index);
        }

        auto Present(VkQueue queue, uint32_t index) -> VkResult
        {
                VkPresentInfoKHR present{};
                present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
                present.swapchainCount = 1;
                present.pSwapchains = &swapchain_;
                present.pImageIndices = &index;
                return vkQueuePresentKHR_(queue, &present);
        }

private:
        auto LoadFunctions(std::string &err) -> bool
        {
#define SLV_DEMO_LOAD_FN(name)                                                                                         \
        name##_ = reinterpret_cast<PFN_##name>(glfwGetInstanceProcAddress(VK_NULL_HANDLE, #name));                     \
        if (name##_ == nullptr) {                                                                                      \
                err = "glfwGetInstanceProcAddress returned null for " #name;                                           \
                return false;                                                                                          \
        }
                // Surface/presentation support (instance level).
                SLV_DEMO_LOAD_FN(vkGetPhysicalDeviceSurfaceSupportKHR)
                SLV_DEMO_LOAD_FN(vkGetPhysicalDeviceSurfaceCapabilitiesKHR)
                SLV_DEMO_LOAD_FN(vkGetPhysicalDeviceSurfaceFormatsKHR)
                SLV_DEMO_LOAD_FN(vkGetPhysicalDeviceSurfacePresentModesKHR)
                SLV_DEMO_LOAD_FN(vkDestroySurfaceKHR)
                // Swapchain (device level, promoted VK_KHR_swapchain).
                SLV_DEMO_LOAD_FN(vkCreateSwapchainKHR)
                SLV_DEMO_LOAD_FN(vkDestroySwapchainKHR)
                SLV_DEMO_LOAD_FN(vkGetSwapchainImagesKHR)
                SLV_DEMO_LOAD_FN(vkAcquireNextImageKHR)
                SLV_DEMO_LOAD_FN(vkQueuePresentKHR)
                // The few core device functions the demo calls directly.
                SLV_DEMO_LOAD_FN(vkCreateImageView)
                SLV_DEMO_LOAD_FN(vkDestroyImageView)
                SLV_DEMO_LOAD_FN(vkCreateSemaphore)
                SLV_DEMO_LOAD_FN(vkDestroySemaphore)
                SLV_DEMO_LOAD_FN(vkDeviceWaitIdle)
#undef SLV_DEMO_LOAD_FN
                return true;
        }

        auto CreateSwapchain(std::string &err) -> bool
        {
                VkSurfaceCapabilitiesKHR caps{};
                vkGetPhysicalDeviceSurfaceCapabilitiesKHR_(physicalDevice_, surface_, &caps);
                // Wait for un-minimize: RecreateSwapchain retries next frame.
                if (caps.currentExtent.width == 0 || caps.currentExtent.height == 0) {
                        err = "window minimized";
                        return false;
                }

                uint32_t formatCount = 0;
                vkGetPhysicalDeviceSurfaceFormatsKHR_(physicalDevice_, surface_, &formatCount, nullptr);
                std::vector<VkSurfaceFormatKHR> formats(formatCount);
                vkGetPhysicalDeviceSurfaceFormatsKHR_(physicalDevice_, surface_, &formatCount, formats.data());
                VkSurfaceFormatKHR chosen = formats[0];
                for (const VkSurfaceFormatKHR &f : formats) {
                        if (f.format == VK_FORMAT_B8G8R8A8_UNORM
                            && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
                                chosen = f;
                                break;
                        }
                        if (f.format == VK_FORMAT_R8G8B8A8_UNORM
                            && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
                                chosen = f;
                                break;
                        }
                }

                VkExtent2D extent = caps.currentExtent;
                if (extent.width == UINT32_MAX) { // framebuffer-driven surface
                        int fw = 0, fh = 0;
                        glfwGetFramebufferSize(window_, &fw, &fh);
                        extent.width = uint32_t(fw);
                        extent.height = uint32_t(fh);
                }
                extent.width = (std::min)(std::max(extent.width, caps.minImageExtent.width), caps.maxImageExtent.width);
                extent.height =
                    (std::min)(std::max(extent.height, caps.minImageExtent.height), caps.maxImageExtent.height);

                uint32_t imageCount = caps.minImageCount + 1;
                if (caps.maxImageCount > 0 && imageCount > caps.maxImageCount)
                        imageCount = caps.maxImageCount;

                VkImageUsageFlags usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
                transferSrc_ = (caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) != 0;
                if (transferSrc_)
                        usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT; // screenshot readback

                VkSwapchainCreateInfoKHR ci{};
                ci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
                ci.surface = surface_;
                ci.minImageCount = imageCount;
                ci.imageFormat = chosen.format;
                ci.imageColorSpace = chosen.colorSpace;
                ci.imageExtent = extent;
                ci.imageArrayLayers = 1;
                ci.imageUsage = usage;
                ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
                ci.preTransform = caps.currentTransform;
                ci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
                ci.presentMode = VK_PRESENT_MODE_FIFO_KHR; // guaranteed everywhere
                ci.clipped = VK_TRUE;
                ci.oldSwapchain = VK_NULL_HANDLE;
                if (vkCreateSwapchainKHR_(device_, &ci, nullptr, &swapchain_) != VK_SUCCESS) {
                        err = "vkCreateSwapchainKHR failed";
                        return false;
                }

                uint32_t count = 0;
                vkGetSwapchainImagesKHR_(device_, swapchain_, &count, nullptr);
                images_.resize(count);
                vkGetSwapchainImagesKHR_(device_, swapchain_, &count, images_.data());

                views_.resize(count);
                for (uint32_t i = 0; i < count; ++i) {
                        VkImageViewCreateInfo vi{};
                        vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
                        vi.image = images_[i];
                        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
                        vi.format = chosen.format;
                        vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
                        if (vkCreateImageView_(device_, &vi, nullptr, &views_[i]) != VK_SUCCESS) {
                                err = "vkCreateImageView failed for swapchain image " + std::to_string(i);
                                DestroySwapchainObjects();
                                return false;
                        }
                }

                if (acquireSem_ == VK_NULL_HANDLE) {
                        VkSemaphoreCreateInfo si{};
                        si.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
                        if (vkCreateSemaphore_(device_, &si, nullptr, &acquireSem_) != VK_SUCCESS) {
                                err = "vkCreateSemaphore failed";
                                DestroySwapchainObjects();
                                return false;
                        }
                }

                format_ = chosen.format;
                extent_ = extent;
                return true;
        }

        void DestroySwapchainObjects()
        {
                if (device_ == VK_NULL_HANDLE)
                        return;
                for (VkImageView view : views_)
                        if (view != VK_NULL_HANDLE)
                                vkDestroyImageView_(device_, view, nullptr);
                views_.clear();
                images_.clear();
                if (swapchain_ != VK_NULL_HANDLE) {
                        vkDestroySwapchainKHR_(device_, swapchain_, nullptr);
                        swapchain_ = VK_NULL_HANDLE;
                }
        }

        void CleanupWindow()
        {
                if (window_ != nullptr) {
                        glfwDestroyWindow(window_);
                        window_ = nullptr;
                }
        }

        void CleanupAll()
        {
                DestroySwapchainObjects();
                if (surface_ != VK_NULL_HANDLE && vkDestroySurfaceKHR_ != nullptr) {
                        vkDestroySurfaceKHR_(instance_, surface_, nullptr);
                        surface_ = VK_NULL_HANDLE;
                }
                CleanupWindow();
        }

        GLFWwindow *window_ = nullptr;
        VkInstance instance_ = VK_NULL_HANDLE;
        VkDevice device_ = VK_NULL_HANDLE;
        VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
        VkSurfaceKHR surface_ = VK_NULL_HANDLE;
        VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
        VkSemaphore acquireSem_ = VK_NULL_HANDLE;
        VkFormat format_ = VK_FORMAT_UNDEFINED;
        VkExtent2D extent_{0, 0};
        std::vector<VkImage> images_;
        std::vector<VkImageView> views_;
        bool transferSrc_ = false;
        bool recreatePending_ = false;

        // Function pointers resolved through glfwGetInstanceProcAddress (see
        // file comment): surface support + swapchain machinery + the few core
        // device functions the demo drives itself.
        PFN_vkGetPhysicalDeviceSurfaceSupportKHR vkGetPhysicalDeviceSurfaceSupportKHR_ = nullptr;
        PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR vkGetPhysicalDeviceSurfaceCapabilitiesKHR_ = nullptr;
        PFN_vkGetPhysicalDeviceSurfaceFormatsKHR vkGetPhysicalDeviceSurfaceFormatsKHR_ = nullptr;
        PFN_vkGetPhysicalDeviceSurfacePresentModesKHR vkGetPhysicalDeviceSurfacePresentModesKHR_ = nullptr;
        PFN_vkDestroySurfaceKHR vkDestroySurfaceKHR_ = nullptr;
        PFN_vkCreateSwapchainKHR vkCreateSwapchainKHR_ = nullptr;
        PFN_vkDestroySwapchainKHR vkDestroySwapchainKHR_ = nullptr;
        PFN_vkGetSwapchainImagesKHR vkGetSwapchainImagesKHR_ = nullptr;
        PFN_vkAcquireNextImageKHR vkAcquireNextImageKHR_ = nullptr;
        PFN_vkQueuePresentKHR vkQueuePresentKHR_ = nullptr;
        PFN_vkCreateImageView vkCreateImageView_ = nullptr;
        PFN_vkDestroyImageView vkDestroyImageView_ = nullptr;
        PFN_vkCreateSemaphore vkCreateSemaphore_ = nullptr;
        PFN_vkDestroySemaphore vkDestroySemaphore_ = nullptr;
        PFN_vkDeviceWaitIdle vkDeviceWaitIdle_ = nullptr;
};

} // namespace demo

#endif // SLANGVK_DEMO_WINDOW

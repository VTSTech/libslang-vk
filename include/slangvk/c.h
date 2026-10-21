// slangvk_c.h — minimal extern "C" integration header for libslang-vk
// Written by VTSTech (Nigel Todman) (veritas@vts-tech.org)
// AI Assisted by Super-Z (GLM-5.3-Flash)
// License: MIT
//
// A drop-in C binding over the C++23 public API (include/slangvk/runner.hpp).
// Lets C and C++ games (SDL2/SDL3, Vulkan-native, emulator hosts) run libretro
// .slangp shader presets through Vulkan without touching C++: opaque handles,
// POD structs, status codes, no std:: types at the boundary.
//
// Dependencies at the boundary: <vulkan/vulkan.h> for handle types only.
// No Vulkan SDK at build or link time — libslang-vk loads the Vulkan runtime
// through volk internally. No slang SDK — presets are compiled at runtime
// through the glslang built into the library.
//
// ---------------------------------------------------------------------------------
// QUICKSTART (the whole integration):
//
//   // 1) Build: the host links the slangvk target (C++23) and compiles
//   //    slangvk_c.cpp (the thin shim implementing this header) alongside it.
//   //    CMake:
//   //      include(FetchContent)
//   //      FetchContent_Declare(libslang-vk GIT_REPOSITORY
//   //          https://github.com/VTSTech/libslang-vk.git GIT_TAG main)
//   //      FetchContent_MakeAvailable(libslang-vk)
//   //      target_link_libraries(mygame PRIVATE slangvk)
//
//   // 2) Device — either let libslang-vk create one (headless-friendly):
//   slangvk_device_options dev_opt = {0};
//   dev_opt.app_name = "mygame";
//   dev_opt.instance_extensions = SDL_Vulkan_GetInstanceExtensions-style list;
//   dev_opt.device_extensions = &VK_KHR_SWAPCHAIN_EXTENSION_NAME; count = 1;
//   slangvk_device *dev = NULL;
//   if (slangvk_device_create(&dev_opt, &dev) != SLANGVK_OK) { ... }
//   //    ... or ADOPT your existing Vulkan context (Vulkan-native hosts):
//   //    slangvk_device_adopt(instance, physical_device, device, queue, family, &dev);
//
//   // 3) Swapchain: libslang-vk does NOT own your window. Create a surface +
//   //    swapchain the normal way (SDL_Vulkan_CreateSurface + vkCreateSwapchainKHR).
//   //    Use slangvk_device_get_handles() if libslang-vk created the device.
//
//   // 4) Input image + preset + chain (once per preset/window size):
//   slangvk_image img = NULL;
//   slangvk_image_options img_opt = {0};
//   img_opt.width = GAME_W; img_opt.height = GAME_H;      // e.g. 320x240
//   img_opt.format = VK_FORMAT_R8G8B8A8_UNORM;
//   slangvk_image_create(dev, &img_opt, &img);
//   slangvk_preset *preset = NULL;
//   slangvk_preset_load(dev, "shaders/crt/royale.slangp", NULL, &preset);
//   slangvk_chain *chain = NULL;
//   slangvk_chain_create(dev, preset, SWAP_W, SWAP_H, swap_format, &chain);
//
//   // 5) Frame loop (all calls synchronous; host owns acquire/present):
//   slangvk_image_update(img, my_cpu_framebuffer, GAME_W * GAME_H * 4);
//   vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, acquire_sem, 0, &idx);
//   slangvk_input_frame in = {0};
//   slangvk_image_get_handles(img, &in.view, &in.image, &in.format, &in.width, &in.height);
//   slangvk_swapchain_frame out = {0};
//   out.swapchain_image_view = swap_views[idx]; out.format = swap_format;
//   out.width = SWAP_W; out.height = SWAP_H;  // out.output_rect all-zero = fit + center
//   out.acquire_semaphore = acquire_sem;
//   slangvk_frame_info fi = { frame_count++, delta_us, +1 };
//   slangvk_chain_render_swapchain(chain, &in, &out, &fi);
//   vkQueuePresentKHR(...);   // render() is synchronous/fenced: no wait semaphore needed
//
//   // 6) On swapchain extent change: slangvk_chain_resize(chain, new_w, new_h);
//   //    On preset change: destroy the chain, load the new preset, create a chain.
//
// CONTRACTS (same as the C++ API):
//   * The input image (host-created or slangvk_image) must rest in
//     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL before render(); it is never
//     modified and returns in the same layout.
//   * slangvk_image_update: pixelBytes must equal width*height*bytesPerTexel,
//     row-major, tightly packed (slangvk_image is 1-mip).
//   * A slangvk_preset must outlive every chain created from it.
//   * A chain is single-threaded; render() records + submits one command
//     buffer and waits (fenced).
//   * After a swapchain extent change, call slangvk_chain_resize() before the
//     next render; render() rejects a drifted extent.
//   * Destroy order: chains -> presets -> images -> device.
// ---------------------------------------------------------------------------------

#ifndef SLANGVK_C_H
#define SLANGVK_C_H

#include <stddef.h>
#include <stdint.h>

#include <vulkan/vulkan.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Status codes (mirror slv::Errc 1:1) ---------------------------------- */

typedef enum slangvk_result {
        SLANGVK_OK = 0,
        SLANGVK_ERR_PRESET_PARSE = 1,        /**< .slangp/.slang syntax or reference problem */
        SLANGVK_ERR_INCLUDE_CYCLE = 2,       /**< #include / #reference cycle detected */
        SLANGVK_ERR_SHADER_COMPILE = 3,      /**< glslang front-end rejected a stage */
        SLANGVK_ERR_UNSUPPORTED_FEATURE = 4, /**< valid construct not yet implemented */
        SLANGVK_ERR_INVALID_ARGUMENT = 5,    /**< mismatched sizes/formats/layouts */
        SLANGVK_ERR_VK_INIT_FAILED = 6,      /**< loader/instance/device creation failed */
        SLANGVK_ERR_VK_DEVICE_LOST = 7,      /**< VK_ERROR_DEVICE_LOST during execution */
        SLANGVK_ERR_DEVICE_LOST_RECOVERY_FAILED = 8, /**< device-lost recovery attempt failed */
        SLANGVK_ERR_VK_OUT_OF_MEMORY = 9,    /**< host or device allocation failed */
        SLANGVK_ERR_VK_ERROR = 10            /**< any other Vulkan VkResult failure */
} slangvk_result;

/** @brief Short name of a result code ("OK", "shader_compile", ...) for logs. */
const char *slangvk_result_name(slangvk_result result);

/**
 * @brief Human-readable message for the LAST failed call on this thread.
 * The pointer stays valid until the next slangvk call on the same thread.
 * Never NULL (empty string when the last call succeeded).
 */
const char *slangvk_last_error(void);

/* ---- Device ---------------------------------------------------------------- */

typedef struct slangvk_device slangvk_device;
typedef struct slangvk_image slangvk_image;
typedef struct slangvk_preset slangvk_preset;
typedef struct slangvk_chain slangvk_chain;

/** @brief Input rotation relative to the output (RetroArch uint 0..3). */
enum {
        SLANGVK_ROTATION_NONE = 0,
        SLANGVK_ROTATION_CW90 = 1,
        SLANGVK_ROTATION_CW180 = 2,
        SLANGVK_ROTATION_CW270 = 3
};

typedef struct slangvk_device_options {
        const char *app_name;                       /**< NULL = "". */
        uint32_t min_api_version;                   /**< 0 = VK_API_VERSION_1_1. */
        uint32_t validation_layers;                 /**< 0/1: VK_LAYER_KHRONOS_validation. */
        const char *const *instance_extensions;     /**< Verbatim (surface exts go here). */
        uint32_t instance_extension_count;
        const char *const *device_extensions;       /**< Verbatim (VK_KHR_swapchain). */
        uint32_t device_extension_count;
} slangvk_device_options;

/** @brief Physical device identity, for host logs/UI. */
typedef struct slangvk_device_info {
        char device_name[VK_MAX_PHYSICAL_DEVICE_NAME_SIZE];
        uint32_t api_version;
        uint32_t vendor_id;
        uint32_t device_id;
        uint32_t driver_version;
} slangvk_device_info;

/**
 * @brief Creates instance + device internally (headless-friendly mode).
 * @param options May be NULL (defaults: appName "", API 1.1, no validation,
 *                no extensions). Instance extensions for swapchain use (the
 *                platform surface extension + VK_KHR_swapchain) must be
 *                requested by the host.
 * @param out_device Set on SLANGVK_OK; destroy with slangvk_device_destroy().
 */
slangvk_result slangvk_device_create(const slangvk_device_options *options, slangvk_device **out_device);

/**
 * @brief Adopts a host-created Vulkan context (DevilutionX / vkQuake mode).
 * The host keeps owning instance/device/queue; libslang-vk creates its own
 * VMA allocator + command machinery on top. The host context must stay alive
 * for the lifetime of every object created through this device.
 */
slangvk_result slangvk_device_adopt(VkInstance instance, VkPhysicalDevice physical_device, VkDevice device,
    VkQueue graphics_queue, uint32_t graphics_family, slangvk_device **out_device);

/** @brief Destroys the device. All images/presets/chains must be destroyed first. */
void slangvk_device_destroy(slangvk_device *device);

/** @brief Fills @p out_info from the adopted/selected physical device. */
slangvk_result slangvk_device_get_info(const slangvk_device *device, slangvk_device_info *out_info);

/** @brief Raw handles of an internally created device (NULL fields when adopted). */
slangvk_result slangvk_device_get_handles(const slangvk_device *device, VkInstance *out_instance,
    VkPhysicalDevice *out_physical_device, VkDevice *out_device, VkQueue *out_queue, uint32_t *out_graphics_family);

/** @brief Blocks until all work submitted through this device finishes. */
slangvk_result slangvk_device_wait_idle(const slangvk_device *device);

/**
 * @brief Rebuilds the Vulkan device after a device-lost event.
 *
 * Recreates the VkDevice, its command pool and the library's memory
 * allocator from the same instance and physical device. Only devices
 * created with slangvk_device_create can be recovered this way; an adopted
 * device stays the host's to recreate and is reported as
 * SLANGVK_UNSUPPORTED.
 *
 * Every object the lost device owned is gone after this call: images,
 * presets and chains created from it must be recreated (presets and chains
 * rebuild themselves on the next use) before they are used again.
 */
slangvk_result slangvk_device_recover(slangvk_device *device);

/**
 * @brief Reads an image back to host memory (row-major, tightly packed).
 * Headless helper for tests / golden images / CI (same contract as the C++
 * Device::readPixels). Transition-safe: the image is moved to TRANSFER_SRC
 * and restored to @p current_layout afterwards.
 * @param out_pixels Caller-allocated buffer of at least
 *                   width*height*bytesPerTexel bytes.
 */
slangvk_result slangvk_device_read_pixels(slangvk_device *device, VkImage image, VkFormat format,
    uint32_t width, uint32_t height, VkImageLayout current_layout, void *out_pixels, size_t out_pixels_bytes);

/* ---- Image (convenience host-side image + view + staging) ------------------- */

typedef struct slangvk_image_options {
        uint32_t width;
        uint32_t height;
        VkFormat format;          /**< 0 = VK_FORMAT_R8G8B8A8_UNORM. */
        VkImageUsageFlags usage;  /**< 0 = sampled | color_attachment | transfer_src | transfer_dst. */
        const void *pixels;       /**< Optional initial content, tightly packed. */
        size_t pixel_bytes;       /**< Must equal width*height*bytesPerTexel when pixels != NULL. */
        VkImageLayout final_layout; /**< 0 = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL. */
} slangvk_image_options;

slangvk_result slangvk_image_create(slangvk_device *device, const slangvk_image_options *options, slangvk_image **out_image);

/**
 * @brief Per-frame host upload: replaces the image content (synchronous
 * staging copy). @p pixel_bytes must equal width*height*bytesPerTexel,
 * row-major, tightly packed. The image rests in its creation layout
 * before and after.
 */
slangvk_result slangvk_image_update(slangvk_image *image, const void *pixels, size_t pixel_bytes);

void slangvk_image_destroy(slangvk_image *image);

/** @brief Borrowed handles: pass these into slangvk_input_frame each frame. */
slangvk_result slangvk_image_get_handles(const slangvk_image *image, VkImageView *out_view, VkImage *out_image,
    VkFormat *out_format, uint32_t *out_width, uint32_t *out_height);

/* ---- Preset ----------------------------------------------------------------- */

typedef struct slangvk_preset_options {
        uint32_t strict;      /**< Promote translator warnings to load errors. */
        uint32_t no_lut_images; /**< Skip LUT texture upload (compile-only sweeps). */
} slangvk_preset_options;

/** @brief Metadata for one shader parameter (merged preset + pragma view). */
typedef struct slangvk_parameter {
        char name[128];
        char description[256];
        float default_value;
        float minimum;
        float maximum;
        float step;
} slangvk_parameter;

/**
 * @brief Parses, translates and SPIR-V-compiles a .slangp synchronously.
 * Immutable after load; shareable across chains; must outlive them.
 * @param path UTF-8 path to the .slangp.
 * @param options May be NULL (defaults: not strict, LUTs uploaded).
 */
slangvk_result slangvk_preset_load(slangvk_device *device, const char *path,
    const slangvk_preset_options *options, slangvk_preset **out_preset);

void slangvk_preset_destroy(slangvk_preset *preset);

slangvk_result slangvk_preset_pass_count(const slangvk_preset *preset, uint32_t *out_count);

/** @brief Number of shader parameters (pragma defaults merged with preset overrides). */
slangvk_result slangvk_preset_parameter_count(const slangvk_preset *preset, uint32_t *out_count);

/** @brief Fetches parameter @p index into @p out_parameter (0 <= index < count). */
slangvk_result slangvk_preset_parameter(const slangvk_preset *preset, uint32_t index, slangvk_parameter *out_parameter);

/* ---- Chain ------------------------------------------------------------------ */

/** @brief Pixel rectangle; x/y may be negative when host-supplied. */
typedef struct slangvk_rect {
        int32_t x;
        int32_t y;
        uint32_t width;
        uint32_t height;
} slangvk_rect;

/** @brief The chain's input texture (host-owned; must be SHADER_READ_ONLY_OPTIMAL). */
typedef struct slangvk_input_frame {
        VkImageView view; /**< Required. */
        VkImage image;    /**< Required for history/mipmap-input presets (blitted, never modified). May be NULL otherwise. */
        VkFormat format;
        uint32_t width;
        uint32_t height;
        uint32_t rotation; /**< SLANGVK_ROTATION_*. */
} slangvk_input_frame;

/** @brief Per-frame inputs (values written into the params UBO). */
typedef struct slangvk_frame_info {
        uint64_t frame_count;
        float frame_delta_us; /**< Microseconds since the previous frame (FrameTime). */
        int32_t frame_direction; /**< 1 forward, -1 rewind. */
} slangvk_frame_info;

/**
 * @brief Render into an already-acquired swapchain image view. Host owns
 * acquire/present: call vkAcquireNextImageKHR (optionally signaling
 * acquire_semaphore), render, then vkQueuePresentKHR after render returns.
 * render() is synchronous (fenced), waits the acquire semaphore at submit
 * and leaves the image in VK_IMAGE_LAYOUT_PRESENT_SRC_KHR — the present
 * needs no wait semaphore.
 */
typedef struct slangvk_swapchain_frame {
        VkImageView swapchain_image_view; /**< The acquired image's view. */
        VkFormat format;                  /**< The swapchain's current format. */
        uint32_t width;
        uint32_t height;
        slangvk_rect output_rect;         /**< Content rect; all-zero = fit + center (letterbox). */
        VkSemaphore acquire_semaphore;    /**< Signaled by the host's acquire; optional (VK_NULL_HANDLE). */
} slangvk_swapchain_frame;

/**
 * @brief Render into a host-provided view the host composites itself.
 * If the final pass resolves to exactly this size and rotation is None it
 * draws straight in (byte-exact); otherwise fit-and-center + letterbox.
 */
typedef struct slangvk_offscreen {
        VkImageView view;   /**< Required. */
        VkImage image;      /**< Required only when the final pass is feedback-read. May be NULL otherwise. */
        VkFormat format;
        uint32_t width;
        uint32_t height;
} slangvk_offscreen;

/**
 * @brief Builds pipelines/FBOs/samplers for one preset on one device.
 * @param output_width/output_height Final output target size (offscreen/swapchain rect).
 * @param output_format 0 = VK_FORMAT_R8G8B8A8_UNORM.
 */
slangvk_result slangvk_chain_create(slangvk_device *device, const slangvk_preset *preset,
    uint32_t output_width, uint32_t output_height, VkFormat output_format, slangvk_chain **out_chain);

void slangvk_chain_destroy(slangvk_chain *chain);

/**
 * @brief Stores a new output target size (picked up by the next render).
 * Pass FBOs re-derive lazily; call after every swapchain extent change,
 * before the next render.
 */
slangvk_result slangvk_chain_resize(slangvk_chain *chain, uint32_t output_width, uint32_t output_height);

/** @brief Overrides a parameter value (merged into the params UBO on the next render). */
slangvk_result slangvk_chain_set_parameter(slangvk_chain *chain, const char *id, float value);

/** @brief Renders one frame synchronously into an acquired swapchain image. */
slangvk_result slangvk_chain_render_swapchain(slangvk_chain *chain, const slangvk_input_frame *input,
    const slangvk_swapchain_frame *target, const slangvk_frame_info *frame_info);

/** @brief Renders one frame synchronously into a host-composited view. */
slangvk_result slangvk_chain_render_offscreen(slangvk_chain *chain, const slangvk_input_frame *input,
    const slangvk_offscreen *target, const slangvk_frame_info *frame_info);

/** @brief Sizes of a pass's output FBO as currently allocated. */
slangvk_result slangvk_chain_pass_output_size(const slangvk_chain *chain, uint32_t pass_index,
    uint32_t *out_width, uint32_t *out_height);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* SLANGVK_C_H */

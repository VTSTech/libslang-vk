// slangvk_c.cpp — extern "C" shim implementing slangvk_c.h over the
// libslang-vk C++23 public API (include/slangvk/runner.hpp).
// Written by VTSTech (Nigel Todman) (veritas@vts-tech.org)
// AI Assisted by Super-Z (GLM-5.3-Flash)
// License: MIT
//
// In-repo placement: include/slangvk/c.h (the header) + this file compiled
// into the slangvk target(s). When kept standalone beside slangvk_c.h,
// adjust the two includes below.
//
// Design notes:
//   * Every slv:: object lives in a heap-allocated aggregate wrapper
//     (struct slangvk_device { slv::Device device; }; ...) whose C++ move
//     constructor initializes the member; handles are plain pointers.
//   * std::expected<T, slv::Error> unwraps into the out-parameter; the
//     Error::message lands in a thread-local buffer served by
//     slangvk_last_error() (empty after any successful call).
//   * The library loads the Vulkan runtime through its own volk internally;
//     the host's loader state (volkInitialize / direct loader calls) is
//     independent. The shim never calls Vulkan.
//   * No allocation policy surprises: the shim only news handles the host
//     destroys through slangvk_*_destroy, and the C++ members own everything
//     else (VMA allocator, staging buffers, pipelines).

#include <slangvk/c.h>

#include <slangvk/runner.hpp>

#include <algorithm>
#include <cstring>
#include <expected>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// --------------------------------------------------------------------------------------
// Handle wrappers (opaque on the C side)
// --------------------------------------------------------------------------------------

struct slangvk_device {
        slv::Device device;
};
struct slangvk_image {
        slv::Image image;
};
struct slangvk_preset {
        slv::Preset preset;
};
struct slangvk_chain {
        slv::Chain chain;
};

namespace {

thread_local std::string tLastError;

slangvk_result MapErrc(slv::Errc code)
{
        switch (code) {
        case slv::Errc::preset_parse:
                return SLANGVK_ERR_PRESET_PARSE;
        case slv::Errc::include_cycle:
                return SLANGVK_ERR_INCLUDE_CYCLE;
        case slv::Errc::shader_compile:
                return SLANGVK_ERR_SHADER_COMPILE;
        case slv::Errc::unsupported_feature:
                return SLANGVK_ERR_UNSUPPORTED_FEATURE;
        case slv::Errc::invalid_argument:
                return SLANGVK_ERR_INVALID_ARGUMENT;
        case slv::Errc::vk_init_failed:
                return SLANGVK_ERR_VK_INIT_FAILED;
        case slv::Errc::vk_device_lost:
                return SLANGVK_ERR_VK_DEVICE_LOST;
        case slv::Errc::vk_out_of_memory:
                return SLANGVK_ERR_VK_OUT_OF_MEMORY;
        case slv::Errc::vk_error:
                return SLANGVK_ERR_VK_ERROR;
        }
        return SLANGVK_ERR_VK_ERROR;
}

slangvk_result Fail(const slv::Error &error)
{
        tLastError = error.message;
        return MapErrc(error.code);
}

slangvk_result SetInvalid(const char *message)
{
        tLastError = message ? message : "invalid argument";
        return SLANGVK_ERR_INVALID_ARGUMENT;
}

/** @brief Unwraps an expected value into a heap handle; clears the error buffer on success. */
template <typename Cxx, typename Handle>
slangvk_result AdoptOut(std::expected<Cxx, slv::Error> &&result, Handle **out)
{
        if (out == nullptr)
                return SetInvalid("output handle pointer is NULL");
        if (!result)
                return Fail(result.error());
        *out = new Handle{std::move(*result)};
        tLastError.clear();
        return SLANGVK_OK;
}

/** @brief Propagates an expected<void> status; clears the error buffer on success. */
slangvk_result Check(std::expected<void, slv::Error> &&result)
{
        if (!result)
                return Fail(result.error());
        tLastError.clear();
        return SLANGVK_OK;
}

void CopyStr(char *dst, size_t dstSize, const char *src)
{
        if (dst == nullptr || dstSize == 0)
                return;
        const size_t n = src != nullptr ? std::min(dstSize - 1, std::strlen(src)) : 0;
        std::memcpy(dst, src != nullptr ? src : "", n);
        dst[n] = '\0';
}

slv::FrameInfo FrameInfoFrom(const slangvk_frame_info *frameInfo)
{
        slv::FrameInfo out;
        if (frameInfo != nullptr) {
                out.frameCount = frameInfo->frame_count;
                out.frameDeltaUs = frameInfo->frame_delta_us;
                out.frameDirection = frameInfo->frame_direction;
        } else {
                out.frameDirection = 1;
        }
        return out;
}

slv::InputFrame InputFrom(const slangvk_input_frame *input)
{
        slv::InputFrame out;
        out.view = input->view;
        out.image = input->image;
        out.format = input->format;
        out.width = input->width;
        out.height = input->height;
        out.rotation = static_cast<slv::Rotation>(input->rotation);
        return out;
}

} // namespace

// --------------------------------------------------------------------------------------
// Status / error access
// --------------------------------------------------------------------------------------

const char *slangvk_result_name(slangvk_result result)
{
        switch (result) {
        case SLANGVK_OK:
                return "OK";
        case SLANGVK_ERR_PRESET_PARSE:
                return "preset_parse";
        case SLANGVK_ERR_INCLUDE_CYCLE:
                return "include_cycle";
        case SLANGVK_ERR_SHADER_COMPILE:
                return "shader_compile";
        case SLANGVK_ERR_UNSUPPORTED_FEATURE:
                return "unsupported_feature";
        case SLANGVK_ERR_INVALID_ARGUMENT:
                return "invalid_argument";
        case SLANGVK_ERR_VK_INIT_FAILED:
                return "vk_init_failed";
        case SLANGVK_ERR_VK_DEVICE_LOST:
                return "vk_device_lost";
        case SLANGVK_ERR_VK_OUT_OF_MEMORY:
                return "vk_out_of_memory";
        case SLANGVK_ERR_VK_ERROR:
                return "vk_error";
        }
        return "unknown";
}

const char *slangvk_last_error(void)
{
        return tLastError.c_str();
}

// --------------------------------------------------------------------------------------
// Device
// --------------------------------------------------------------------------------------

slangvk_result slangvk_device_create(const slangvk_device_options *options, slangvk_device **out_device)
{
        if (out_device == nullptr)
                return SetInvalid("out_device is NULL");
        if (options != nullptr && options->instance_extension_count != 0 && options->instance_extensions == nullptr)
                return SetInvalid("instance_extensions is NULL but instance_extension_count > 0");
        if (options != nullptr && options->device_extension_count != 0 && options->device_extensions == nullptr)
                return SetInvalid("device_extensions is NULL but device_extension_count > 0");

        slv::DeviceOptions opts;
        if (options != nullptr) {
                opts.appName = options->app_name != nullptr ? std::string_view(options->app_name) : std::string_view();
                if (options->min_api_version != 0)
                        opts.minApiVersion = options->min_api_version;
                opts.validationLayers = options->validation_layers != 0;
                opts.instanceExtensions.assign(options->instance_extensions, options->instance_extensions + options->instance_extension_count);
                opts.deviceExtensions.assign(options->device_extensions, options->device_extensions + options->device_extension_count);
        }
        return AdoptOut(slv::Device::create(opts), out_device);
}

slangvk_result slangvk_device_adopt(VkInstance instance, VkPhysicalDevice physical_device, VkDevice device,
    VkQueue graphics_queue, uint32_t graphics_family, slangvk_device **out_device)
{
        if (out_device == nullptr)
                return SetInvalid("out_device is NULL");
        if (instance == VK_NULL_HANDLE || physical_device == VK_NULL_HANDLE || device == VK_NULL_HANDLE || graphics_queue == VK_NULL_HANDLE)
                return SetInvalid("instance, physical_device, device and graphics_queue must be non-NULL");
        return AdoptOut(slv::Device::adopt(instance, physical_device, device, graphics_queue, graphics_family), out_device);
}

void slangvk_device_destroy(slangvk_device *device)
{
        delete device;
}

slangvk_result slangvk_device_get_info(const slangvk_device *device, slangvk_device_info *out_info)
{
        if (device == nullptr || out_info == nullptr)
                return SetInvalid("device and out_info must be non-NULL");
        std::memset(out_info, 0, sizeof(*out_info));
        const VkPhysicalDeviceProperties &props = device->device.properties();
        CopyStr(out_info->device_name, sizeof(out_info->device_name), props.deviceName);
        out_info->api_version = props.apiVersion;
        out_info->vendor_id = props.vendorID;
        out_info->device_id = props.deviceID;
        out_info->driver_version = props.driverVersion;
        tLastError.clear();
        return SLANGVK_OK;
}

slangvk_result slangvk_device_get_handles(const slangvk_device *device, VkInstance *out_instance,
    VkPhysicalDevice *out_physical_device, VkDevice *out_device, VkQueue *out_queue, uint32_t *out_graphics_family)
{
        if (device == nullptr)
                return SetInvalid("device is NULL");
        if (out_instance != nullptr)
                *out_instance = device->device.instance();
        if (out_physical_device != nullptr)
                *out_physical_device = device->device.physicalDevice();
        if (out_device != nullptr)
                *out_device = device->device.device();
        if (out_queue != nullptr)
                *out_queue = device->device.graphicsQueue();
        if (out_graphics_family != nullptr)
                *out_graphics_family = device->device.graphicsFamily();
        tLastError.clear();
        return SLANGVK_OK;
}

slangvk_result slangvk_device_wait_idle(const slangvk_device *device)
{
        if (device == nullptr)
                return SetInvalid("device is NULL");
        return Check(device->device.waitIdle());
}

slangvk_result slangvk_device_read_pixels(slangvk_device *device, VkImage image, VkFormat format,
    uint32_t width, uint32_t height, VkImageLayout current_layout, void *out_pixels, size_t out_pixels_bytes)
{
        if (device == nullptr || image == VK_NULL_HANDLE || out_pixels == nullptr)
                return SetInvalid("device, image and out_pixels must be non-NULL");
        auto pixels = device->device.readPixels(image, format, width, height, current_layout);
        if (!pixels)
                return Fail(pixels.error());
        if (out_pixels_bytes < pixels->size())
                return SetInvalid("out_pixels buffer is smaller than the packed image readback");
        std::memcpy(out_pixels, pixels->data(), pixels->size());
        tLastError.clear();
        return SLANGVK_OK;
}

// --------------------------------------------------------------------------------------
// Image
// --------------------------------------------------------------------------------------

slangvk_result slangvk_image_create(slangvk_device *device, const slangvk_image_options *options, slangvk_image **out_image)
{
        if (device == nullptr || options == nullptr || out_image == nullptr)
                return SetInvalid("device, options and out_image must be non-NULL");

        slv::Image::CreateInfo info;
        info.desc.width = options->width;
        info.desc.height = options->height;
        info.desc.format = options->format != VK_FORMAT_UNDEFINED ? options->format : VK_FORMAT_R8G8B8A8_UNORM;
        info.usage = options->usage != 0 ? options->usage
            : VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT
                | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        info.pixels = options->pixels;
        info.pixelBytes = options->pixel_bytes;
        info.finalLayout = options->final_layout != VK_IMAGE_LAYOUT_UNDEFINED ? options->final_layout
                                                                              : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        return AdoptOut(slv::Image::create(device->device, info), out_image);
}

slangvk_result slangvk_image_update(slangvk_image *image, const void *pixels, size_t pixel_bytes)
{
        if (image == nullptr || pixels == nullptr)
                return SetInvalid("image and pixels must be non-NULL");
        return Check(image->image.update(pixels, pixel_bytes));
}

void slangvk_image_destroy(slangvk_image *image)
{
        delete image;
}

slangvk_result slangvk_image_get_handles(const slangvk_image *image, VkImageView *out_view, VkImage *out_image,
    VkFormat *out_format, uint32_t *out_width, uint32_t *out_height)
{
        if (image == nullptr)
                return SetInvalid("image is NULL");
        if (out_view != nullptr)
                *out_view = image->image.view();
        if (out_image != nullptr)
                *out_image = image->image.image();
        if (out_format != nullptr)
                *out_format = image->image.desc().format;
        if (out_width != nullptr)
                *out_width = image->image.desc().width;
        if (out_height != nullptr)
                *out_height = image->image.desc().height;
        tLastError.clear();
        return SLANGVK_OK;
}

// --------------------------------------------------------------------------------------
// Preset
// --------------------------------------------------------------------------------------

slangvk_result slangvk_preset_load(slangvk_device *device, const char *path,
    const slangvk_preset_options *options, slangvk_preset **out_preset)
{
        if (device == nullptr || path == nullptr || out_preset == nullptr)
                return SetInvalid("device, path and out_preset must be non-NULL");
        slv::PresetLoadOptions opts;
        if (options != nullptr) {
                opts.strict = options->strict != 0;
                opts.noLutImages = options->no_lut_images != 0;
        }
        return AdoptOut(slv::Preset::load(device->device, std::filesystem::path(path), opts), out_preset);
}

void slangvk_preset_destroy(slangvk_preset *preset)
{
        delete preset;
}

slangvk_result slangvk_preset_pass_count(const slangvk_preset *preset, uint32_t *out_count)
{
        if (preset == nullptr || out_count == nullptr)
                return SetInvalid("preset and out_count must be non-NULL");
        *out_count = preset->preset.passCount();
        tLastError.clear();
        return SLANGVK_OK;
}

slangvk_result slangvk_preset_parameter_count(const slangvk_preset *preset, uint32_t *out_count)
{
        if (preset == nullptr || out_count == nullptr)
                return SetInvalid("preset and out_count must be non-NULL");
        *out_count = static_cast<uint32_t>(preset->preset.parameterInfo().size());
        tLastError.clear();
        return SLANGVK_OK;
}

slangvk_result slangvk_preset_parameter(const slangvk_preset *preset, uint32_t index, slangvk_parameter *out_parameter)
{
        if (preset == nullptr || out_parameter == nullptr)
                return SetInvalid("preset and out_parameter must be non-NULL");
        const std::span<const slv::ParameterInfo> params = preset->preset.parameterInfo();
        if (index >= params.size())
                return SetInvalid("parameter index out of range");
        std::memset(out_parameter, 0, sizeof(*out_parameter));
        const slv::ParameterInfo &info = params[index];
        CopyStr(out_parameter->name, sizeof(out_parameter->name), info.name.c_str());
        CopyStr(out_parameter->description, sizeof(out_parameter->description), info.description.c_str());
        out_parameter->default_value = info.defaultValue;
        out_parameter->minimum = info.minimum;
        out_parameter->maximum = info.maximum;
        out_parameter->step = info.step;
        tLastError.clear();
        return SLANGVK_OK;
}

// --------------------------------------------------------------------------------------
// Chain
// --------------------------------------------------------------------------------------

slangvk_result slangvk_chain_create(slangvk_device *device, const slangvk_preset *preset,
    uint32_t output_width, uint32_t output_height, VkFormat output_format, slangvk_chain **out_chain)
{
        if (device == nullptr || preset == nullptr || out_chain == nullptr)
                return SetInvalid("device, preset and out_chain must be non-NULL");
        slv::ChainOptions opts;
        opts.outputWidth = output_width;
        opts.outputHeight = output_height;
        opts.outputFormat = output_format != VK_FORMAT_UNDEFINED ? output_format : VK_FORMAT_R8G8B8A8_UNORM;
        return AdoptOut(slv::Chain::create(device->device, preset->preset, opts), out_chain);
}

void slangvk_chain_destroy(slangvk_chain *chain)
{
        delete chain;
}

slangvk_result slangvk_chain_resize(slangvk_chain *chain, uint32_t output_width, uint32_t output_height)
{
        if (chain == nullptr)
                return SetInvalid("chain is NULL");
        return Check(chain->chain.resize(output_width, output_height));
}

slangvk_result slangvk_chain_set_parameter(slangvk_chain *chain, const char *id, float value)
{
        if (chain == nullptr || id == nullptr)
                return SetInvalid("chain and id must be non-NULL");
        return Check(chain->chain.setParameter(std::string_view(id), value));
}

slangvk_result slangvk_chain_render_swapchain(slangvk_chain *chain, const slangvk_input_frame *input,
    const slangvk_swapchain_frame *target, const slangvk_frame_info *frame_info)
{
        if (chain == nullptr || input == nullptr || target == nullptr)
                return SetInvalid("chain, input and target must be non-NULL");
        if (input->rotation > SLANGVK_ROTATION_CW270)
                return SetInvalid("rotation must be a SLANGVK_ROTATION_* value");
        slv::SwapchainFrame frame;
        frame.swapchainImageView = target->swapchain_image_view;
        frame.format = target->format;
        frame.width = target->width;
        frame.height = target->height;
        frame.outputRect = slv::Rect { target->output_rect.x, target->output_rect.y,
                target->output_rect.width, target->output_rect.height };
        frame.acquireSemaphore = target->acquire_semaphore;
        return Check(chain->chain.render(InputFrom(input), frame, FrameInfoFrom(frame_info)));
}

slangvk_result slangvk_chain_render_offscreen(slangvk_chain *chain, const slangvk_input_frame *input,
    const slangvk_offscreen *target, const slangvk_frame_info *frame_info)
{
        if (chain == nullptr || input == nullptr || target == nullptr)
                return SetInvalid("chain, input and target must be non-NULL");
        if (input->rotation > SLANGVK_ROTATION_CW270)
                return SetInvalid("rotation must be a SLANGVK_ROTATION_* value");
        slv::Offscreen offscreen;
        offscreen.view = target->view;
        offscreen.image = target->image;
        offscreen.format = target->format;
        offscreen.width = target->width;
        offscreen.height = target->height;
        return Check(chain->chain.render(InputFrom(input), offscreen, FrameInfoFrom(frame_info)));
}

slangvk_result slangvk_chain_pass_output_size(const slangvk_chain *chain, uint32_t pass_index,
    uint32_t *out_width, uint32_t *out_height)
{
        if (chain == nullptr)
                return SetInvalid("chain is NULL");
        const auto size = chain->chain.passOutputSize(pass_index);
        if (!size)
                return Fail(size.error());
        if (out_width != nullptr)
                *out_width = size->first;
        if (out_height != nullptr)
                *out_height = size->second;
        tLastError.clear();
        return SLANGVK_OK;
}

// Written by VTSTech (Nigel Todman) (veritas@vts-tech.org)
// AI Assisted by Super-Z (GLM-5.3-Flash)
// License: MIT

// libslang-vk internal — VMA implementation translation unit (M1).
//
// volk is included first so VMA's dynamic function fetch binds to the
// runtime-loaded pointers (VMA_DYNAMIC_VULKAN_FUNCTIONS). Static Vulkan
// functions stay off: the library has no link-time Vulkan dependency.

#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#define VMA_IMPLEMENTATION
#include <volk.h>
#include <vk_mem_alloc.h>

#include "vulkan_device.h"
#include "core/logger.h"
#include "core/kmemory.h"
#include "core/kstring.h"
#include "containers/darray.h"

typedef struct vulkan_physical_device_requirements {
    b8 graphics;
    b8 present;
    b8 compute;
    b8 transfer;
    const char** device_extension_names;
    b8 sampler_anisotropy;
    b8 discrete_gpu;
} vulkan_physical_device_requirements;

typedef struct vulkan_physical_device_queue_family_info {
    u32 graphics_family_index;
    u32 present_family_index;
    u32 compute_family_index;
    u32 transfer_family_index;
} vulkan_physical_device_queue_family_info;

static b8 select_physical_device(vulkan_context* context);
static b8 physical_device_meets_requirements(
    VkPhysicalDevice device,
    VkSurfaceKHR surface,
    const VkPhysicalDeviceProperties* properties,
    const VkPhysicalDeviceFeatures* features,
    const vulkan_physical_device_requirements* requirements,
    vulkan_physical_device_queue_family_info* out_queue_family_info,
    vulkan_swapchain_support_info* out_swapchain_support);

b8 vulkan_device_create(vulkan_context* context) {
    if (!select_physical_device(context)) {
        return FALSE;
    }

    KINFO("Creating logical device...");
    b8 present_shares_graphics_queue = context->device.graphics_queue_index == context->device.present_queue_index;
    b8 transfer_shares_graphics_queue = context->device.graphics_queue_index == context->device.transfer_queue_index;
    b8 transfer_shares_present_queue = context->device.transfer_queue_index == context->device.present_queue_index;
    u32 index_count = 1;
    if (!present_shares_graphics_queue) {
        index_count++;
    }
    if (!transfer_shares_graphics_queue) {
        index_count++;
    }
    if (transfer_shares_present_queue) {
        index_count--;
    }
    u32 indices[index_count];
    u8 index = 0;
    indices[index++] = context->device.graphics_queue_index;
    if (!present_shares_graphics_queue) {
        indices[index++] = context->device.present_queue_index;
    }
    if (!transfer_shares_graphics_queue && !transfer_shares_present_queue) {
        indices[index++] = context->device.transfer_queue_index;
    }

    VkDeviceQueueCreateInfo queue_create_infos[index_count];
    for (u32 i = 0; i < index_count; ++i) {
        queue_create_infos[i].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queue_create_infos[i].queueFamilyIndex = indices[i];
        queue_create_infos[i].queueCount = 1;
        queue_create_infos[i].flags = 0;
        queue_create_infos[i].pNext = 0;
        f32 queue_priority = 1.0f;
        queue_create_infos[i].pQueuePriorities = &queue_priority;
    }

    // Request device features.
    VkPhysicalDeviceFeatures device_features = {};
    device_features.samplerAnisotropy = VK_TRUE;

    VkDeviceCreateInfo device_create_info = {VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    device_create_info.queueCreateInfoCount = index_count;
    device_create_info.pQueueCreateInfos = queue_create_infos;
    device_create_info.pEnabledFeatures = &device_features;
    device_create_info.enabledExtensionCount = 1;
    const char* extension_names = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
    device_create_info.ppEnabledExtensionNames = &extension_names;

    // Deprecated and ignored, so pass nothing.
    device_create_info.enabledLayerCount = 0;
    device_create_info.ppEnabledLayerNames = 0;

    // Create the device.
    VK_CHECK(vkCreateDevice(
        context->device.physical_device,
        &device_create_info,
        context->allocator,
        &context->device.logical_device));

    KINFO("Logical device created.");

    // Get queues.
    vkGetDeviceQueue(
        context->device.logical_device,
        context->device.graphics_queue_index,
        0,
        &context->device.graphics_queue);
    vkGetDeviceQueue(
        context->device.logical_device,
        context->device.present_queue_index,
        0,
        &context->device.present_queue);
    vkGetDeviceQueue(
        context->device.logical_device,
        context->device.transfer_queue_index,
        0,
        &context->device.transfer_queue);
    KINFO("Queues obtained.");

    // Create command pool for graphics queue.
    VkCommandPoolCreateInfo pool_create_info = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pool_create_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool_create_info.queueFamilyIndex = context->device.graphics_queue_index;
    VK_CHECK(vkCreateCommandPool(context->device.logical_device, &pool_create_info, context->allocator, &context->device.graphics_command_pool));
    KINFO("Graphics command pool created.");

    return TRUE;
}

void vulkan_device_destroy(vulkan_context* context) {
    if (context->device.graphics_command_pool) {
        vkDestroyCommandPool(context->device.logical_device, context->device.graphics_command_pool, context->allocator);
    }

    context->device.graphics_queue = 0;
    context->device.present_queue = 0;
    context->device.transfer_queue = 0;

    KINFO("Destroying logical device...");
    if (context->device.logical_device) {
        vkDestroyDevice(context->device.logical_device, context->allocator);
        context->device.logical_device = 0;
    }

    KINFO("Releasing physical device resources...");
    context->device.physical_device = 0;

    if (context->device.swapchain_support.formats) {
        kfree(
            context->device.swapchain_support.formats,
            sizeof(VkSurfaceFormatKHR) * context->device.swapchain_support.format_count,
            MEMORY_TAG_RENDERER);
        context->device.swapchain_support.formats = 0;
        context->device.swapchain_support.format_count = 0;
    }

    if (context->device.swapchain_support.present_modes) {
        kfree(
            context->device.swapchain_support.present_modes,
            sizeof(VkPresentModeKHR) * context->device.swapchain_support.present_mode_count,
            MEMORY_TAG_RENDERER);
        context->device.swapchain_support.present_modes = 0;
        context->device.swapchain_support.present_mode_count = 0;
    }

    kzero_memory(&context->device.swapchain_support.capabilities, sizeof(context->device.swapchain_support.capabilities));

    context->device.graphics_queue_index = -1;
    context->device.present_queue_index = -1;
    context->device.transfer_queue_index = -1;
}

void vulkan_device_query_swapchain_support(
    VkPhysicalDevice physical_device,
    VkSurfaceKHR surface,
    vulkan_swapchain_support_info* out_support_info) {
    // Surface capabilities.
    VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
        physical_device,
        surface,
        &out_support_info->capabilities));

    // Surface formats.
    VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(
        physical_device,
        surface,
        &out_support_info->format_count,
        0));
    if (out_support_info->format_count != 0) {
        if (!out_support_info->formats) {
            out_support_info->formats = kallocate(sizeof(VkSurfaceFormatKHR) * out_support_info->format_count, MEMORY_TAG_RENDERER);
        }
        VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(
            physical_device,
            surface,
            &out_support_info->format_count,
            out_support_info->formats));
    }

    // Present modes
    VK_CHECK(vkGetPhysicalDeviceSurfacePresentModesKHR(
        physical_device,
        surface,
        &out_support_info->present_mode_count,
        0));
    if (out_support_info->present_mode_count != 0) {
        if (!out_support_info->present_modes) {
            out_support_info->present_modes = kallocate(sizeof(VkPresentModeKHR) * out_support_info->present_mode_count, MEMORY_TAG_RENDERER);
        }
        VK_CHECK(vkGetPhysicalDeviceSurfacePresentModesKHR(
            physical_device,
            surface,
            &out_support_info->present_mode_count,
            out_support_info->present_modes));
    }
}

b8 vulkan_device_detect_depth_format(vulkan_device* device) {
    // Format candidates
    const u64 candidate_count = 3;
    VkFormat candidates[3] = {
        VK_FORMAT_D32_SFLOAT,
        VK_FORMAT_D32_SFLOAT_S8_UINT,
        VK_FORMAT_D24_UNORM_S8_UINT};
    
    u32 flags = VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT;
    for (u64 i = 0; i < candidate_count; ++i) {
        VkFormatProperties properties;
        vkGetPhysicalDeviceFormatProperties(device->physical_device, candidates[i], &properties);

        if ((properties.linearTilingFeatures & flags) == flags) {
            device->depth_format = candidates[i];
            return TRUE;
        } else if ((properties.optimalTilingFeatures & flags) == flags) {
            device->depth_format = candidates[i];
            return TRUE;
        }
    }

    return FALSE;
}

static b8 select_physical_device(vulkan_context* context) {
    u32 physical_device_count = 0;
    VK_CHECK(vkEnumeratePhysicalDevices(context->instance, &physical_device_count, 0));
    if (physical_device_count == 0) {
        KFATAL("No devices which support Vulkan were found.");
        return FALSE;
    }

    VkPhysicalDevice physical_devices[physical_device_count];
    VK_CHECK(vkEnumeratePhysicalDevices(context->instance, &physical_device_count, physical_devices));
    for (u32 i = 0; i < physical_device_count; ++i) {
        VkPhysicalDeviceProperties properties;
        vkGetPhysicalDeviceProperties(physical_devices[i], &properties);

        VkPhysicalDeviceFeatures features;
        vkGetPhysicalDeviceFeatures(physical_devices[i], &features);

        VkPhysicalDeviceMemoryProperties memory;
        vkGetPhysicalDeviceMemoryProperties(physical_devices[i], &memory);

        vulkan_physical_device_requirements requirements = {};
        requirements.graphics = TRUE;
        requirements.present = TRUE;
        requirements.transfer = TRUE;
        requirements.sampler_anisotropy = TRUE;
        requirements.discrete_gpu = FALSE;
        requirements.device_extension_names = darray_create(const char*);
        const char* swapchain_extension_name = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
        darray_push(requirements.device_extension_names, swapchain_extension_name);

        vulkan_physical_device_queue_family_info queue_info = {};
        b8 result = physical_device_meets_requirements(
            physical_devices[i],
            context->surface,
            &properties,
            &features,
            &requirements,
            &queue_info,
            &context->device.swapchain_support);
        if (result) {
            KINFO("Selected device: %s.", properties.deviceName);
            switch (properties.deviceType) {
                default:
                case VK_PHYSICAL_DEVICE_TYPE_OTHER:
                    KINFO("GPU type is Unknown.");
                    break;
                case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
                    KINFO("GPU type is integrated.");
                    break;
                case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
                    KINFO("GPU type is discreted.");
                    break;
                case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
                    KINFO("GPU type is virtual.");
                    break;
                case VK_PHYSICAL_DEVICE_TYPE_CPU:
                    KINFO("GPU type is CPU.");
                    break;
            }

            KINFO(
                "GPU Driver version: %d.%d.%d",
                VK_VERSION_MAJOR(properties.driverVersion),
                VK_VERSION_MINOR(properties.driverVersion),
                VK_VERSION_PATCH(properties.driverVersion));
            KINFO(
                "Vulkan API version: %d.%d.%d",
                VK_VERSION_MAJOR(properties.apiVersion),
                VK_VERSION_MINOR(properties.apiVersion),
                VK_VERSION_PATCH(properties.apiVersion));

            // Memory information
            for (u32 j = 0; j < memory.memoryHeapCount; ++j) {
                f32 memory_size_gib = (f32)(memory.memoryHeaps[j].size / 1024.0 / 1024.0 / 1024.0);
                if (memory.memoryHeaps[j].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) {
                    KINFO("Local GPU memory: %.2f GiB", memory_size_gib);
                } else {
                    KINFO("Shared system memory: %.2f GiB", memory_size_gib);
                }
            }

            context->device.physical_device = physical_devices[i];
            context->device.graphics_queue_index = queue_info.graphics_family_index;
            context->device.present_queue_index = queue_info.present_family_index;
            context->device.transfer_queue_index = queue_info.transfer_family_index;

            context->device.properties = properties;
            context->device.features = features;
            context->device.memory = memory;
            break;
        }
    }

    // Ensure a device is selected
    if (!context->device.physical_device) {
        KERROR("No physical device were found which meet the requirements.");
        return FALSE;
    }

    KINFO("Physical device selected.");
    return TRUE;
}

static b8 physical_device_meets_requirements(
    VkPhysicalDevice device,
    VkSurfaceKHR surface,
    const VkPhysicalDeviceProperties* properties,
    const VkPhysicalDeviceFeatures* features,
    const vulkan_physical_device_requirements* requirements,
    vulkan_physical_device_queue_family_info* out_queue_family_info,
    vulkan_swapchain_support_info* out_swapchain_support) {
    out_queue_family_info->graphics_family_index = -1;
    out_queue_family_info->present_family_index = -1;
    out_queue_family_info->compute_family_index = -1;
    out_queue_family_info->transfer_family_index = -1;

    if (requirements->discrete_gpu) {
        if (properties->deviceType != VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
            KINFO("Device is not a discrete GPU, and one is required. Skipping.");
            return FALSE;
        }
    }

    u32 queue_family_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_family_count, 0);
    VkQueueFamilyProperties queue_families[queue_family_count];
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_family_count, queue_families);

    KINFO("Graphics | Present | Compute | Transfer | Name");
    u8 min_transfer_score = 255;
    for (u32 i = 0; i < queue_family_count; ++i) {
        u8 current_transfer_score = 0;

        if (queue_families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            out_queue_family_info->graphics_family_index = i;
            ++current_transfer_score;
        }

        if (queue_families[i].queueFlags & VK_QUEUE_COMPUTE_BIT) {
            out_queue_family_info->compute_family_index = i;
            ++current_transfer_score;
        }

        if (queue_families[i].queueFlags & VK_QUEUE_TRANSFER_BIT) {
            if (current_transfer_score < min_transfer_score) {
                min_transfer_score = current_transfer_score;
                out_queue_family_info->transfer_family_index = i;
            }
        }

        VkBool32 support_present = VK_FALSE;
        VK_CHECK(vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &support_present));
        if (support_present) {
            out_queue_family_info->present_family_index = i;
        }
    }
    KINFO("       %d |       %d |       %d |        %d | %s",
          out_queue_family_info->graphics_family_index != -1,
          out_queue_family_info->present_family_index != -1,
          out_queue_family_info->compute_family_index != -1,
          out_queue_family_info->transfer_family_index != -1,
          properties->deviceName);

    if (
        (!requirements->graphics || (requirements->graphics && out_queue_family_info->graphics_family_index != -1)) &&
        (!requirements->compute || (requirements->compute && out_queue_family_info->compute_family_index != -1)) &&
        (!requirements->transfer || (requirements->transfer && out_queue_family_info->transfer_family_index != -1)) &&
        (!requirements->present || (requirements->present && out_queue_family_info->present_family_index != -1))) {
        KINFO("Device meets queue requirements.");
        KTRACE("Graphics Family Index: %i", out_queue_family_info->graphics_family_index);
        KTRACE("Present Family Index: %i", out_queue_family_info->present_family_index);
        KTRACE("Transfer Family Index: %i", out_queue_family_info->transfer_family_index);
        KTRACE("Compute Family Index: %i", out_queue_family_info->compute_family_index);

        vulkan_device_query_swapchain_support(device, surface, out_swapchain_support);
        if (out_swapchain_support->format_count < 1 || out_swapchain_support->present_mode_count < 1) {
            if (out_swapchain_support->formats) {
                kfree(out_swapchain_support->formats, sizeof(VkSurfaceFormatKHR) * out_swapchain_support->format_count, MEMORY_TAG_RENDERER);
            }
            if (out_swapchain_support->present_modes) {
                kfree(out_swapchain_support->present_modes, sizeof(VkPresentModeKHR) * out_swapchain_support->present_mode_count, MEMORY_TAG_RENDERER);
            }
            KINFO("Required swapchain support not present, skipping device");
            return FALSE;
        }

        // Device extensions.
        if (requirements->device_extension_names) {
            u32 available_extension_count = 0;
            VkExtensionProperties* available_extensions = 0;
            VK_CHECK(vkEnumerateDeviceExtensionProperties(device, 0, &available_extension_count, 0));
            if (available_extension_count != 0) {
                available_extensions = kallocate(sizeof(VkExtensionProperties) * available_extension_count, MEMORY_TAG_RENDERER);
                VK_CHECK(vkEnumerateDeviceExtensionProperties(device, 0, &available_extension_count, available_extensions));

                u32 required_extension_count = darray_length(requirements->device_extension_names);
                for (u32 i = 0; i < required_extension_count; ++i) {
                    b8 found = FALSE;
                    for (u32 j = 0; j < available_extension_count; ++j) {
                        if (strings_equal(requirements->device_extension_names[i], available_extensions[j].extensionName)) {
                            found = TRUE;
                            break;
                        }
                    }
                    if (!found) {
                        KINFO("Required extension not found: '%s', skipping device.", requirements->device_extension_names[i]);
                        kfree(available_extensions, sizeof(VkExtensionProperties) * available_extension_count, MEMORY_TAG_RENDERER);
                        return FALSE;
                    }
                }
                kfree(available_extensions, sizeof(VkExtensionProperties) * available_extension_count, MEMORY_TAG_RENDERER);
            }
        }

        // Sampler anisotropy
        if (requirements->sampler_anisotropy && !features->samplerAnisotropy) {
            KINFO("Device does not support samplerAnisotropy, skipping.");
            return FALSE;
        }

        // Device meets all requirements.
        return TRUE;
    }

    return FALSE;
}
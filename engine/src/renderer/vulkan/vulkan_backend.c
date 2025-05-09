#include "vulkan_backend.h"

#include "vulkan_types.inl"
#include "vulkan_device.h"
#include "vulkan_platform.h"
#include "vulkan_swapchain.h"
#include "vulkan_renderpass.h"
#include "vulkan_command_buffer.h"

#include "core/logger.h"
#include "core/kstring.h"
#include "core/asserts.h"
#include "core/kmemory.h"

#include "containers/darray.h"

#include "platform/platform.h"

static vulkan_context context;

VKAPI_ATTR VkBool32 VKAPI_CALL vk_debug_callback(
    VkDebugUtilsMessageSeverityFlagBitsEXT message_severity,
    VkDebugUtilsMessageTypeFlagsEXT message_types,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* pUserData);

static i32 find_memory_index(u32 type_filter, u32 property_flags);

static void create_command_buffers(renderer_backend* backend);

b8 vulkan_renderer_backend_initialize(struct renderer_backend* backend, const char* application_name, struct platform_state* plat_state) {
    context.allocator = 0;
    context.find_memory_index = find_memory_index;

    VkApplicationInfo app_info = {VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app_info.apiVersion = VK_API_VERSION_1_2;
    app_info.pApplicationName = application_name;
    app_info.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    app_info.pEngineName = "Kohi Engine";
    app_info.engineVersion = VK_MAKE_VERSION(1, 0, 0);

    VkInstanceCreateInfo create_info = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    create_info.pApplicationInfo = &app_info;

    const char** required_extensions = darray_create(const char*);
    char* generic_surface_extension_name = VK_KHR_SURFACE_EXTENSION_NAME;
    darray_push(required_extensions, generic_surface_extension_name);  // ??
    platform_get_required_extension_names(&required_extensions);       // Platform-specific extensions
#if defined(_DEBUG)
    char* debug_extension_name = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
    darray_push(required_extensions, debug_extension_name);

    KDEBUG("Required extensions:");
    u32 length = darray_length(required_extensions);
    for (u32 i = 0; i < length; ++i) {
        KDEBUG(required_extensions[i]);
    }
#endif

    create_info.enabledExtensionCount = darray_length(required_extensions);
    create_info.ppEnabledExtensionNames = required_extensions;

    const char** required_layer_names = 0;

#if defined(_DEBUG)
    KINFO("Validation layers enabled. Enumerating...");

    required_layer_names = darray_create(const char*);
    char* validation_layer_name = "VK_LAYER_KHRONOS_validation";
    darray_push(required_layer_names, validation_layer_name);

    u32 available_layer_count = 0;
    VK_CHECK(vkEnumerateInstanceLayerProperties(&available_layer_count, 0));
    VkLayerProperties* available_layers = darray_reserve(VkLayerProperties, available_layer_count);
    VK_CHECK(vkEnumerateInstanceLayerProperties(&available_layer_count, available_layers));

    u32 required_layer_count = darray_length(required_layer_names);
    for (u32 i = 0; i < required_layer_count; ++i) {
        KINFO("Searching for layers: %s...", required_layer_names[i]);
        b8 found = FALSE;
        for (u32 j = 0; j < available_layer_count; ++j) {
            if (strings_equal(required_layer_names[i], available_layers[j].layerName)) {
                found = TRUE;
                KINFO("Found.");
                break;
            }
        }

        if (!found) {
            KFATAL("required layer is missing: %s", required_layer_names[i]);
            return FALSE;
        }
    }
    KINFO("All required layers are present.");
#endif

    create_info.enabledLayerCount = (required_layer_names == 0) ? 0 : darray_length(required_layer_names);
    create_info.ppEnabledLayerNames = required_layer_names;

    VK_CHECK(vkCreateInstance(&create_info, context.allocator, &context.instance));
    KINFO("Vulkan instance created.");

#if defined(_DEBUG)
    KDEBUG("Creating Vulkan debugger...");
    u32 log_severity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT;

    VkDebugUtilsMessengerCreateInfoEXT debug_create_info = {VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
    debug_create_info.messageSeverity = log_severity;
    debug_create_info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT;
    debug_create_info.pfnUserCallback = vk_debug_callback;
    PFN_vkCreateDebugUtilsMessengerEXT pvkCreateDebugUtilsMessengerEXT = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(context.instance, "vkCreateDebugUtilsMessengerEXT");
    KASSERT_MSG(pvkCreateDebugUtilsMessengerEXT != 0, "Failed to load vkCreateDebugUtilsMessengerEXT.");

    VK_CHECK(pvkCreateDebugUtilsMessengerEXT(context.instance, &debug_create_info, context.allocator, &context.debug_messenger));
    KDEBUG("Vulkan debugger created!");
#endif

    KDEBUG("Creating Vulkan surface...");
    if (!platform_create_vulkan_surface(plat_state, &context)) {
        KERROR("Failed to create platform surface!");
        return FALSE;
    }
    KDEBUG("Vulkan surface created.");

    if (!vulkan_device_create(&context)) {
        KERROR("Failed to create device!");
        return FALSE;
    }

    vulkan_swapchain_create(&context, context.framebuffer_width, context.framebuffer_height, &context.swapchain);

    vulkan_renderpass_create(
        &context,
        &context.main_renderpass,
        0, 0, context.framebuffer_width, context.framebuffer_height,
        0.f, 0.f, .2f, 1.f,
        1.f,
        0);

    // Create command buffers.
    create_command_buffers(backend);
    KINFO("Command buffer created.");
    
    KINFO("Vulkan renderer initialized successfully.");
    return TRUE;
}

void vulkan_renderer_backend_shutdown(struct renderer_backend* backend) {
    for (u32 i = 0; i < context.swapchain.image_count; ++i) {
        if (context.graphics_command_buffers[i].handle) {
            vkFreeCommandBuffers(context.device.logical_device, context.device.graphics_command_pool, 1, &context.graphics_command_buffers[i].handle);
            context.graphics_command_buffers[i].handle = 0;
        }
    }
    darray_destroy(context.graphics_command_buffers);
    context.graphics_command_buffers = 0;

    vulkan_renderpass_destroy(&context, &context.main_renderpass);

    KDEBUG("Destroying Vulkan swapchain...");
    vulkan_swapchain_destroy(&context, &context.swapchain);

    KDEBUG("Destroying Vulkan device...");
    vulkan_device_destroy(&context);

    KDEBUG("Destroying Vulkan surface...");
    if (context.surface) {
        vkDestroySurfaceKHR(context.instance, context.surface, context.allocator);
        context.surface = 0;
    }
    
#if defined(_DEBUG)
    KDEBUG("Destroying Vulkan debugger...");
    if (context.debug_messenger) {
        PFN_vkDestroyDebugUtilsMessengerEXT pvkDestroyDebugUtilsMessengerEXT = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(context.instance, "vkDestroyDebugUtilsMessengerEXT");
        pvkDestroyDebugUtilsMessengerEXT(context.instance, context.debug_messenger, context.allocator);
    }
#endif

    KDEBUG("Destroying Vulkan instance...");
    if (context.instance) {
        vkDestroyInstance(context.instance, context.allocator);
    }
}

void vulkan_renderer_backend_resized(struct renderer_backend* backend, u16 width, u16 height) {
}

b8 vulkan_renderer_backend_begin_frame(struct renderer_backend* backend, f32 delta_time) {
    return TRUE;
}

b8 vulkan_renderer_backend_end_frame(struct renderer_backend* backend, f32 delta_time) {
    return TRUE;
}

VKAPI_ATTR VkBool32 VKAPI_CALL vk_debug_callback(
    VkDebugUtilsMessageSeverityFlagBitsEXT message_severity,
    VkDebugUtilsMessageTypeFlagsEXT message_types,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* pUserData) {
    switch (message_severity) {
        default:
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT:
            KERROR(pCallbackData->pMessage);
            break;
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT:
            KWARN(pCallbackData->pMessage);
            break;
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT:
            KINFO(pCallbackData->pMessage);
            break;
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT:
            KTRACE(pCallbackData->pMessage);
            break;
    }
    return VK_FALSE;
}

static i32 find_memory_index(u32 type_filter, u32 property_flags) {
    VkPhysicalDeviceMemoryProperties memory_properties;
    vkGetPhysicalDeviceMemoryProperties(context.device.physical_device, &memory_properties);
    for (u32 i = 0; i < memory_properties.memoryTypeCount; ++i) {
        if ((type_filter & (1 << i)) && ((memory_properties.memoryTypes[i].propertyFlags & property_flags) == property_flags)) {
            return i;
        }
    }

    KINFO("Unable to find suitable memory type.");
    return -1;
}

static void create_command_buffers(renderer_backend* backend) {
    if (!context.graphics_command_buffers) {
        context.graphics_command_buffers = darray_reserve(vulkan_command_buffer, context.swapchain.image_count);
    }

    for (u32 i = 0; i < context.swapchain.image_count; ++i) {
        if (context.graphics_command_buffers[i].handle) {
            vulkan_command_buffer_free(&context, context.device.graphics_command_pool, &context.graphics_command_buffers[i]);
        }
        kzero_memory(&context.graphics_command_buffers[i], sizeof(vulkan_command_buffer));
        vulkan_command_buffer_allocate(&context, context.device.graphics_command_pool, TRUE, &context.graphics_command_buffers[i]);
    }
}


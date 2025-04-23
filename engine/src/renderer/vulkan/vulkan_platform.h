#pragma once

#include "defines.h"

typedef struct platform_state platform_state;
typedef struct vulkan_context vulkan_context;

b8 platform_create_vulkan_surface(
    platform_state* plat_state,
    vulkan_context* context);

void platform_get_required_extension_names(const char*** names_darray);
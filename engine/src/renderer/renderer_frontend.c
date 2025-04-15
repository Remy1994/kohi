#include "renderer_frontend.h"
#include "renderer_backend.h"

#include "core/logger.h"
#include "core/kmemory.h"

static renderer_backend* backend = 0;

static b8 renderer_begin_frame(f32 delta_time);
static b8 renderer_end_frame(f32 delta_time);

b8 renderer_initialize(const char* application_name, struct platform_state* plat_state) {
    backend = kallocate(sizeof(*backend), MEMORY_TAG_RENDERER);

    renderer_backend_create(RENDERER_BACKEND_TYPE_VULKAN, plat_state, backend);

    if (!backend->initialize(backend, application_name, plat_state)) {
        KFATAL("Renderer backend failed to initialize. Shutting down.");
        return FALSE;
    }

    return TRUE;
}

void renderer_shutdown() {
    backend->shutdown(backend);
    kfree(backend, sizeof(*backend), MEMORY_TAG_RENDERER);
}

void renderer_on_resized(u16 width, u16 height);

b8 renderer_draw_frame(render_packet* packet) {
    // Begin frame can fail.
    if (renderer_begin_frame(packet->delta_time)) {
        // End frame can not fail.
        if (!renderer_end_frame(packet->delta_time)) {
            KERROR("renderer_end_frame failed. Application shutting down...");
            return FALSE;
        }
    }

    return TRUE;
}

static b8 renderer_begin_frame(f32 delta_time) {
    return backend->begin_frame(backend, delta_time);
}

static b8 renderer_end_frame(f32 delta_time) {
    b8 result = backend->end_frame(backend, delta_time);
    backend->frame_number++;
    return result;
}
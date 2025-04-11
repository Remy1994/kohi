#include "application.h"

#include "game_types.h"
#include "core/logger.h"
#include "core/kmemory.h"
#include "core/event.h"
#include "core/input.h"
#include "platform/platform.h"

typedef struct application_state {
    game* game_inst;
    b8 is_running;
    b8 is_suspended;
    platform_state platform;
    i16 width;
    i16 height;
    f64 last_time;
} application_state;

static b8 initialized = FALSE;
static application_state app_state;

b8 application_create(game* game_inst) {
    if (initialized) {
        KERROR("application_create called more than once.");
        return FALSE;
    }

    app_state.game_inst = game_inst;

    // Initialize subsystems.
    initialize_logging();

    KFATAL("The number is %f.", 3.14f);
    KERROR("The number is %f.", 3.14f);
    KWARN("The number is %f.", 3.14f);
    KINFO("The number is %f.", 3.14f);
    KDEBUG("The number is %f.", 3.14f);
    KTRACE("The number is %f.", 3.14f);

    app_state.is_running = TRUE;
    app_state.is_suspended = FALSE;

    if (!event_initialize()) {
        KERROR("Event system failed initialization. Application cannot countinue.");
        return FALSE;
    }

    input_initialize();

    if (!platform_startup(
            &app_state.platform,
            game_inst->app_config.name,
            game_inst->app_config.start_pos_x, game_inst->app_config.start_pos_y,
            game_inst->app_config.start_width, game_inst->app_config.start_height)) {
        return FALSE;
    }

    if (!app_state.game_inst->initialize(app_state.game_inst)) {
        KFATAL("Game failed to initialize.");
        return FALSE;
    }

    app_state.game_inst->on_resize(app_state.game_inst, app_state.width, app_state.height);

    initialized = TRUE;
    return TRUE;
}

b8 application_run() {
    KINFO(get_memory_usage_str());

    while (app_state.is_running) {
        if (!platform_pump_messages(&app_state.platform)) {
            app_state.is_running = FALSE;
        }

        if (!app_state.is_suspended){
            if (!app_state.game_inst->update(app_state.game_inst, 0.f)) {
                KFATAL("Game update failed, shutting down.");
                app_state.is_running = FALSE;
                break;
            }

            if (!app_state.game_inst->render(app_state.game_inst, 0.f)) {
                KFATAL("Game render failed, shutting down.");
                app_state.is_running = FALSE;
                break;
            }

            input_update(0);
        }
        
    }

    app_state.is_running = FALSE;

    platform_shutdown(&app_state.platform);
    input_shutdown();
    event_shutdown();
    shutdown_logging();
    
    return TRUE;
}
#include <entry.h>
#include "game.h"
#include <core/kmemory.h>

b8 create_game(game* game_inst) {
    game_inst->app_config.name = "Kohi Engine Testbed";
    game_inst->app_config.start_pos_x = 100;
    game_inst->app_config.start_pos_y = 100;
    game_inst->app_config.start_width = 1280;
    game_inst->app_config.start_height = 720;

    game_inst->initialize = game_initialize;
    game_inst->on_resize = game_on_resize;
    game_inst->render = game_render;
    game_inst->update = game_update;

    game_inst->state = kallocate(sizeof(game_state), MEMORY_TAG_GAME);

    
    return TRUE;
}
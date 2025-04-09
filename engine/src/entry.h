#pragma once

#include "game_types.h"
#include "core/logger.h"
#include "core/kmemory.h"

extern b8 create_game(game* out_game);

int main(void) {
    initialize_memory();

    game game_inst;
    if (!create_game(&game_inst)) {
        KFATAL("Failed to create game!");
        return -1;
    }

    if (!game_inst.render || !game_inst.initialize || !game_inst.on_resize || !game_inst.update) {
        KFATAL("The game's function pointers must be assigned!");
        return -2;
    }

    if (!application_create(&game_inst)) {
        KINFO("Application create failed!");
        return 1;
    }

    if (!application_run()) {
        KINFO("Application did not shutdown gracefully.");
        return 2;
    }
    
    shutdown_memory();
    
    return 0;
}
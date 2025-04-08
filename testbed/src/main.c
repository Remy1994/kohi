#include <core/logger.h>
#include <core/asserts.h>
#include <platform/platform.h>

int main() {
    KFATAL("The number is %f.", 3.14f);
    KERROR("The number is %f.", 3.14f);
    KWARN("The number is %f.", 3.14f);
    KINFO("The number is %f.", 3.14f);
    KDEBUG("The number is %f.", 3.14f);
    KTRACE("The number is %f.", 3.14f);

    platform_state state;
    if (platform_startup(&state, "Kohi Engine Testbed", 100, 100, 1280, 720)) {
        while (TRUE) {
            platform_pump_messages(&state);
        }
    }

    platform_shutdown(&state);
    return 0;
}
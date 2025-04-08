#include <core/logger.h>
#include <core/asserts.h>

int main() {
    KFATAL("The number is %f.", 3.14f);
    KERROR("The number is %f.", 3.14f);
    KWARN("The number is %f.", 3.14f);
    KINFO("The number is %f.", 3.14f);
    KDEBUG("The number is %f.", 3.14f);
    KTRACE("The number is %f.", 3.14f);

//    KASSERT(1 ==  0)
    KASSERT_MSG(1 == 0, "NOT EQUAL!")
    KASSERT_DEBUG(1 == 0)
    return 0;
}
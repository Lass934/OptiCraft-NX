#ifdef SWITCH_PLATFORM
#include <switch.h>
#include "client/Minecraft.h"
#include "java/String.h"
#include "switch/render/SwitchGraphicsContext.h"
int main(int, char**)
{
    if (!appletMainLoop()) return 0;
    if (!SwitchGraphicsContext::instance().initialize()) return 1;
    jstring username = "Player", auth = "-";
    Minecraft::start(&username, &auth);
    SwitchGraphicsContext::instance().shutdown();
    return 0;
}
#endif

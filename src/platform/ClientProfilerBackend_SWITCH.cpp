#include "platform/ClientProfilerBackend.h"
#include "switch/SwitchRuntimeDebug.h"

namespace ClientProfilerBackend
{
void frameBegin() {}
void ticks(long long, int) {}
void lighting(long long ns) { switchProfileTickPhase("frameLighting", ns); }
void displayUpdate(long long) {}
void render(long long) {}
void frameEnd(long long, long long, long long, int, int, World*, RenderGlobal*) {}
}

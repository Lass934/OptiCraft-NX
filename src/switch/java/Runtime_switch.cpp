#ifdef SWITCH_PLATFORM
#include "java/Runtime.h"
#include <switch.h>

// Report what Horizon actually granted this process instead of a fixed figure.
// The budget depends on how the NRO was launched: applet mode (Album) gets a
// few hundred MB, title takeover (hold R while starting a game) gets the full
// application pool, about 3 GB. These values only feed the F3 overlay.
namespace
{
long_t processInfo(InfoType type)
{
    u64 value = 0;
    if (R_FAILED(svcGetInfo(&value, type, CUR_PROCESS_HANDLE, 0)))
        return 0;
    return static_cast<long_t>(value);
}
}

Runtime Runtime::instance;
Runtime& Runtime::getRuntime() { return instance; }
long_t Runtime::maxMemory() { return processInfo(InfoType_TotalMemorySize); }
long_t Runtime::totalMemory() { return maxMemory(); }
long_t Runtime::freeMemory()
{
    const long_t total = processInfo(InfoType_TotalMemorySize);
    const long_t used = processInfo(InfoType_UsedMemorySize);
    return total > used ? total - used : 0;
}
#endif

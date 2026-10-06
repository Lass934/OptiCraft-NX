#include "platform/Profiler.h"
#include "switch/SwitchRuntimeDebug.h"

#include <switch.h>

// 32 bits of the 19.2 MHz system counter wrap every ~223 s; unsigned
// subtraction still yields the right duration for any phase shorter than that.
std::uint32_t platformProfileRenderPhaseBegin() { return static_cast<std::uint32_t>(armGetSystemTick()); }
void platformProfileRenderPhaseEnd(std::uint32_t begin, PlatformRenderPhase phase)
{
    const std::uint32_t now = static_cast<std::uint32_t>(armGetSystemTick());
    switchProfileRenderPhase(static_cast<int>(phase), static_cast<std::uint64_t>(now - begin));
}
void platformProfileTickPhase(const char *name, long long ns) { switchProfileTickPhase(name, ns); }
void platformProfileChunkBuild(long long, int) {}
void platformProfileChunkMeshPass(int, long long, int) {}
void platformProfileSnowColumn(bool, bool, int) {}
void platformProfilePopulatePhase(PlatformPopulatePhase, long long) {}
void platformProfileChunkLoad(long long) {}
void platformProfilePopulate(long long) {}
void platformProfileGenerate(long long) {}
void platformProfileMesh(long long) {}
void platformProfileUnloadSave(long long) {}
void platformProfileTickUpdates(long long) {}
void platformProfileTickQueue(long long) {}
void platformProfileMobSpawn(long long) {}
void platformProfileSaveWorldInfo(long long) {}
void platformProfileMapStorage(long long) {}
void platformProfileChunkEvict(long long) {}

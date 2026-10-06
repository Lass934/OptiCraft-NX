#include "platform/ClientPlatformPolicy.h"
#include "net/minecraft/src/GameSettings.h"
#include "platform/Log.h"

#include <cstdio>

namespace ClientPlatformPolicy
{
int initialWidth(){return 1280;}
int initialHeight(){return 720;}
std::string minecraftDirectory(){return "sdmc:/switch/OptiCraft/.minecraft";}
bool saveConverterUsesSavesSubdirectory(){return true;}
void applyGameSettingsDefaults(GameSettings*s){if(s){s->renderDistance=2;s->advancedOpengl=false;s->ofChunkUpdates=1;}}
void preloadStartupTextures(RenderEngine*){}
void releaseWorldEntryAssets(RenderEngine*){}
int panoramaSampleGrid(){return 4;}
void reportCrash(const std::string& description)
{
    MC_LOG_ERROR("switch", "%s\n", description.c_str());
    // stderr is normally invisible when launched from hbmenu. Preserve the
    // exception description beside the application so a startup failure is
    // diagnosable without nxlink.
    if (std::FILE* file = std::fopen("sdmc:/switch/OptiCraft/crash.log", "wb"))
    {
        std::fwrite(description.data(), 1, description.size(), file);
        std::fwrite("\n", 1, 1, file);
        std::fclose(file);
    }
}
}

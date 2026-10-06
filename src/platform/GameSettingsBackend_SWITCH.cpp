#include "platform/GameSettingsBackend.h"
#include "net/minecraft/src/GameSettings.h"
#include "platform/PlatformTuning.h"
void platformGameSettingsInitialize(GameSettings&){} void platformGameSettingsResetControlBindings(GameSettings&){}
int_t platformGameSettingsDefaultChunkUpdates(){return PLATFORM_MAX_RENDERER_UPDATES_PER_FRAME;} int_t platformGameSettingsDefaultConnectedTextures(){return 2;}
int_t platformGameSettingsCycleRenderDistance(int_t c,int_t d){c+=d;while(c>3)c-=3;while(c<1)c+=3;return c;} int_t platformGameSettingsClampRenderDistance(int_t v){return v<1?1:(v>3?3:v);} int_t platformGameSettingsClampFineRenderDistance(int_t v){return v<32?32:(v>128?128:v);} void platformGameSettingsUpdateRenderDistanceFromFine(int_t f,int_t&r){r=f>64?1:(f>32?2:3);} bool platformGameSettingsAnaglyphValue(bool,bool){return false;}
bool platformGameSettingsLoadOption(GameSettings&,const std::string&,const std::string&){return false;} void platformGameSettingsFinalizeLoad(GameSettings&s){s.renderDistance=platformGameSettingsClampRenderDistance(s.renderDistance);s.advancedOpengl=false;} void platformGameSettingsSyncControllerBindings(const GameSettings&){} void platformGameSettingsAddKnownKeys(std::unordered_set<std::string>&){} void platformGameSettingsWriteOptions(const GameSettings&,std::ostream&){}

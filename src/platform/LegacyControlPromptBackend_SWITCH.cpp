#include "platform/LegacyControlPromptBackend.h"
#include "net/minecraft/src/GameSettings.h"
std::string legacyControlPromptLabel(const GameSettings&, LegacyControlAction action){ switch(action){case LegacyControlAction::Inventory:return "X";case LegacyControlAction::Drop:return "B";case LegacyControlAction::Jump:return "A";case LegacyControlAction::Attack:return "ZR";case LegacyControlAction::Use:return "ZL";} return {}; }

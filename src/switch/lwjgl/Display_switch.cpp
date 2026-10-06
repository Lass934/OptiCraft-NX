#ifdef SWITCH_PLATFORM
#include "lwjgl/Display.h"
#include "switch/input/SwitchInput.h"
#include "switch/render/SwitchGraphicsContext.h"
namespace { bool created = false; }
namespace lwjgl::Display {
void create() { if (!created) created = SwitchGraphicsContext::instance().initialize(); }
void setDisplayMode(const DisplayMode&) {}
DisplayMode getDisplayMode() { return DisplayMode(1280, 720); }
void setTitle(const jstring&) {} void setFullscreen(bool) {}
bool isCloseRequested() { return !SwitchGraphicsContext::instance().alive(); }
bool isVisible() { return true; } bool isActive() { return true; }
void processMessages() { switchInputPoll(); }
void swapBuffers() { SwitchGraphicsContext::instance().present(); }
void update(bool process) { swapBuffers(); if (process) processMessages(); }
int_t getX() { return 0; } int_t getY() { return 0; }
int_t getWidth() { return 1280; } int_t getHeight() { return 720; }
}
#endif

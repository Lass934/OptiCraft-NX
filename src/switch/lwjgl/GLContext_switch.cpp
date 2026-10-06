#ifdef SWITCH_PLATFORM
#include "lwjgl/GLContext.h"
namespace lwjgl::GLContext {
namespace { detail::GLCapabilities capabilities; int samples = 0; }
void setRequestedSamples(int value) { samples = value; }
int getRequestedSamples() { return samples; }
void instantiate() {}
const detail::GLCapabilities& getCapabilities() { return capabilities; }
}
#endif

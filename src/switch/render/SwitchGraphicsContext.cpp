#include "switch/render/SwitchGraphicsContext.h"
#include "switch/SwitchRuntimeDebug.h"

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <glad/glad.h>
#include <switch.h>

#include <cstdio>

namespace
{
EGLDisplay g_display = EGL_NO_DISPLAY;
EGLContext g_context = EGL_NO_CONTEXT;
EGLSurface g_surface = EGL_NO_SURFACE;

void logEglFailure(const char *operation)
{
    std::fprintf(stderr, "Switch EGL: %s failed (0x%04x)\n", operation,
                 static_cast<unsigned int>(eglGetError()));
}
}

SwitchGraphicsContext &SwitchGraphicsContext::instance()
{
    static SwitchGraphicsContext value;
    return value;
}

bool SwitchGraphicsContext::initialize()
{
    if (alive_) return true;

    g_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (g_display == EGL_NO_DISPLAY)
    {
        logEglFailure("eglGetDisplay");
        return false;
    }
    if (eglInitialize(g_display, nullptr, nullptr) == EGL_FALSE)
    {
        logEglFailure("eglInitialize");
        shutdown();
        return false;
    }
    if (eglBindAPI(EGL_OPENGL_API) == EGL_FALSE)
    {
        logEglFailure("eglBindAPI");
        shutdown();
        return false;
    }

    constexpr EGLint configAttributes[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_DEPTH_SIZE, 24,
        EGL_STENCIL_SIZE, 8,
        EGL_NONE};
    EGLConfig config = nullptr;
    EGLint configCount = 0;
    if (eglChooseConfig(g_display, configAttributes, &config, 1, &configCount) == EGL_FALSE ||
        configCount == 0)
    {
        logEglFailure("eglChooseConfig");
        shutdown();
        return false;
    }

    g_surface = eglCreateWindowSurface(g_display, config, nwindowGetDefault(), nullptr);
    if (g_surface == EGL_NO_SURFACE)
    {
        logEglFailure("eglCreateWindowSurface");
        shutdown();
        return false;
    }

    constexpr EGLint contextAttributes[] = {
        EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR,
        EGL_CONTEXT_MAJOR_VERSION_KHR, 4,
        EGL_CONTEXT_MINOR_VERSION_KHR, 3,
        EGL_NONE};
    g_context = eglCreateContext(g_display, config, EGL_NO_CONTEXT, contextAttributes);
    if (g_context == EGL_NO_CONTEXT)
    {
        logEglFailure("eglCreateContext");
        shutdown();
        return false;
    }
    if (eglMakeCurrent(g_display, g_surface, g_surface, g_context) == EGL_FALSE)
    {
        logEglFailure("eglMakeCurrent");
        shutdown();
        return false;
    }
    if (gladLoadGL() == 0)
    {
        std::fprintf(stderr, "Switch EGL: gladLoadGL failed\n");
        shutdown();
        return false;
    }

    eglSwapInterval(g_display, 1);
    alive_ = true;
    return true;
}

void SwitchGraphicsContext::shutdown()
{
    alive_ = false;
    if (g_display == EGL_NO_DISPLAY) return;
    eglMakeCurrent(g_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    if (g_context != EGL_NO_CONTEXT) eglDestroyContext(g_display, g_context);
    if (g_surface != EGL_NO_SURFACE) eglDestroySurface(g_display, g_surface);
    eglTerminate(g_display);
    g_context = EGL_NO_CONTEXT;
    g_surface = EGL_NO_SURFACE;
    g_display = EGL_NO_DISPLAY;
}

bool SwitchGraphicsContext::alive() const
{
    return alive_ && appletMainLoop();
}

void SwitchGraphicsContext::present()
{
    // Time spent here is the CPU waiting for the GPU to finish and for vsync.
    const u64 start = armGetSystemTick();
    if (alive_ && eglSwapBuffers(g_display, g_surface) == EGL_FALSE)
    {
        logEglFailure("eglSwapBuffers");
        alive_ = false;
    }
    switchProfileSwap(armGetSystemTick() - start);
}

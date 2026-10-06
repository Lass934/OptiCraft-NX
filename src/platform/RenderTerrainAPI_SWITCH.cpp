#include "platform/RenderTerrainAPI.h"
#include "platform/RenderAPI.h"
#include "switch/SwitchRuntimeDebug.h"
#include "switch/render/SwitchLegacyRenderer.h"

#include <glad/glad.h>
#include <cstdio>
#include <string>

namespace { int s_nextTerrainHandle = 1; }
int renderTerrainCreateChunkHandle() { return s_nextTerrainHandle++; }
void renderTerrainDestroyChunkHandle(int) {}
void renderTerrainClearChunkHandle(int) {}
void renderTerrainSwapChunkHandles(int, int) {}
bool renderTerrainBeginChunkBatch(int) { return false; }
bool renderTerrainAppendChunk(int) { return false; }
void renderTerrainEndChunkBatch() {}
void renderTerrainCaptureCamera() {}
void renderTerrainSetViewerPosition(double, double, double) {}
void renderTerrainSetFog(RenderFogMode, float, float, float, float, float, float, float) {}
void renderTerrainSetEarlyDepth(bool) {}
bool renderTerrainSortOpaqueFaces(const std::vector<int_t>&, std::vector<int_t>&, int, int) { return false; }
namespace
{
// Snapshot of the GL and shader state the terrain pass draws with, written to
// status.log on the first pass and then every ~10 s. The screen cannot be
// trusted to report it when the terrain itself is what fails to show up.
void logTerrainPassState(int texture, RenderTerrainPass pass)
{
    static int calls = 0;
    if ((calls++ % 600) != 0)
        return;
    GLint bound = 0, active = 0, width = 0, height = 0, depthFunc = 0, viewport[4] = {};
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &bound);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &active);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &width);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &height);
    glGetIntegerv(GL_DEPTH_FUNC, &depthFunc);
    glGetIntegerv(GL_VIEWPORT, viewport);
    GLboolean depthMask = GL_FALSE;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMask);
    const GLenum error = glGetError();
    char text[400];
    std::snprintf(text, sizeof(text),
        "terrainPass=%d tex=%d bound=%d unit=0x%x size=%dx%d depthTest=%d depthFunc=0x%x depthMask=%d blend=%d vp=%d,%d,%d,%d glErr=0x%x | %s",
        static_cast<int>(pass), texture, bound, active, width, height,
        glIsEnabled(GL_DEPTH_TEST) ? 1 : 0, depthFunc, depthMask ? 1 : 0,
        glIsEnabled(GL_BLEND) ? 1 : 0, viewport[0], viewport[1], viewport[2], viewport[3],
        error, SwitchLegacyRenderer::describeState().c_str());
    switchDebugNote(text);
}
}

bool renderTerrainBeginPass(int texture, RenderTerrainPass pass)
{
    renderBindTexture(texture);
    logTerrainPassState(texture, pass);
    // Back-face culling stays as the shared renderer set it, like desktop GL.
    // Tessellator splits each quad into (0,1,2)(0,2,3), which keeps its
    // winding. Culling used to be disabled here while terrain was invisible,
    // but that was the shared display-list handle, not winding; with culling
    // off every double-sided quad (tall grass, flowers, fancy leaves) drew
    // both of its coplanar, mirrored faces and z-fought.
    return true;
}
void renderTerrainEndPass(RenderTerrainPass pass)
{
    (void)pass;
}
std::size_t renderTerrainLiveBytes() { return 0; }
std::size_t renderTerrainStagingBytes() { return 0; }

bool renderTerrainCaptureFrame(RenderTerrainFrame& out) { out = RenderTerrainFrame{}; return false; }
RenderTerrainDrawResult renderTerrainDrawSection(const RenderTerrainFrame&, const RenderTerrainSectionView&, const RenderTerrainFallbackDraw&) { return {}; }

void renderTerrainCacheInit(RenderTerrainBackendCache& cache) { cache.initialized = true; }
void renderTerrainCacheDestroy(RenderTerrainBackendCache& cache) { cache.initialized = false; }
void renderTerrainCacheReset(RenderTerrainBackendCache&) {}
void renderTerrainCacheRelease(RenderTerrainBackendCache&) {}
std::size_t renderTerrainCacheRamBytes(const RenderTerrainBackendCache&) { return 0; }
void renderTerrainCacheRamBreakdown(const RenderTerrainBackendCache&, RenderTerrainCacheRamBreakdown&) {}
bool renderTerrainCacheSortFaces(RenderTerrainBackendCache&, const int_t*, int_t*, int_t) { return false; }
bool renderTerrainCacheBuildOpaque(RenderTerrainBackendCache&, const int_t*, std::size_t, int_t, int_t, bool, bool, bool) { return false; }
bool renderTerrainCacheOpaqueValid(const RenderTerrainBackendCache&) { return false; }
int_t renderTerrainCacheOpaqueVertexCount(const RenderTerrainBackendCache&) { return 0; }
const void* renderTerrainCacheFaceGroups(const RenderTerrainBackendCache&) { return nullptr; }
const void* renderTerrainCacheOpaqueMesh(const RenderTerrainBackendCache&) { return nullptr; }

bool renderTerrainIsGreedyCube(Block*) { return false; }
bool renderTerrainGreedyMeshFace(ChunkCache&, int, int, int, int, int, int, int) { return false; }

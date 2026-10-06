#pragma once

#include "platform/PlatformConfig.h"

#if PLATFORM_ASYNC_CHUNK_MESHING

#include <atomic>
#include <memory>
#include <vector>

#include "net/minecraft/src/Tessellator.h"

class World;
class WorldRenderer;
class SwitchSectionSnapshot;

// Off-thread chunk-section meshing.
//
// WorldRenderer::updateRenderer() asks for a job instead of meshing inline.
// A job is created for any section without container blocks (chests,
// furnaces, signs, ...), which need their TileEntity and keep the section on
// the main thread. Renderers that resize the shared Block object (stairs,
// fences, slabs, doors, ...) write the mesher thread's own copy of the bounds
// (BlockBound), so collision on the game thread is unaffected. The job carries
// a copy of the surrounding sections, so the worker never touches World or
// Chunk.
struct SwitchMeshJob
{
    SwitchMeshJob();
    ~SwitchMeshJob();

    // Touched on the main thread only, and only while !cancelled.
    WorldRenderer *renderer = nullptr;
    int posX = 0;
    int posY = 0;
    int posZ = 0;
    int size = 16;
    std::unique_ptr<SwitchSectionSnapshot> snapshot;

    // Results, written by the worker before `finished` is set.
    RenderCapturedMesh passMesh[2];
    std::vector<TessellatorTextureMesh> extraMeshes[2];
    bool drew[2] = {false, false};
    bool isLit = false;
    bool failed = false;

    std::atomic<bool> cancelled{false};
};

// Main thread. Returns a queued job, or nullptr when the section has a block
// that must be meshed on the main thread or the worker queue is full.
std::shared_ptr<SwitchMeshJob> switchSubmitChunkMesh(World *world, WorldRenderer *renderer,
                                                     int posX, int posY, int posZ, int size);

// Main thread, once per frame: hands up to maxJobs finished meshes to their
// renderers, which compile them into display lists.
void switchPublishChunkMeshes(int maxJobs);

#endif

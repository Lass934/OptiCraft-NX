#include "switch/minecraft/SwitchChunkMesher.h"

#if PLATFORM_ASYNC_CHUNK_MESHING

#include <condition_variable>
#include <deque>
#include <mutex>

#include "java/Arithmetic.h"
#include "net/minecraft/src/BiomeGenBase.h"
#include "net/minecraft/src/Block.h"
#include "net/minecraft/src/Chunk.h"
#include "net/minecraft/src/EnumSkyBlock.h"
#include "net/minecraft/src/ExtendedBlockStorage.h"
#include "net/minecraft/src/IBlockAccess.h"
#include "net/minecraft/src/Material.h"
#include "net/minecraft/src/NibbleArray.h"
#include "net/minecraft/src/RenderBlocks.h"
#include "net/minecraft/src/Vec3D.h"
#include "net/minecraft/src/World.h"
#include "net/minecraft/src/WorldHeight.h"
#include "net/minecraft/src/WorldProvider.h"
#include "net/minecraft/src/WorldRenderer.h"
#include "platform/Thread.h"

// ---------------------------------------------------------------------------
// Section snapshot: an IBlockAccess over copies of the 3x3 chunks x 3 sections
// around one 16^3 render section. Every accessor mirrors ChunkCache (and the
// Chunk methods it falls back to) so the mesh is identical to an inline build.
// ---------------------------------------------------------------------------
class SwitchSectionSnapshot : public IBlockAccess
{
public:
    static constexpr int kChunks = 3;
    static constexpr int kSections = 3;
    static constexpr int kBiomeSpan = 18; // section +/- 1 block

    struct ChunkCopy
    {
        bool present = false;
        std::unique_ptr<ExtendedBlockStorage> sections[kSections];
    };

    int originChunkX = 0;
    int originChunkZ = 0;
    int firstSection = 0;
    ChunkCopy chunks[kChunks][kChunks];
    int biomeX0 = 0;
    int biomeZ0 = 0;
    BiomeGenBase *biomes[kBiomeSpan * kBiomeSpan] = {};
    int skylightSubtracted = 0;
    bool hasNoSky = false;
    float brightnessTable[16] = {};
    bool levelsEmpty = true;
    bool lit = false;

    const ChunkCopy *chunkAt(int i, int k) const
    {
        const int cx = JavaArithmetic::intSub(JavaArithmetic::intShr(i, 4), originChunkX);
        const int cz = JavaArithmetic::intSub(JavaArithmetic::intShr(k, 4), originChunkZ);
        if (cx < 0 || cx >= kChunks || cz < 0 || cz >= kChunks)
            return nullptr;
        const ChunkCopy &chunk = chunks[cx][cz];
        return chunk.present ? &chunk : nullptr;
    }

    // Null both for an empty section and for one outside the copied range;
    // queries stay within one block of the section, inside the copy.
    static const ExtendedBlockStorage *sectionOf(const ChunkCopy *chunk, int j, int firstSection)
    {
        const int s = (j >> 4) - firstSection;
        if (chunk == nullptr || s < 0 || s >= kSections)
            return nullptr;
        return chunk->sections[s].get();
    }

    int_t getBlockId(int_t i, int_t j, int_t k) override
    {
        if (j < WorldHeight::MIN_Y || j >= WorldHeight::HEIGHT)
            return 0;
        const ExtendedBlockStorage *section = sectionOf(chunkAt(i, k), j, firstSection);
        return section != nullptr ? section->getExtBlockID(i & 0xf, j & 0xf, k & 0xf) : 0;
    }

    TileEntity *getBlockTileEntity(int_t, int_t, int_t) override
    {
        return nullptr; // container blocks never reach the worker
    }

    int_t savedLight(EnumSkyBlock *type, int_t i, int_t j, int_t k) const
    {
        if (type == nullptr)
            return 0;
        if (j < 0)
            j = 0;
        if (j >= WorldHeight::HEIGHT)
            j = WorldHeight::MAX_Y;
        if (i < -30000000 || k < -30000000 || i >= 30000000 || k > 30000000)
            return type->defaultLightValue;
        const ChunkCopy *chunk = chunkAt(i, k);
        if (chunk == nullptr)
            return type->defaultLightValue;
        const ExtendedBlockStorage *section = sectionOf(chunk, j, firstSection);
        if (section == nullptr)
            return type->defaultLightValue; // Chunk::getSavedLightValue
        if (type == EnumSkyBlock::Sky)
            return section->getExtSkylightValue(i & 15, j & 15, k & 15);
        if (type == EnumSkyBlock::Block)
            return section->getExtBlocklightValue(i & 15, j & 15, k & 15);
        return type->defaultLightValue;
    }

    int_t skyBlockTypeBrightness(EnumSkyBlock *type, int_t i, int_t j, int_t k)
    {
        if (type == nullptr)
            return 0;
        if (j < 0)
            j = 0;
        if (j >= WorldHeight::HEIGHT)
            j = WorldHeight::MAX_Y;
        if (i < -30000000 || k < -30000000 || i >= 30000000 || k > 30000000)
            return type->defaultLightValue;
        const int_t blockId = getBlockId(i, j, k);
        if (blockId >= 0 && blockId < Block::BLOCK_REGISTRY_SIZE && Block::useNeighborBrightness[blockId])
        {
            int_t brightness = savedLight(type, i, j + 1, k);
            const int_t east = savedLight(type, i + 1, j, k);
            const int_t west = savedLight(type, i - 1, j, k);
            const int_t south = savedLight(type, i, j, k + 1);
            const int_t north = savedLight(type, i, j, k - 1);
            if (east > brightness) brightness = east;
            if (west > brightness) brightness = west;
            if (south > brightness) brightness = south;
            if (north > brightness) brightness = north;
            return brightness;
        }
        return savedLight(type, i, j, k);
    }

    int_t getLightBrightnessForSkyBlocks(int_t i, int_t j, int_t k, int_t minimumBlockLight) override
    {
        const int_t skyLight = skyBlockTypeBrightness(EnumSkyBlock::Sky, i, j, k);
        int_t blockLight = skyBlockTypeBrightness(EnumSkyBlock::Block, i, j, k);
        if (blockLight < minimumBlockLight)
            blockLight = minimumBlockLight;
        return (skyLight << 20) | (blockLight << 4);
    }

    int_t lightValueExt(int_t i, int_t j, int_t k, bool flag)
    {
        if (i < -30000000 || k < -30000000 || i >= 30000000 || k > 30000000)
            return 15;
        if (flag)
        {
            const int_t l = getBlockId(i, j, k);
            if (l == Block::stairSingle->blockID || l == Block::tilledField->blockID ||
                l == Block::stairCompactPlanks->blockID || l == Block::stairCompactCobblestone->blockID)
            {
                int_t k1 = lightValueExt(i, j + 1, k, false);
                const int_t i2 = lightValueExt(i + 1, j, k, false);
                const int_t j2 = lightValueExt(i - 1, j, k, false);
                const int_t k2 = lightValueExt(i, j, k + 1, false);
                const int_t l2 = lightValueExt(i, j, k - 1, false);
                if (i2 > k1) k1 = i2;
                if (j2 > k1) k1 = j2;
                if (k2 > k1) k1 = k2;
                if (l2 > k1) k1 = l2;
                return k1;
            }
        }
        if (j < 0)
            return 0;
        if (j >= WorldHeight::HEIGHT)
        {
            const int_t value = 15 - skylightSubtracted;
            return value < 0 ? 0 : value;
        }
        const ChunkCopy *chunk = chunkAt(i, k);
        if (chunk == nullptr)
            return 0;
        // Chunk::getBlockLightValue
        const ExtendedBlockStorage *section = sectionOf(chunk, j, firstSection);
        const bool hasSky = !hasNoSky;
        if (section == nullptr)
            return hasSky && skylightSubtracted < EnumSkyBlock::Sky->defaultLightValue
                ? EnumSkyBlock::Sky->defaultLightValue - skylightSubtracted : 0;
        int_t skyLight = hasSky ? section->getExtSkylightValue(i & 15, j & 15, k & 15) : 0;
        if (skyLight > 0)
            lit = true;
        skyLight -= skylightSubtracted;
        const int_t blockLight = section->getExtBlocklightValue(i & 15, j & 15, k & 15);
        return blockLight > skyLight ? blockLight : skyLight;
    }

    float getBrightness(int_t i, int_t j, int_t k, int_t l) override
    {
        int_t value = lightValueExt(i, j, k, true);
        if (value < l)
            value = l;
        return brightnessTable[value & 15];
    }

    float getLightBrightness(int_t i, int_t j, int_t k) override
    {
        return brightnessTable[lightValueExt(i, j, k, true) & 15];
    }

    int_t getBlockMetadata(int_t i, int_t j, int_t k) override
    {
        if (j < 0 || j >= WorldHeight::HEIGHT)
            return 0;
        const ExtendedBlockStorage *section = sectionOf(chunkAt(i, k), j, firstSection);
        return section != nullptr ? section->getExtBlockMetadata(i & 0xf, j & 0xf, k & 0xf) : 0;
    }

    Material *getBlockMaterial(int_t i, int_t j, int_t k) override
    {
        const int_t id = getBlockId(i, j, k);
        if (id == 0 || Block::blocksList[id] == nullptr)
            return Material::air;
        return Block::blocksList[id]->blockMaterial;
    }

    bool isBlockOpaqueCube(int_t i, int_t j, int_t k) override
    {
        Block *block = Block::blocksList[getBlockId(i, j, k)];
        return block != nullptr && block->isOpaqueCube();
    }

    bool isBlockNormalCube(int_t i, int_t j, int_t k) override
    {
        Block *block = Block::blocksList[getBlockId(i, j, k)];
        return block != nullptr && block->blockMaterial->getIsSolid() && block->renderAsNormalBlock();
    }

    bool isAirBlock(int_t i, int_t j, int_t k) override
    {
        const int_t id = getBlockId(i, j, k);
        return id <= 0 || id >= Block::BLOCK_REGISTRY_SIZE || Block::blocksList[id] == nullptr;
    }

    BiomeGenBase *getBiomeGenForCoords(int_t i, int_t k) override
    {
        const int x = i - biomeX0;
        const int z = k - biomeZ0;
        if (x < 0 || x >= kBiomeSpan || z < 0 || z >= kBiomeSpan)
            return BiomeGenBase::plains;
        BiomeGenBase *biome = biomes[z * kBiomeSpan + x];
        return biome != nullptr ? biome : BiomeGenBase::plains;
    }

    int_t getHeight() override { return WorldHeight::HEIGHT; }
    bool func_48452_a() override { return levelsEmpty; }
    WorldChunkManager *getWorldChunkManager() override { return nullptr; }
};

SwitchMeshJob::SwitchMeshJob() = default;
SwitchMeshJob::~SwitchMeshJob() = default;

namespace
{
// ---------------------------------------------------------------------------
// Which block ids may be meshed off-thread. Built once on the main thread.
// ---------------------------------------------------------------------------
std::vector<unsigned char> g_safeBlock;

void buildSafeBlockTable()
{
    if (!g_safeBlock.empty())
        return;
    g_safeBlock.assign(static_cast<std::size_t>(Block::BLOCK_REGISTRY_SIZE), 0);
    for (int id = 1; id < Block::BLOCK_REGISTRY_SIZE; ++id)
    {
        Block *block = Block::blocksList[id];
        if (block == nullptr || Block::isBlockContainer[id])
            continue;
        const int type = block->getRenderType();
        // Every RenderBlocks type except 22 (chests, drawn by their tile-entity
        // renderer). These renderers read the IBlockAccess and resize the block
        // they draw; the resizing lands in this thread's copy of the bounds
        // (BlockBound), so collision on the game thread never sees it.
        // Container blocks need their TileEntity and stay on the game thread.
        if (type < 0 || type > 27 || type == 22)
            continue;
        g_safeBlock[static_cast<std::size_t>(id)] = 1;
    }
}

// ---------------------------------------------------------------------------
// Worker
// ---------------------------------------------------------------------------
constexpr std::size_t kMaxInFlight = 3;

struct MesherQueue
{
    std::mutex mutex;
    std::condition_variable wake;
    std::deque<std::shared_ptr<SwitchMeshJob>> pending;
    std::deque<std::shared_ptr<SwitchMeshJob>> finished;
    std::size_t inFlight = 0; // pending + being meshed
    bool stop = false;
    bool started = false;
    PlatformThread thread;

    ~MesherQueue()
    {
        {
            std::lock_guard<std::mutex> lock(mutex);
            stop = true;
        }
        wake.notify_all();
        if (thread.joinable())
            thread.join();
    }
};

MesherQueue &queue()
{
    static MesherQueue instance;
    return instance;
}

void meshJob(SwitchMeshJob &job)
{
    // This thread's own temporary-vector pool (fluid flow directions).
    Vec3D::initialize();
    SwitchSectionSnapshot &snapshot = *job.snapshot;
    RenderBlocks renderBlocks(&snapshot);
    Tessellator *tessellator = &Tessellator::instance; // this thread's instance

    const int x0 = job.posX, y0 = job.posY, z0 = job.posZ;
    const int x1 = x0 + job.size, y1 = y0 + job.size, z1 = z0 + job.size;
    for (int pass = 0; pass < 2; ++pass)
    {
        bool hasOtherPass = false;
        bool drewAnything = false;
        bool open = false;
        for (int y = y0; y < y1; ++y)
        {
            for (int z = z0; z < z1; ++z)
            {
                for (int x = x0; x < x1; ++x)
                {
                    const int id = snapshot.getBlockId(x, y, z);
                    if (id <= 0)
                        continue;
                    Block *block = Block::blocksList[id];
                    if (block == nullptr)
                        continue;
                    if (!open)
                    {
                        open = true;
                        tessellator->startDrawingQuads();
                        tessellator->setTranslationD(-(double)x0, -(double)y0, -(double)z0);
                    }
                    if (block->getRenderBlockPass() != pass)
                    {
                        hasOtherPass = true;
                        continue;
                    }
                    drewAnything |= renderBlocks.renderBlockByRenderType(block, x, y, z);
                }
            }
        }
        if (open)
        {
            tessellator->captureTextureGroups(job.extraMeshes[pass]);
            tessellator->capture(job.passMesh[pass]);
            tessellator->setTranslationD(0.0, 0.0, 0.0);
        }
        job.drew[pass] = open && (drewAnything || !job.extraMeshes[pass].empty());
        if (!hasOtherPass)
            break;
    }
    job.isLit = snapshot.lit;
}

void *workerMain(void *)
{
    g_blockBoundsSlot = 1; // this thread resizes its own copy of block bounds
    MesherQueue &q = queue();
    for (;;)
    {
        std::shared_ptr<SwitchMeshJob> job;
        {
            std::unique_lock<std::mutex> lock(q.mutex);
            q.wake.wait(lock, [&q]() { return q.stop || !q.pending.empty(); });
            if (q.stop)
                break;
            job = q.pending.front();
            q.pending.pop_front();
        }
        if (!job->cancelled.load(std::memory_order_relaxed))
        {
            try
            {
                meshJob(*job);
            }
            catch (...)
            {
                job->failed = true;
            }
        }
        job->snapshot.reset(); // free the copy on this thread
        std::lock_guard<std::mutex> lock(q.mutex);
        q.finished.push_back(std::move(job));
    }
    return nullptr;
}

// Copies section data so the worker never reads live Chunk storage.
std::unique_ptr<ExtendedBlockStorage> copySection(const ExtendedBlockStorage &source)
{
    std::unique_ptr<NibbleArray> msb;
    if (const NibbleArray *sourceMsb = source.getBlockMSBArray())
        msb.reset(new NibbleArray(*sourceMsb));
    return std::unique_ptr<ExtendedBlockStorage>(new ExtendedBlockStorage(
        source.getYLocation(), source.func_48692_g(), std::move(msb),
        source.func_48697_j(), source.getBlocklightArray(), source.getSkylightArray()));
}

std::unique_ptr<SwitchSectionSnapshot> buildSnapshot(World *world, int posX, int posY, int posZ, int size)
{
    std::unique_ptr<SwitchSectionSnapshot> snapshot(new SwitchSectionSnapshot());
    SwitchSectionSnapshot &s = *snapshot;
    s.originChunkX = JavaArithmetic::intShr(posX - 1, 4);
    s.originChunkZ = JavaArithmetic::intShr(posZ - 1, 4);
    s.firstSection = (posY >> 4) - 1;
    s.skylightSubtracted = world->skylightSubtracted;
    s.hasNoSky = world->worldProvider != nullptr && world->worldProvider->hasNoSky;
    for (int i = 0; i < 16; ++i)
        s.brightnessTable[i] = world->worldProvider != nullptr ? world->worldProvider->lightBrightnessTable[i] : 1.0f;

    for (int cx = 0; cx < SwitchSectionSnapshot::kChunks; ++cx)
    {
        for (int cz = 0; cz < SwitchSectionSnapshot::kChunks; ++cz)
        {
            Chunk *chunk = world->getChunkIfExists(s.originChunkX + cx, s.originChunkZ + cz);
            if (chunk == nullptr)
                continue;
            if (!chunk->getAreLevelsEmpty(posY - 1, posY + size + 1))
                s.levelsEmpty = false;
            if (chunk->isEmptyChunk())
                continue; // reads as air, like ChunkCache's fast path
            SwitchSectionSnapshot::ChunkCopy &copy = s.chunks[cx][cz];
            copy.present = true;
            for (int sec = 0; sec < SwitchSectionSnapshot::kSections; ++sec)
            {
                const int sectionY = s.firstSection + sec;
                if (sectionY < 0 || sectionY >= WorldHeight::SECTION_COUNT)
                    continue;
                if (const ExtendedBlockStorage *storage = chunk->getBlockStorage(sectionY))
                    copy.sections[sec] = copySection(*storage);
            }
        }
    }

    s.biomeX0 = posX - 1;
    s.biomeZ0 = posZ - 1;
    for (int z = 0; z < SwitchSectionSnapshot::kBiomeSpan; ++z)
        for (int x = 0; x < SwitchSectionSnapshot::kBiomeSpan; ++x)
            s.biomes[z * SwitchSectionSnapshot::kBiomeSpan + x] = world->getBiomeGenForCoords(s.biomeX0 + x, s.biomeZ0 + z);
    return snapshot;
}

bool sectionIsSafe(SwitchSectionSnapshot &snapshot, int posX, int posY, int posZ, int size)
{
    for (int y = posY; y < posY + size; ++y)
        for (int z = posZ; z < posZ + size; ++z)
            for (int x = posX; x < posX + size; ++x)
            {
                const int id = snapshot.getBlockId(x, y, z);
                if (id > 0 && (id >= Block::BLOCK_REGISTRY_SIZE || !g_safeBlock[static_cast<std::size_t>(id)]))
                    return false;
            }
    return true;
}
}

std::shared_ptr<SwitchMeshJob> switchSubmitChunkMesh(World *world, WorldRenderer *renderer,
                                                     int posX, int posY, int posZ, int size)
{
    if (world == nullptr || renderer == nullptr || size != 16)
        return nullptr;
    buildSafeBlockTable();

    MesherQueue &q = queue();
    {
        std::lock_guard<std::mutex> lock(q.mutex);
        if (q.inFlight >= kMaxInFlight)
            return nullptr;
    }

    std::unique_ptr<SwitchSectionSnapshot> snapshot = buildSnapshot(world, posX, posY, posZ, size);
    if (!sectionIsSafe(*snapshot, posX, posY, posZ, size))
        return nullptr;

    std::shared_ptr<SwitchMeshJob> job = std::make_shared<SwitchMeshJob>();
    job->renderer = renderer;
    job->posX = posX;
    job->posY = posY;
    job->posZ = posZ;
    job->size = size;
    job->snapshot = std::move(snapshot);
    {
        std::lock_guard<std::mutex> lock(q.mutex);
        if (!q.started)
        {
            // Give the mesher copy of every block's bounds the values the
            // constructors set on this thread.
            for (int id = 1; id < Block::BLOCK_REGISTRY_SIZE; ++id)
                if (Block::blocksList[id] != nullptr)
                    Block::blocksList[id]->syncMesherBounds();
            // Normal priority: it sits on whichever core is idle, and the main
            // thread no longer waits for meshes, only publishes finished ones.
            q.started = q.thread.start(&workerMain, nullptr, 256 * 1024);
            if (!q.started)
                return nullptr;
        }
        q.pending.push_back(job);
        ++q.inFlight;
    }
    q.wake.notify_one();
    return job;
}

void switchPublishChunkMeshes(int maxJobs)
{
    MesherQueue &q = queue();
    for (int n = 0; n < maxJobs; ++n)
    {
        std::shared_ptr<SwitchMeshJob> job;
        {
            std::lock_guard<std::mutex> lock(q.mutex);
            if (q.finished.empty())
                return;
            job = q.finished.front();
            q.finished.pop_front();
            --q.inFlight;
        }
        if (job->cancelled.load(std::memory_order_relaxed) || job->renderer == nullptr)
            continue;
        job->renderer->switchPublishAsyncMesh(*job);
    }
}

#endif

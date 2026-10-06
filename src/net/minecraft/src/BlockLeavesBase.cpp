#include "BlockLeavesBase.h"
#include "platform/PlatformConfig.h"
#include "IBlockAccess.h"

BlockLeavesBase::BlockLeavesBase(int_t i, int_t j, Material *material, bool flag)
    : Block(i, j, material), graphicsLevel(flag)
{
}

bool BlockLeavesBase::isOpaqueCube()
{
    return false;
}

bool BlockLeavesBase::shouldSideBeRendered(IBlockAccess *iblockaccess, int_t i, int_t j, int_t k, int_t l)
{
    int_t i1 = iblockaccess->getBlockId(i, j, k);
    if (!graphicsLevel && i1 == blockID)
        return false;
#if PLATFORM_SWITCH
    // Fancy leaves draw the faces between two leaf blocks from both sides:
    // two coplanar quads with mirrored UVs. The Switch GPU resolves that
    // depth tie differently from pixel to pixel and frame to frame, so the
    // two leaf patterns flicker through each other. Keep only the face that
    // points in the negative direction (bottom/north/west); the neighbour's
    // face covers the same plane, so the canopy looks the same.
    if (graphicsLevel && i1 == blockID && (l == 1 || l == 3 || l == 5))
        return false;
#endif
    return Block::shouldSideBeRendered(iblockaccess, i, j, k, l);
}

#pragma once

#include "CoopNet.h"

// Engine-independent decisions used by the Cell adapter and its native tests.
namespace CoopVisual
{
    inline bool HasCellIndex(int index)
    {
        // SPORE pool handles can have their sign bit set. Only -1 is absent.
        return index != -1;
    }

    inline bool AppearanceMatchesPosition(const CoopNet::Snapshot& snapshot)
    {
        return snapshot.hasRemotePosition && snapshot.hasRemoteAppearance &&
            snapshot.remoteModelInstance == snapshot.remoteAppearanceModelInstance &&
            snapshot.remoteModelType == snapshot.remoteAppearanceModelType &&
            snapshot.remoteModelGroup == snapshot.remoteAppearanceModelGroup;
    }

    struct CellSize { float scale, target; };

    inline CellSize SharedSize(bool worldOwner, float localScale, float localTarget,
        const CoopNet::Snapshot& snapshot)
    {
        // Progress is shared separately. A joiner's old size must never grow
        // the owner's cell, including when the second window owns the world.
        return worldOwner ? CellSize{ localScale, localTarget }
            : CellSize{ snapshot.remoteScale, snapshot.remoteTargetSize };
    }

}

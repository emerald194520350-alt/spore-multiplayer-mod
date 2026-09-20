#pragma once
#include "CoopNet.h"

namespace CoopSession
{
    inline bool IsWorldOwner(const CoopNet::Snapshot& snapshot, const char* role)
    {
        return !snapshot.inviteFrom.empty() && snapshot.inviteFrom == role;
    }

    inline bool ShouldMirrorPause(const CoopNet::Snapshot& snapshot,
        const char* role, bool inWorld)
    {
        return snapshot.connected && snapshot.inviteAccepted &&
            snapshot.remotePeerConnected && snapshot.hostPaused && inWorld &&
            !snapshot.inviteFrom.empty() && !IsWorldOwner(snapshot, role);
    }

    // Own exactly one pause increment. Release it before resetting connection
    // state; never resume tutorial, menu or another mod's pause increments.
    struct PauseLease
    {
        bool applied = false;
        template<typename Pause, typename Resume>
        void Set(bool desired, Pause pause, Resume resume)
        {
            if (desired == applied) return;
            if (desired) pause(); else resume();
            applied = desired;
        }
    };
}

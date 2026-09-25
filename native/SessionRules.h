#pragma once
#include "CoopNet.h"

namespace CoopSession
{
    inline bool IsWorldOwner(const CoopNet::Snapshot& snapshot, const char* role)
    {
        return !snapshot.inviteFrom.empty() && snapshot.inviteFrom == role;
    }

    // Losing TCP does not turn the invited campaign into a local save. Release
    // this lease only after leaving that world, or selecting a new owner.
    struct WorldSaveLease
    {
        bool blocked = false;
        void Observe(const CoopNet::Snapshot& state, const char* role, bool leftWorld)
        {
            if (state.connected && state.inviteAccepted && !state.inviteFrom.empty())
                blocked = !IsWorldOwner(state, role);
            else if (leftWorld) blocked = false;
        }
    };

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

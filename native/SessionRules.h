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

    // Keep the participant role after networking clears the invitation on EOF.
    // Editor and loading transitions are part of the same campaign, not exits.
    struct WorldLifecycle
    {
        std::uint64_t world=0, connection=0;
        bool guest=false, ownerInWorld=false, leaveSent=false, exitPending=false;
        void Observe(const CoopNet::Snapshot& state,const char* role,bool inWorld)
        {
            if (state.connected && state.inviteAccepted)
            {
                if (world!=state.worldGeneration || connection!=state.connectionGeneration)
                {
                    *this=WorldLifecycle{};
                    world=state.worldGeneration; connection=state.connectionGeneration;
                }
                guest=!IsWorldOwner(state,role);
                if (!guest && inWorld) ownerInWorld=true;
            }
            if (guest && state.sessionEnded) exitPending=true;
        }
        bool ShouldAnnounceExit(const CoopNet::Snapshot& state,bool inMenu) const
        { return state.connected && state.inviteAccepted && ownerInWorld && !guest && !leaveSent && inMenu; }
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

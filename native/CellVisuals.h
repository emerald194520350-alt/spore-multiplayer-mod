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

    // A native save can normalize resource bytes and assign a profile-local
    // key. Remember the completed remote editor transaction instead of treating
    // that saved avatar as a foreign creation solely because its bytes differ.
    class SavedEditorAppearance
    {
        std::uint64_t world=0, peer=0, session=0, sequence=0;
        std::uint32_t instance=0, type=0, group=0;
        std::string authoritativeBlob;
    public:
        void Reset() { *this=SavedEditorAppearance{}; }

        template<class Key>
        bool Remember(const CoopNet::Snapshot& state, std::uint64_t appliedSequence, const Key& key)
        {
            Reset();
            if (!state.inviteAccepted || state.editorOpen || !state.editorFinished ||
                !state.editorSession || state.speciesBlob.empty() ||
                state.speciesSequence!=appliedSequence || !key.instanceID) return false;
            world=state.worldGeneration; peer=state.remotePeerGeneration;
            session=state.editorSession; sequence=state.speciesSequence;
            instance=key.instanceID; type=key.typeID; group=key.groupID;
            authoritativeBlob=state.speciesBlob;
            return true;
        }

        template<class Key>
        const std::string* Canonical(const CoopNet::Snapshot& state, const Key& avatarKey) const
        {
            const bool current=instance && state.inviteAccepted && !state.editorOpen && state.editorFinished &&
                state.worldGeneration==world && state.remotePeerGeneration==peer &&
                state.editorSession==session && state.speciesSequence==sequence &&
                avatarKey.instanceID==instance && avatarKey.typeID==type && avatarKey.groupID==group &&
                state.speciesBlob==authoritativeBlob;
            return current ? &authoritativeBlob : nullptr;
        }

        template<class Key>
        bool Matches(const CoopNet::Snapshot& state, const Key& avatarKey) const
        {
            const auto canonical=Canonical(state,avatarKey);
            return canonical && AppearanceMatchesPosition(state) && state.remoteAppearanceBlob==*canonical;
        }
    };

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

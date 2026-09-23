#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace CoopNet
{
    struct CellPose
    {
        float x = 0, y = 0, z = 0;
        float qx = 0, qy = 0, qz = 0, qw = 1;
        float scale = 1;
        std::uint32_t animation = 0xAAAA0015;
        bool visible = true;
    };
    struct CellProgress
    {
        int food = 0;
        int plantFood = 0;
        int overPlantFood = 0;
        int overAnimalFood = 0;
        int spent = 0;
        std::array<int, 13> unlocks{};
        std::array<int, 24> missions{};
        int killCount = 0;
        bool playerHasMoved = false;
        bool playerHasEaten = false;
        bool partCinematicPlayed = false;
        bool showMateButton = false;
        bool firstEditorEntry = false;
    };

    struct NpcState
    {
        std::uint32_t id = 0;
        std::uint32_t cellResource = 0;
        float x = 0, y = 0, z = 0;
        float qz = 0, qw = 1;
        float scale = 1;
        float targetSize = 1;
        float opacity = 1;
        int stageScale = 0;
        std::uint32_t modelInstance = 0, modelType = 0, modelGroup = 0;
        int health = 6;
        std::uint32_t animation = 0;
        bool dead = false;
        float elevation = 0;
    };

    struct WorldAction
    {
        std::uint64_t sequence = 0;
        std::uint32_t id = 0, resource = 0;
        int damage = 0;
        bool removed = false, effects = false;
    };

    struct Snapshot
    {
        bool enabled = false;
        bool connected = false;
        std::uint64_t connectionGeneration = 0;
        std::string lastError;
        bool sessionEnded = false;
        std::string disconnectReason;
        // A TCP connection alone is not permission to spawn a player clone.
        // This is set only when the session-state snapshot contains the other role.
        bool remotePeerConnected = false;
        std::uint64_t remotePeerGeneration = 0;
        bool hasRemotePosition = false;
        float remoteX = 0.0f;
        float remoteY = 0.0f;
        float remoteZ = 0.0f;
        std::uint64_t remotePositionSequence = 0;
        std::uint64_t remotePositionReceivedTick = 0;
        bool hasRemoteAppearance = false;
        std::uint32_t remoteModelInstance = 0;
        std::uint32_t remoteModelType = 0;
        std::uint32_t remoteModelGroup = 0;
        std::uint32_t remoteCellResource = 0;
        float remoteScale = 1.0f;
        float remoteTargetSize = 1.0f;
        float remoteOpacity = 1.0f;
        CellPose remotePose;
        std::uint64_t remoteAppearanceSequence = 0;
        std::uint32_t remoteAppearanceModelInstance = 0;
        std::uint32_t remoteAppearanceModelType = 0;
        std::uint32_t remoteAppearanceModelGroup = 0;
        std::string remoteAppearanceBlob;
        std::uint64_t npcSequence = 0;
        std::uint64_t npcReceivedTick = 0;
        std::vector<NpcState> remoteNpcs;
        std::uint64_t worldActionAck = 0;

        bool invitePending = false;
        bool inviteAccepted = false;
        // Role that owns the saved world behind the current invitation.
        std::string inviteFrom;
        std::uint64_t worldGeneration = 0;
        // The host alone controls the shared simulation pause.
        bool hostPaused = false;

        bool progressInitialized = false;
        std::uint64_t progressAckSequence = 0;
        std::uint64_t revision = 0;
        CellProgress progress;

        bool editorOpen = false;
        std::uint32_t editorID = 0;
        std::string editorRole;
        std::uint64_t speciesSequence = 0;
        std::uint64_t speciesAck = 0;
        bool speciesConflict = false;
        std::string speciesBlob;
        int editorBudget = -1;
    };

    bool StartFromEnvironment();
    void Stop();
    Snapshot GetSnapshot();
    void AcknowledgeSessionEnd();
    const char* GetRole();

    void SubmitPosition(float x, float y, float z,
        std::uint32_t modelInstance, std::uint32_t modelType,
        std::uint32_t modelGroup, std::uint32_t cellResource,
        float scale, float targetSize, float opacity, const CellPose* pose = nullptr);
    void SubmitAppearance(std::uint32_t modelInstance, std::uint32_t modelType,
        std::uint32_t modelGroup, const std::string& appearanceBlob);
    void SubmitInvite();
    void SubmitInviteResponse(bool accepted);
    void SubmitHostPause(bool paused);
    void SubmitNpcSnapshot(const std::vector<NpcState>& npcs, std::uint64_t actionAck = 0);
    std::uint64_t SubmitWorldAction(WorldAction action);
    std::vector<WorldAction> TakeWorldActions();
    void SeedProgress(const CellProgress& progress);
    void SubmitProgressDelta(std::uint64_t sequence, const CellProgress& delta,
        const std::array<int, 13>& absoluteUnlocks);
    void SubmitEditorOpen(std::uint32_t editorID, const std::string& initialSpecies, int budget);
    void SubmitEditorClose(const std::string& speciesBlob, int budget);
    std::uint64_t SubmitSpecies(const std::string& speciesBlob, std::uint64_t baseSequence, int budget);
}

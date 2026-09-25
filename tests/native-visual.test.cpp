// Run the actual native message parser without starting a game or socket thread.
#include "../native/CoopNet.cpp"
#include "../native/CellVisuals.h"
#include "../native/SessionRules.h"
#include "../native/ProgressSync.h"
#include "../native/CellEngineAbi.h"
#include "../native/UiGraphicsAbi.h"
#include <stdexcept>
#include <iostream>
#include "engine-motion.test.h"
#include "shared-world.test.h"

namespace
{
    int checks = 0;

    // Matches the native Graphics2D vtable, including its scalar EAX return.
    // Using the SDK's Math::Color return here would add a hidden stack argument.
    class NativeGraphicsFixture
    {
    public:
        std::uint32_t color = 0xA1B2C3D4;
        virtual ~NativeGraphicsFixture() = default;
        virtual void SetColor(std::uint32_t value) { color = value; }
        virtual std::uint32_t GetColor() { return color; }
    };
    void Check(bool condition, const char* message)
    {
        if (!condition) throw std::runtime_error(message);
        ++checks;
    }

    void Appearance(unsigned sequence, unsigned instance, const char* blob = "U0NQMQ==")
    {
        HandleMessage("{\"type\":\"appearance\",\"role\":\"guest\",\"sequence\":" +
            std::to_string(sequence) + ",\"modelInstance\":" + std::to_string(instance) +
            ",\"modelType\":731479110,\"modelGroup\":0,\"appearance\":\"" + blob + "\"}");
    }

    void Position(unsigned instance, unsigned group = 0)
    {
        static unsigned sequence = 0;
        HandleMessage("{\"type\":\"position\",\"role\":\"guest\",\"sequence\":" +
            std::to_string(sequence++) + ","
            "\"position\":[1,2,3],\"modelInstance\":" + std::to_string(instance) +
            ",\"modelType\":731479110,\"modelGroup\":" + std::to_string(group) +
            ",\"cellResource\":456,\"scale\":0.55,\"targetSize\":0.75,\"opacity\":0.8}");
    }
}

int main(int argc, char** argv)
{
    try
    {
        if (argc == 3 && std::string(argv[1]) == "--engine-motion")
        {
            std::cout << "PASS: " << TestEngineMotion(argv[2])
                << " engine movement/collision checks, including 1000 frames using native setters. No game session was started.\n";
            return 0;
        }
        if (argc == 2 && std::string(argv[1]) == "--idle")
        {
            Check(CoopNet::StartFromEnvironment(), "Native client starts");
            const auto deadline = GetTickCount64() + 5000;
            while (!CoopNet::GetSnapshot().connected && GetTickCount64() < deadline) Sleep(20);
            const auto before = CoopNet::GetSnapshot();
            Check(before.connected, "Native client completed server handshake");
            // Actual server times out after 15 seconds without network input.
            // Do not submit gameplay packets or call a game update function.
            Sleep(17000);
            const auto after = CoopNet::GetSnapshot();
            CoopNet::Stop();
            Check(after.connected && after.connectionGeneration == before.connectionGeneration,
                "Idle menus survive the real server timeout without reconnecting");
            std::cout << "PASS: native client stayed connected for 17 seconds without gameplay.\n";
            return 0;
        }
        Check(TestSharedWorld(),"Shared world coordinates, editor rebase and mission increments");
        TestCampaignData();
        Check(true,"World-save ownership and bounded native history data");
        gRole="guest";gSnapshot=CoopNet::Snapshot{};
        gSnapshot.connected=gSnapshot.inviteAccepted=true;
        gSnapshot.worldGeneration=9;gSnapshot.inviteFrom="host";
        HandleMessage(R"({"type":"history","role":"host","worldGeneration":9,"sequence":1,"history":"snapshot"})");
        Check(gSnapshot.historySequence==1 && gSnapshot.historyBlob=="snapshot","History owner accepted atomically");
        HandleMessage(R"({"type":"history","role":"guest","worldGeneration":9,"sequence":2,"history":"wrong-owner"})");
        Check(gSnapshot.historySequence==1,"Guest cannot replace authoritative history");
        HandleMessage(R"({"type":"history","role":"host","worldGeneration":8,"sequence":2,"history":"wrong-world"})");
        Check(gSnapshot.historySequence==1,"Old campaign history rejected");
        HandleMessage(R"({"type":"history","role":"host","worldGeneration":9,"sequence":1,"history":"stale"})");
        Check(gSnapshot.historyBlob=="snapshot","Duplicate history does not overwrite latest data");
        MarkDisconnected();
        Check(gSnapshot.historyBlob.empty() && !gSnapshot.historySequence,"Disconnect clears remote history");
        gSnapshot=CoopNet::Snapshot{};
        TestPeerIndicator();
        Check(true,"Off-screen indicator covers all edges, corners, aspect ratios and invalid inputs");
        {
            CoopVisual::SavedEditorAppearance savedAppearance;
            struct Key { std::uint32_t instanceID,typeID,groupID; } localKey{222,0x2b978c46,0};
            CoopNet::Snapshot completed;
            completed.inviteAccepted=true; completed.editorFinished=true;
            completed.worldGeneration=2; completed.remotePeerGeneration=3;
            completed.editorSession=4; completed.speciesSequence=7;
            completed.speciesBlob="authoritative-final-body-and-paint";
            completed.hasRemotePosition=completed.hasRemoteAppearance=true;
            completed.remoteModelInstance=completed.remoteAppearanceModelInstance=111;
            completed.remoteModelType=completed.remoteAppearanceModelType=localKey.typeID;
            completed.remoteAppearanceBlob=completed.speciesBlob;
            Check(!savedAppearance.Matches(completed,localKey),"Appearance reuse requires a completed native save");
            Check(savedAppearance.Remember(completed,7,localKey) && savedAppearance.Matches(completed,localKey),
                "Shared final body uses the saved local key despite profile-local resource normalization");
            Check(savedAppearance.Canonical(completed,localKey) &&
                *savedAppearance.Canonical(completed,localKey)==completed.speciesBlob,
                "Mirrored save republishes the authoritative body so the initiating player also recognizes it");
            auto changed=completed; changed.remoteAppearanceBlob="different-color-or-parts";
            Check(!savedAppearance.Matches(changed,localKey),"Different incoming paint/body cannot alias the saved avatar");
            changed=completed; changed.speciesBlob="different-final-body";
            Check(!savedAppearance.Matches(changed,localKey),"Changed final model invalidates the saved binding");
            changed=completed; ++changed.worldGeneration;
            Check(!savedAppearance.Matches(changed,localKey),"Another world cannot reuse the old editor binding");
            changed=completed; ++changed.remotePeerGeneration;
            Check(!savedAppearance.Matches(changed,localKey),"Reconnected peer cannot reuse the old editor binding");
            changed=completed; ++changed.editorSession;
            Check(!savedAppearance.Matches(changed,localKey),"New editor session cannot reuse the old binding");
            changed=completed; ++changed.speciesSequence;
            Check(!savedAppearance.Matches(changed,localKey),"New model revision cannot reuse the old binding");
            changed=completed; changed.editorOpen=true;
            Check(!savedAppearance.Matches(changed,localKey),"Active editor cannot reuse a previous completion");
            changed=completed; changed.editorFinished=false;
            Check(!savedAppearance.Matches(changed,localKey),"Cancelled editor cannot reuse a completed appearance");
            changed=completed; changed.inviteAccepted=false;
            Check(!savedAppearance.Matches(changed,localKey),"Ended invitation invalidates the binding");
            changed=completed; ++changed.remoteModelInstance;
            Check(!savedAppearance.Matches(changed,localKey),"Unpaired position/appearance packets cannot reuse the saved avatar");
            Check(!savedAppearance.Matches(completed,Key{333,localKey.typeID,0}) &&
                !savedAppearance.Matches(completed,Key{222,localKey.typeID,1}),
                "Changed local avatar key or group requires a fresh appearance decision");
            Check(!savedAppearance.Remember(completed,6,localKey) && !savedAppearance.Matches(completed,localKey),
                "Unapplied final revision cannot be bound to a native save");
            changed=completed; changed.editorFinished=false;
            Check(!savedAppearance.Remember(changed,7,localKey),"Cancellation must not establish a binding");
            Check(!savedAppearance.Remember(completed,7,Key{}),"Missing native save key must not establish a binding");
            savedAppearance.Remember(completed,7,localKey); savedAppearance.Reset();
            Check(!savedAppearance.Matches(completed,localKey),"Reset clears the completed appearance");
            Check(!savedAppearance.Canonical(completed,localKey),"Reset also stops publishing the previous shared body");
        }
        NativeGraphicsFixture graphics;
        unsigned char removeCode[CoopEngine::kRemoveCellCodeSize]{};
        const unsigned char removePrologue[] = {0x8b,0x0d,0x04,0x3c,0x6b,0x01,0x83,0xec,0x10,0x55,0x8b,0x6c,0x24,0x18,0x83,0xc1,0x1c,0x55,0xe8,0x29,0xa2,0xcf,0xff};
        const unsigned char removeTail[] = {0x8b,0x0d,0x04,0x3c,0x6b,0x01,0x55,0x83,0xc1,0x1c,0xe8,0x4f,0xa1,0xcf,0xff,0x5e,0x5d,0x83,0xc4,0x10,0xc3};
        std::memcpy(removeCode, removePrologue, sizeof(removePrologue));
        std::memcpy(removeCode+0x172, removeTail, sizeof(removeTail));
        Check(CoopEngine::MatchesRemovalAbi(removeCode, sizeof(removeCode), 0xe780a0, 0x16b3c04, 0xb722e0, 0xb72370),
            "Observed native cell removal ABI matches the cdecl cleanup function");
        Check(!CoopEngine::MatchesRemovalAbi(removeCode, sizeof(removeCode)-1, 0xe780a0, 0x16b3c04, 0xb722e0, 0xb72370),
            "Truncated executable code must not enable a native call");
        Check(!CoopEngine::MatchesRemovalAbi(removeCode, sizeof(removeCode), 0xe780a0, 0x16b3c04, 0xb722e0, 0xb72371),
            "Different engine cleanup targets must fail the version guard");
        removeCode[0x186] = 0xc2;
        Check(!CoopEngine::MatchesRemovalAbi(removeCode, sizeof(removeCode), 0xe780a0, 0x16b3c04, 0xb722e0, 0xb72370),
            "A different stack cleanup convention must never be called");
        auto& nativeGraphics = *reinterpret_cast<UTFWin::Graphics2D*>(&graphics);
        Check(CoopUi::ReadGraphicsColor(nativeGraphics) == 0xA1B2C3D4,
            "UI color reader preserves all ARGB bytes through the native scalar ABI");
        std::uint32_t colorChecksum = 0;
        for (std::uint32_t i = 0; i < 1024; ++i)
        {
            graphics.SetColor(0xFF000000 | i);
            colorChecksum += CoopUi::ReadGraphicsColor(nativeGraphics);
        }
        Check(colorChecksum == 0x0007FE00,
            "Repeated native color calls leave the stack and loop registers intact");

        // Both players gain food from the same shared baseline before either
        // acknowledgement arrives. Neither client's own pending event may be
        // erased by, or counted again after, the intervening server snapshot.
        CoopProgress::Reconciler firstProgress, secondProgress;
        CoopNet::CellProgress seed;
        seed.food = 10; seed.spent = 3;
        firstProgress.Initialize(seed); secondProgress.Initialize(seed);
        auto a = seed, b = seed;
        a.food++; a.unlocks[3] = 1;
        b.food++; b.unlocks[5] = 1;
        CoopProgress::Event ea, eb;
        Check(firstProgress.Capture(a, ea) && secondProgress.Capture(b, eb) &&
            ea.delta.food == 1 && eb.delta.food == 1,
            "Concurrent food becomes two independent increments");
        auto older = secondProgress.Reconcile(seed, 0);
        Check(older.food == 11 && older.unlocks[5] == 1,
            "Older server snapshots preserve unacknowledged food and parts");
        CoopProgress::Event echo;
        Check(!secondProgress.Capture(older, echo), "Reconciliation is never republished as local progress");
        auto serverProgress = seed;
        serverProgress.food = 11; serverProgress.unlocks[3] = 1;
        auto waiting = secondProgress.Reconcile(serverProgress, 0);
        Check(waiting.food == 12 && waiting.unlocks[3] == 1 && waiting.unlocks[5] == 1,
            "The other player's event merges with pending local gains");
        serverProgress.food = 12; serverProgress.unlocks[5] = 1;
        auto confirmed = secondProgress.Reconcile(serverProgress, eb.sequence);
        Check(confirmed.food == 12 && !secondProgress.Capture(confirmed, echo),
            "Acknowledgement removes pending increments exactly once");
        Check(secondProgress.Reconcile(serverProgress, eb.sequence).food == 12,
            "Duplicate acknowledgements do not award food twice");
        confirmed.spent--;
        Check(secondProgress.Capture(confirmed, echo) && echo.delta.spent == -1,
            "Editor refunds stay signed increments");
        Check(secondProgress.Reconcile(serverProgress, eb.sequence).spent == 2,
            "Unacknowledged editor refunds survive a stale snapshot");
        secondProgress.Reset();
        Check(!secondProgress.IsInitialized(), "Another world cannot inherit pending inventory events");
        secondProgress.Initialize(seed);
        Check(secondProgress.Capture(b, eb) && eb.sequence == 1,
            "A new invitation restarts its progress sequence");

        gOutgoing.clear();
        CoopNet::SubmitProgressDelta(1, ea.delta, a.unlocks);
        for (int i = 0; i < 1000; ++i)
            CoopNet::SubmitPosition(float(i), 2, 3, 123, 731479110, 0, 456, 1, 1, 1);
        Check(gOutgoing.size() == 2 && gOutgoing.front().find("progressDelta") != std::string::npos,
            "Movement congestion preserves reliable inventory events");
        std::vector<double> latestPosition;
        Check(ReadNumberArray(gOutgoing.back(), "position", latestPosition, 3) && latestPosition[0] == 999,
            "Congestion retains the latest position rather than stale movement");
        gOutgoing.clear();

        CoopNet::Snapshot pauseState;
        pauseState.connected = pauseState.inviteAccepted = pauseState.remotePeerConnected = true;
        pauseState.hostPaused = true;
        pauseState.inviteFrom = "guest";
        Check(CoopSession::ShouldMirrorPause(pauseState, "host", true),
            "The first window obeys a world created by the second window");
        Check(!CoopSession::ShouldMirrorPause(pauseState, "guest", true), "Owner never mirrors itself");
        pauseState.inviteFrom = "host";
        Check(CoopSession::ShouldMirrorPause(pauseState, "guest", true), "Normal invitation pause authority");
        Check(!CoopSession::ShouldMirrorPause(pauseState, "guest", false), "Menus do not acquire gameplay pauses");
        pauseState.connected = false;
        Check(!CoopSession::ShouldMirrorPause(pauseState, "guest", true), "Disconnect releases owner pause");
        CoopSession::PauseLease lease;
        int totalPauses = 2; // Tutorial and the user's own menu already hold pauses.
        auto pause = [&] { ++totalPauses; };
        auto resume = [&] { --totalPauses; };
        lease.Set(true, pause, resume);
        lease.Set(true, pause, resume);
        Check(totalPauses == 3, "Repeated snapshots acquire exactly one pause");
        lease.Set(false, pause, resume); // Also run for a fresh connection generation.
        lease.Set(false, pause, resume);
        Check(totalPauses == 2 && !lease.applied, "Reconnect releases only the mod's own pause");
        lease.Set(true, pause, resume);
        lease.Set(false, pause, resume);
        Check(totalPauses == 2, "Repeated reconnect cycles remain balanced");
        Check(CoopVisual::HasCellIndex(-2120220322), "Observed negative SPORE handle must be valid");
        Check(CoopVisual::HasCellIndex(0), "Zero handle is valid");
        Check(CoopVisual::HasCellIndex(42), "Positive handle is valid");
        Check(!CoopVisual::HasCellIndex(-1), "Only the absent-handle sentinel is invalid");

        CoopNet::Snapshot sizeState;
        sizeState.remoteScale = 100.0f;
        sizeState.remoteTargetSize = 120.0f;
        auto size = CoopVisual::SharedSize(true, 0.55f, 0.75f, sizeState);
        Check(size.scale == 0.55f && size.target == 0.75f,
            "A joiner's old growth must not enlarge the world owner");
        sizeState.remoteScale = 0.55f;
        sizeState.remoteTargetSize = 0.75f;
        size = CoopVisual::SharedSize(false, 100.0f, 120.0f, sizeState);
        Check(size.scale == 0.55f && size.target == 0.75f,
            "A larger joiner must shrink to the owner's size");
        size = CoopVisual::SharedSize(false, 0.1f, 0.2f, sizeState);
        Check(size.scale == 0.55f && size.target == 0.75f,
            "A smaller joiner must grow to the owner's size");

        CoopNet::CellPose localPose;
        localPose.x = 4.0f; localPose.y = 5.0f; localPose.z = 6.0f;
        localPose.qz = 0.7071067f; localPose.qw = 0.7071067f;
        localPose.scale = 0.55f;
        localPose.animation = 0xAAAA0015;
        CoopNet::SubmitPosition(1, 2, 3, 123, 731479110, 0, 456,
            0.55f, 0.75f, 0.8f, &localPose);
        double sentOpacity = 0;
        Check(ReadNumber(gOutgoing.back(), "opacity", sentOpacity) && sentOpacity == 0.8,
            "Position packet retains the game's visible opacity");
        std::vector<double> sentRenderPosition, sentOrientation;
        Check(ReadNumberArray(gOutgoing.back(), "renderPosition", sentRenderPosition, 3) &&
            sentRenderPosition[0] == 4 && sentRenderPosition[1] == 5 && sentRenderPosition[2] == 6,
            "Position packet includes the rendered model location");
        Check(ReadNumberArray(gOutgoing.back(), "orientation", sentOrientation, 4) &&
            sentOrientation[2] > 0.707 && sentOrientation[3] > 0.707,
            "Position packet includes the rendered model orientation");
        Check(gOutgoing.back().back() == '}', "Extended position packet remains valid JSON");

        CoopNet::NpcState sentNpc;
        sentNpc.id = 0x80000001u;
        sentNpc.cellResource = 123456;
        sentNpc.modelInstance = 0xfedcba98;
        sentNpc.modelType = 731479110;
        sentNpc.x = 12; sentNpc.y = 34;
        sentNpc.scale = sentNpc.targetSize = 0.55f;
        CoopNet::SubmitNpcSnapshot({ sentNpc });
        Check(gOutgoing.back().find("\"type\":\"npcSnapshot\"") != std::string::npos &&
            gOutgoing.back().find("2147483649,123456,12.000000,34.000000") != std::string::npos,
            "NPC publisher serializes signed-looking pool handles as stable unsigned IDs");

        gRole = "host";
        gSnapshot = CoopNet::Snapshot{};
        HandleMessage("{\"type\":\"state\",\"players\":{\"host\":{},\"guest\":{}}}");
        const auto generation = gSnapshot.remotePeerGeneration;
        Position(123);
        Check(!gSnapshot.hasRemoteAppearance && !CoopVisual::AppearanceMatchesPosition(gSnapshot),
            "Movement alone is not a complete creature appearance");
        Appearance(0, 123);
        Check(CoopVisual::AppearanceMatchesPosition(gSnapshot), "First appearance sequence zero is accepted");
        Position(789);
        Check(gSnapshot.remoteAppearanceModelInstance == 123 &&
            !CoopVisual::AppearanceMatchesPosition(gSnapshot),
            "Movement for an edited model cannot relabel the old appearance blob");
        Appearance(1, 789);
        Check(CoopVisual::AppearanceMatchesPosition(gSnapshot), "Matching edited appearance becomes ready");
        Appearance(0, 123);
        Appearance(1, 123, "b2xk");
        Check(gSnapshot.remoteAppearanceModelInstance == 789 && gSnapshot.remoteAppearanceBlob == "U0NQMQ==",
            "Stale and duplicate appearance packets cannot replace a newer model");
        Position(789, 7);
        Check(!CoopVisual::AppearanceMatchesPosition(gSnapshot), "Matching includes the resource group");
        HandleMessage("{\"type\":\"state\",\"players\":{\"host\":{}}}");
        Check(!gSnapshot.hasRemoteAppearance && gSnapshot.remoteAppearanceModelInstance == 0,
            "Peer departure clears the complete appearance identity");
        HandleMessage("{\"type\":\"state\",\"players\":{\"host\":{},\"guest\":{}}}");
        Check(gSnapshot.remotePeerGeneration == generation + 1,
            "Peer replacement resets the adapter even if its own TCP connection survives");
        Appearance(0, 456);
        Check(!gSnapshot.hasRemotePosition && gSnapshot.remoteModelInstance == 0,
            "Replayed appearance must not invent movement or replace position identity");
        Position(456);
        Check(CoopVisual::AppearanceMatchesPosition(gSnapshot), "Reconnected peer can start appearance numbering again");
        HandleMessage("{\"type\":\"state\",\"revision\":8,\"progressInitialized\":true,"
            "\"hostProgressSequence\":3,\"guestProgressSequence\":7,\"worldGeneration\":42,\"food\":12}");
        Check(gSnapshot.progressAckSequence == 3 && gSnapshot.revision == 8 && gSnapshot.progress.food == 12 && gSnapshot.worldGeneration == 42,
            "Native snapshots publish progress and the local role's acknowledgement together");
        HandleMessage("{\"type\":\"npcSnapshot\",\"role\":\"guest\",\"sequence\":0,"
            "\"npcs\":[2147483649,123456,12,34,0,0,1,0.55,0.55,1,0,4275878552,731479110,0,6,0,0,0]}");
        Check(gSnapshot.npcReceivedTick != 0 && gSnapshot.npcSequence == 0 &&
            gSnapshot.remoteNpcs.size() == 1 &&
            gSnapshot.remoteNpcs[0].id == 0x80000001u &&
            gSnapshot.remoteNpcs[0].cellResource == 123456 &&
            gSnapshot.remoteNpcs[0].modelInstance == 0xfedcba98,
            "NPC receiver preserves the world owner's creature identity and render state");
        HandleMessage("{\"type\":\"npcSnapshot\",\"role\":\"guest\",\"sequence\":1,"
            "\"npcs\":[1,2,3]}");
        Check(gSnapshot.npcSequence == 0 && gSnapshot.remoteNpcs.size() == 1,
            "Malformed NPC frames cannot erase the last valid authoritative population");
        gSnapshot.hostPaused = gSnapshot.inviteAccepted = gSnapshot.editorOpen = true;
        Queue("{\"type\":\"hostPause\",\"paused\":true}");
        MarkDisconnected();
        Check(!gSnapshot.hasRemoteAppearance && !gSnapshot.hasRemotePosition &&
            gSnapshot.remoteNpcs.empty() && gSnapshot.npcReceivedTick == 0 &&
            !gSnapshot.inviteAccepted && !gSnapshot.hostPaused && !gSnapshot.editorOpen && gOutgoing.empty(),
            "Disconnect clears peer and NPC visuals, editor, invite, pause and queued actions");
        gSnapshot.hostPaused = true;
        HandleMessage("{\"type\":\"welcome\",\"protocol\":4,\"role\":\"host\"}");
        Check(!gSnapshot.hostPaused && !gSnapshot.inviteAccepted,
            "A fresh handshake cannot retain the previous world's pause");
        HandleMessage("{\"type\":\"state\",\"worldGeneration\":50,\"speciesSequence\":20,\"species\":\"old-campaign\"}");
        gSnapshot.speciesAck = 200;
        gSnapshot.speciesConflict = true;
        HandleMessage("{\"type\":\"state\",\"worldGeneration\":51,\"speciesSequence\":0,\"species\":\"\"}");
        Check(gSnapshot.speciesSequence == 0 && gSnapshot.speciesBlob.empty() &&
            gSnapshot.speciesAck == 0 && !gSnapshot.speciesConflict,
            "A new invitation resets editor revisions and acknowledgements from the previous campaign");
        HandleMessage("{\"type\":\"state\",\"worldGeneration\":51,\"evolving\":true,\"speciesSequence\":1,\"species\":\"shared-entry\"}");
        Check(gSnapshot.speciesSequence == 1 && gSnapshot.speciesBlob == "shared-entry",
            "The new editor's initial body is accepted even after a higher revision in a previous campaign");
        HandleMessage("{\"type\":\"speciesLive\",\"role\":\"host\",\"sequence\":2,\"editorBudget\":6,\"clientSequence\":201,\"species\":\"body-with-spikes\"}");
        Check(gSnapshot.speciesSequence == 2 && gSnapshot.speciesBlob == "body-with-spikes" &&
            gSnapshot.speciesAck == 201 && !gSnapshot.speciesConflict,
            "A committed spike edit advances the shared revision and acknowledges the local transaction");
        Check(gSnapshot.editorBudget==6,"The model and native DNA balance arrive atomically");
        HandleMessage("{\"type\":\"speciesLive\",\"role\":\"guest\",\"sequence\":3,\"species\":\"bad\",\"editorBudget\":-4}");
        Check(gSnapshot.speciesSequence==2 && gSnapshot.editorBudget==6,"Invalid budget cannot advance the model revision");
        HandleMessage("{\"type\":\"speciesLive\",\"role\":\"guest\",\"sequence\":3,\"species\":\"bad\"}");
        Check(gSnapshot.speciesSequence==2,"Model packets without their budget are rejected");
        HandleMessage("{\"type\":\"state\",\"worldGeneration\":51,\"speciesSequence\":1,\"species\":\"shared-entry\"}");
        Check(gSnapshot.speciesSequence == 2 && gSnapshot.speciesBlob == "body-with-spikes",
            "An older snapshot in the same campaign cannot undo a newer live edit");
        Check(gSnapshot.editorBudget==6,"Older full snapshots cannot overwrite a newer budget");
        HandleMessage(R"({"type":"speciesLive","role":"guest","sequence":3,"species":"body-with-spikes","editorBudget":6,"speciesName":"KAQ4BD8E","editorSession":7})");
        Check(gSnapshot.speciesName=="KAQ4BD8E" && gSnapshot.editorSession==7 && !gSnapshot.editorFinished,
            "Native client keeps the shared name and editor visit with the model");
        HandleMessage(R"({"type":"editorClosed","role":"guest","sequence":4,"species":"final","editorBudget":6,"speciesName":"KAQ4BD8E","editorSession":7,"editorFinished":true})");
        Check(!gSnapshot.editorOpen && gSnapshot.editorFinished && gSnapshot.speciesBlob=="final" && gSnapshot.speciesName=="KAQ4BD8E",
            "Final creature, name and acceptance become visible atomically");
        HandleMessage(R"({"type":"state","worldGeneration":51,"evolving":true,"speciesSequence":5,"species":"new","editorBudget":6,"speciesName":"","editorSession":8,"editorFinished":false})");
        HandleMessage(R"({"type":"editorClosed","role":"guest","sequence":4,"species":"final","editorBudget":6,"speciesName":"KAQ4BD8E","editorSession":7,"editorFinished":true})");
        Check(gSnapshot.editorOpen && !gSnapshot.editorFinished && gSnapshot.editorSession==8 && gSnapshot.speciesName.empty(),
            "An old close cannot close the new editor visit");
        CoopNet::SubmitEditorClose("final",6,"KAQ4BD8E",8,true);
        const auto closing=gOutgoing.back();
        Check(closing.find(R"("editorSession":8)")!=std::string::npos && closing.find(R"("editorFinished":true)")!=std::string::npos,
            "Native finish packet includes its session and explicit acceptance");
        gSnapshot.sessionEnded = false;
        gSnapshot.connected = gSnapshot.inviteAccepted = true;
        HandleMessage("{\"type\":\"sessionEnded\",\"reason\":\"host_left\"}");
        MarkDisconnected();
        Check(gSnapshot.sessionEnded && gSnapshot.disconnectReason == "host_left" &&
            !gSnapshot.connected && !gSnapshot.inviteAccepted,
            "TCP teardown preserves the host-left reason and clears active session state");
        std::cout << "PASS: " << checks << " native visual/network assertions. No gameplay was tested.\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL after " << checks << " checks: " << error.what() << '\n';
        return 1;
    }
}

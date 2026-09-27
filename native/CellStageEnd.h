// SPDX-License-Identifier: GPL-3.0-or-later
// Included after the native UI and campaign helpers in Probe.cpp.
using CellStageTransitionFunction=void(__cdecl*)();
CellStageTransitionFunction gLeaveCellStageOriginal=nullptr, gEnterLandEditorOriginal=nullptr;
bool gCellStageEndHooks=false;
IWindowPtr gCellStageEndBlack, gCellStageEndTitle, gCellStageEndDetail;
IWindowPtr gCellStageEndSteam, gCellStageEndVersion, gCellStageEndMenu;

bool RequestCellStageEnd()
{
    const auto state=CoopNet::GetSnapshot();
    // Leave the unmodified single-player launch alone; this is the co-op release's boundary.
    if (!state.enabled || !Simulator::IsCellGame()) return false;
    if (!gCellStageEnd.requested && !gCellStageEnd.visible) {
        gCellStageEnd.Request(state.worldGeneration);
        if (state.connected && state.inviteAccepted) {
            UpdateSharedCellProgress(state); // Queue the final pickup before its completion.
            CoopNet::SubmitCellStageComplete(state.worldGeneration);
        }
        WriteProbeLog("Version 1: Cell stage complete; intercepted landfall after History, before the Creature stage.");
    }
    return true;
}

void __cdecl LeaveCellStageHook() {
    if (!RequestCellStageEnd()) gLeaveCellStageOriginal();
}
void __cdecl EnterLandEditorHook() {
    if (!RequestCellStageEnd()) gEnterLandEditorOriginal();
}

class CellStageEndInput : public UTFWin::DefaultWinProc<> {
public:
    bool HandleUIMessage(UTFWin::IWindow*,const UTFWin::Message& message) override {
        // The full-screen background receives clicks outside the menu button.
        return message.eventType>=UTFWin::kMsgKeyDown && message.eventType<=UTFWin::kMsgMouseWheel;
    }
};

void LayoutCellStageEnd()
{
    auto root=WindowManager.GetMainWindow();
    if (!root) return;
    // Gameplay has no Russian pause-menu captions to inspect. Use the locale
    // itself so the ending stays Russian after the History window is hidden.
    if (auto locale=App::cLocaleManager::Get()) {
        const auto& language=locale->GetActiveLanguage();
        gUiUsesRussian=language.size()>=2 && (language[0]==u'r' || language[0]==u'R') &&
            (language[1]==u'u' || language[1]==u'U');
    }
    const auto area=root->GetArea();
    const float width=area.GetWidth(), height=area.GetHeight();
    const float left=std::max(12.0f,(width-640.0f)*0.5f), right=width-left;
    const float top=std::max(20.0f,height*0.5f-125.0f);
    if (!gCellStageEndBlack) {
        gCellStageEndBlack=CreateCoopPanel(0x5c0f1020);
        gCellStageEndBlack->SetDrawable(new CoopUi::Drawable(CoopUi::Style::Blackout));
        gCellStageEndBlack->SetFlag(UTFWin::kWinFlagEnabled,true);
        gCellStageEndBlack->SetFlag(UTFWin::kWinFlagIgnoreMouse,false);
        gCellStageEndBlack->AddWinProc(new CellStageEndInput());
        gCellStageEndTitle=CreateCoopButton(0x5c0f1021,u"",CoopUi::Style::Label);
        gCellStageEndDetail=CreateCoopButton(0x5c0f1022,u"",CoopUi::Style::Label);
        gCellStageEndSteam=CreateCoopButton(0x5c0f1023,u"",CoopUi::Style::Label);
        gCellStageEndVersion=CreateCoopButton(0x5c0f1024,u"",CoopUi::Style::Label);
        gCellStageEndMenu=CreateCoopButton(kCellStageEndMenuID,u"",CoopUi::Style::Secondary);
    }
    gCellStageEndBlack->SetArea({0,0,width,height});
    gCellStageEndTitle->SetArea({left,top,right,top+48});
    gCellStageEndDetail->SetArea({left,top+64,right,top+99});
    gCellStageEndSteam->SetArea({left,top+99,right,top+134});
    gCellStageEndVersion->SetArea({left,top+155,right,top+185});
    gCellStageEndMenu->SetArea({width*.5f-125,top+210,width*.5f+125,top+254});
    gCellStageEndTitle->SetCaption(CoopText(u"The first stage is complete!",u"\u041f\u0435\u0440\u0432\u044b\u0439 \u044d\u0442\u0430\u043f \u0437\u0430\u0432\u0435\u0440\u0448\u0451\u043d!"));
    gCellStageEndDetail->SetCaption(CoopText(u"The remaining stages are coming soon",u"\u041e\u0441\u0442\u0430\u043b\u044c\u043d\u044b\u0435 \u044d\u0442\u0430\u043f\u044b \u0441\u043a\u043e\u0440\u043e \u043f\u043e\u044f\u0432\u044f\u0442\u0441\u044f"));
    gCellStageEndSteam->SetCaption(CoopText(u"in the mod for SPORE on Steam.",u"\u0432 \u043c\u043e\u0434\u0435 \u0434\u043b\u044f SPORE \u0432 Steam."));
    gCellStageEndVersion->SetCaption(CoopText(u"Version 1 \u2014 Cell Stage",u"Version 1 \u2014 \u044d\u0442\u0430\u043f \u00ab\u041a\u043b\u0435\u0442\u043a\u0430\u00bb"));
    gCellStageEndMenu->SetCaption(CoopText(u"Main menu",u"\u0412 \u0433\u043b\u0430\u0432\u043d\u043e\u0435 \u043c\u0435\u043d\u044e"));
    for (auto window:{gCellStageEndBlack.get(),gCellStageEndTitle.get(),gCellStageEndDetail.get(),
        gCellStageEndSteam.get(),gCellStageEndVersion.get(),gCellStageEndMenu.get()}) {
        if (window->GetParent()!=root) {
            if (auto parent=window->GetParent()) parent->RemoveWindow(window);
            root->AddWindow(window);
        }
        window->SetFlag(UTFWin::kWinFlagVisible,true);
        root->BringToFront(window);
    }
}

bool UpdateCellStageEnd(const CoopNet::Snapshot& state)
{
    if (gCellStageEnd.dismissed && state.cellStageComplete && state.sessionEnded) {
        CoopNet::AcknowledgeSessionEnd();
        return true;
    }
    const bool wasVisible=gCellStageEnd.visible;
    if (!gCellStageEnd.Observe(state.worldGeneration,state.inviteAccepted && state.cellStageComplete,
        Simulator::IsCellGame(),gTimelineVisible)) return false;
    if (!wasVisible) {
        // Completion and its final food snapshot may arrive between gameplay
        // updates. Save the completed progress, never the previous frame's food.
        if (state.inviteAccepted && state.progressInitialized) UpdateSharedCellProgress(state);
        HideCoopUI();
        for (auto window:{gSessionEndedPanel.get(),gSessionEndedTitle.get(),gSessionEndedOk.get(),gPeerArrow.get()})
            if (window) window->SetFlag(UTFWin::kWinFlagVisible,false);
        if (!BlockGuestWorldSave()) {
            // Same synchronous message as the native Cell save command (E7F6FD).
            MessageManager.MessageSend(Simulator::kMsgSaveGame,nullptr);
            WriteProbeLog("Version 1 completion requested the owner's native Cell campaign save.");
        }
    }
    if (auto manager=Simulator::cGameTimeManager::Get())
        gCoopPause.Set(true,[manager]{manager->Pause(Simulator::TimeManagerPause::Gameplay);},[]{});
    LayoutCellStageEnd();
    if (gCellStageEnd.menuRequested) {
        ReleaseCoopPause();
        gGuestExitInProgress=BlockGuestWorldSave();
        // Keep the save lease through OnExit. No save is requested by the guest.
        if (Simulator::GetGameModeID()==GameModeIDs::kGGEMode || GameModeManager.SetActiveMode(GameModeIDs::kGGEMode)) {
            if (state.connected && state.inviteAccepted && CoopSession::IsWorldOwner(state,CoopNet::GetRole())) {
                CoopNet::SubmitWorldLeave(state.worldGeneration);
                gWorldLifecycle.leaveSent=true;
            }
            for (auto window:{gCellStageEndBlack.get(),gCellStageEndTitle.get(),gCellStageEndDetail.get(),
                gCellStageEndSteam.get(),gCellStageEndVersion.get(),gCellStageEndMenu.get()})
                if (window) window->SetFlag(UTFWin::kWinFlagVisible,false);
            gCellStageEnd.Dismiss();
            CoopNet::AcknowledgeSessionEnd();
        }
    }
    return true;
}

bool AttachCellStageEndHooks()
{
    using namespace CoopEngine;
    gLeaveCellStageOriginal=reinterpret_cast<CellStageTransitionFunction>(VerifiedMotionCode(
        kLeaveCellStageRva,kLeaveCellStageSize,kLeaveCellStageHash,kLeaveCellStageRelocations));
    gEnterLandEditorOriginal=reinterpret_cast<CellStageTransitionFunction>(VerifiedMotionCode(
        kEnterLandEditorRva,kEnterLandEditorSize,kEnterLandEditorHash,kEnterLandEditorRelocations));
    if (!gLeaveCellStageOriginal || !gEnterLandEditorOriginal) return false;
    if (DetourAttach(reinterpret_cast<PVOID*>(&gLeaveCellStageOriginal),LeaveCellStageHook)!=NO_ERROR) return false;
    if (DetourAttach(reinterpret_cast<PVOID*>(&gEnterLandEditorOriginal),EnterLandEditorHook)!=NO_ERROR) {
        DetourDetach(reinterpret_cast<PVOID*>(&gLeaveCellStageOriginal),LeaveCellStageHook); return false;
    }
    return true;
}

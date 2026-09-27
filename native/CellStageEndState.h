// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
namespace CoopSession {
struct CellStageEndState {
    std::uint64_t world=0;
    bool requested=false, visible=false, dismissed=false, menuRequested=false;
    void Request(std::uint64_t generation) {
        world=generation; requested=true; dismissed=false;
    }
    bool Observe(std::uint64_t generation,bool completed,bool inCell,bool historyVisible) {
        // A finished screen survives the owner leaving and TCP reconnection.
        if (generation!=world && !visible) {
            *this=CellStageEndState{}; world=generation;
        }
        if (inCell && completed && !dismissed) requested=true;
        if (requested && inCell && !historyVisible) visible=true;
        // Own the update while waiting, too: the normal guest-exit path would
        // otherwise unload the world and close History when its owner leaves.
        return visible || (requested && inCell && historyVisible);
    }
    void Dismiss() {
        requested=visible=menuRequested=false; dismissed=true;
    }
};
}

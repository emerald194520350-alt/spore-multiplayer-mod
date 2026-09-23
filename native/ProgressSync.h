#pragma once

#include "CoopNet.h"
#include <algorithm>
#include <deque>

namespace CoopProgress
{
    inline void MergeMilestones(CoopNet::CellProgress& into, const CoopNet::CellProgress& from)
    {
        for (size_t i = 0; i < into.unlocks.size(); ++i)
            into.unlocks[i] = std::max(into.unlocks[i], from.unlocks[i]);
        for (size_t i = 0; i < into.missions.size(); ++i)
            if (i % 4 == 0 || i % 4 == 3)
                into.missions[i] = std::max(into.missions[i], from.missions[i]);
        into.playerHasMoved |= from.playerHasMoved;
        into.playerHasEaten |= from.playerHasEaten;
        into.partCinematicPlayed |= from.partCinematicPlayed;
        into.showMateButton |= from.showMateButton;
        into.firstEditorEntry |= from.firstEditorEntry;
    }

    struct Event
    {
        std::uint64_t sequence = 0;
        CoopNet::CellProgress delta;
    };

    // Compare the engine only against the state last applied to it. Keep
    // unacknowledged local gains on top of server state, so an older snapshot
    // cannot erase a just-earned part or make the same food get sent twice.
    class Reconciler
    {
        bool initialized = false;
        std::uint64_t nextSequence = 1;
        CoopNet::CellProgress baseline;
        std::deque<Event> pending;
    public:
        bool IsInitialized() const { return initialized; }
        // Native replay may set local presentation flags. They are already
        // applied state, not another pickup to send back to the session.
        void ObserveApplied(const CoopNet::CellProgress& value) { baseline = value; }
        void Reset() { *this = Reconciler{}; }
        void Initialize(const CoopNet::CellProgress& value)
        {
            initialized = true;
            baseline = value;
        }
        bool Capture(const CoopNet::CellProgress& current, Event& event, bool ownsSharedSpend = true)
        {
            if (!initialized) return false;
            auto& d = event.delta;
            d = current;
            d.food = std::max(0, current.food - baseline.food);
            d.plantFood = std::max(0, current.plantFood - baseline.plantFood);
            d.overPlantFood = std::max(0, current.overPlantFood - baseline.overPlantFood);
            d.overAnimalFood = std::max(0, current.overAnimalFood - baseline.overAnimalFood);
            // Both native editors save the same final body. Charge that shared
            // purchase once, from the world owner's native budget calculation.
            d.spent = ownsSharedSpend ? current.spent - baseline.spent : 0;
            d.killCount = std::max(0, current.killCount - baseline.killCount);
            bool missionProgress = false;
            for (size_t i = 0; i < d.missions.size(); ++i)
                if (i % 4 == 1 || i % 4 == 2)
                {
                    d.missions[i] = std::max(0,current.missions[i]-baseline.missions[i]);
                    missionProgress |= d.missions[i] != 0;
                }
            auto merged = baseline;
            MergeMilestones(merged, current);
            const bool changed = missionProgress || d.food || d.plantFood || d.overPlantFood ||
                d.overAnimalFood || d.spent || d.killCount ||
                merged.unlocks != baseline.unlocks || merged.missions != baseline.missions ||
                merged.playerHasMoved != baseline.playerHasMoved ||
                merged.playerHasEaten != baseline.playerHasEaten ||
                merged.partCinematicPlayed != baseline.partCinematicPlayed ||
                merged.showMateButton != baseline.showMateButton ||
                merged.firstEditorEntry != baseline.firstEditorEntry;
            baseline = current;
            if (!changed) return false;
            event.sequence = nextSequence++;
            pending.push_back(event);
            return true;
        }
        CoopNet::CellProgress Reconcile(const CoopNet::CellProgress& authoritative,
            std::uint64_t acknowledged)
        {
            while (!pending.empty() && pending.front().sequence <= acknowledged)
                pending.pop_front();
            auto value = authoritative;
            for (const auto& event : pending)
            {
                const auto& d = event.delta;
                value.food += d.food;
                value.plantFood += d.plantFood;
                value.overPlantFood += d.overPlantFood;
                value.overAnimalFood += d.overAnimalFood;
                value.spent = std::max(0, value.spent + d.spent);
                value.killCount += d.killCount;
                for (size_t i = 0; i < value.missions.size(); ++i)
                    if (i % 4 == 1 || i % 4 == 2) value.missions[i] += d.missions[i];
                MergeMilestones(value, d);
            }
            baseline = value;
            initialized = true;
            return value;
        }
    };
}

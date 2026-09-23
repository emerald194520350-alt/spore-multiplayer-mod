#pragma once
#include <cstdint>
namespace CoopEditor
{
    constexpr int MaxBudget = 100000000;
    inline bool ValidBudget(int value) { return value >= 0 && value <= MaxBudget; }
    // Rebase native local charges/refunds onto the matching remote model budget.
    // An identical merged model already includes our edit; never charge it twice.
    inline bool MergeBudget(int baseline, int local, int remote, bool keepsLocalEdits, int& result)
    {
        if (!ValidBudget(baseline) || !ValidBudget(local) || !ValidBudget(remote)) return false;
        const auto value = std::int64_t(remote) + (keepsLocalEdits ? std::int64_t(local)-baseline : 0);
        if (value < 0 || value > MaxBudget) return false;
        result = static_cast<int>(value);
        return true;
    }
}

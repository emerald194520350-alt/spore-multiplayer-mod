#pragma once
#include <cstdint>

namespace CoopVisual
{
    // Replay mouth gestures only. Death, hatching, editor and attack states
    // have gameplay meaning and must not be injected into a visual replica.
    inline bool IsEatingAnimation(std::uint32_t animation)
    {
        return animation == 6 || (animation >= 12 && animation <= 15) ||
            animation == 31 || animation == 32 || animation == 48 || animation == 49 ||
            (animation >= 79 && animation <= 81);
    }
    inline bool MouthTransition(std::uint32_t incoming, std::uint32_t current, std::uint32_t& target)
    {
        if (IsEatingAnimation(incoming)) target = incoming;
        else if (IsEatingAnimation(current)) target = 0;
        else return false;
        return current != target;
    }
}

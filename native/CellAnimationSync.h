#pragma once
#include <cstdint>

namespace CoopVisual
{
    inline std::uint32_t MouthAnimationId(std::uint32_t value)
    {
        switch (value)
        {
        case 6: case 0xAAAA0005: return 0xAAAA0005;
        case 12: case 0xAAAA0006: return 0xAAAA0006;
        case 13: case 0xAAAA0007: return 0xAAAA0007;
        case 14: case 0xAAAA0008: return 0xAAAA0008;
        case 15: case 0xAAAA0009: return 0xAAAA0009;
        case 31: case 0xAAAA0028: return 0xAAAA0028;
        case 32: case 0xAAAA0029: return 0xAAAA0029;
        case 48: case 0xAAAA0047: return 0xAAAA0047;
        case 49: case 0xAAAA0048: return 0xAAAA0048;
        case 79: case 0xAAAA0075: return 0xAAAA0075;
        case 80: case 0xAAAA0076: return 0xAAAA0076;
        case 81: case 0xAAAA0077: return 0xAAAA0077;
        default: return 0;
        }
    }
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

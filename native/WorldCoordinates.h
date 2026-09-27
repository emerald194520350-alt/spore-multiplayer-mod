#pragma once
#include <cmath>

namespace CoopWorld
{
    struct Point { double x = 0, y = 0, z = 0; };
    // SPORE rebases around each window's camera during growth. Network space
    // remains fixed while local positions and lengths are rescaled by the engine.
    struct Coordinates
    {
        double unit = 1;
        Point origin;
        Point Encode(Point p) const { return {p.x*unit+origin.x, p.y*unit+origin.y, p.z*unit+origin.z}; }
        Point Decode(Point p) const { return {(p.x-origin.x)/unit, (p.y-origin.y)/unit, (p.z-origin.z)/unit}; }
        bool Anchor(Point local, Point network, double networkUnitsPerLocalUnit)
        {
            if (!std::isfinite(networkUnitsPerLocalUnit) || networkUnitsPerLocalUnit<1e-12 ||
                networkUnitsPerLocalUnit>1e12 || !std::isfinite(local.x) || !std::isfinite(local.y) ||
                !std::isfinite(local.z) || !std::isfinite(network.x) || !std::isfinite(network.y) ||
                !std::isfinite(network.z)) return false;
            unit=networkUnitsPerLocalUnit;
            origin={network.x-local.x*unit,network.y-local.y*unit,network.z-local.z*unit};
            return true;
        }
        bool Rebase(Point before, Point after, double ratio)
        {
            const double next = unit*ratio;
            if (!std::isfinite(next) || next < 1e-12 || next > 1e12) return false;
            const auto anchor = Encode(before);
            origin = {anchor.x-after.x*next, anchor.y-after.y*next, anchor.z-after.z*next};
            unit = next;
            return true;
        }
    };
}

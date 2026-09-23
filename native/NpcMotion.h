#pragma once
#include "WorldCoordinates.h"
#include <algorithm>
#include <cstdint>
#include <deque>

namespace CoopWorld
{
    struct NpcPose
    {
        Point position;
        float qz = 0, qw = 1;
    };

    // Store fixed network coordinates, so growth can rebase the local camera
    // without invalidating the interpolation history. Never extrapolate combat
    // bodies beyond the last authoritative sample.
    class NpcMotion
    {
        struct Frame { std::uint64_t tick; NpcPose pose; };
        std::deque<Frame> frames;
        std::uint64_t sequence = 0;
    public:
        void Push(std::uint64_t seq, std::uint64_t tick, NpcPose pose, float scale)
        {
            if (!frames.empty() && seq <= sequence) return;
            const float length = std::hypot(pose.qz, pose.qw);
            if (length > 0.0001f) { pose.qz /= length; pose.qw /= length; }
            else { pose.qz = 0; pose.qw = 1; }
            if (!frames.empty())
            {
                const auto& last = frames.back();
                const double dx = pose.position.x-last.pose.position.x;
                const double dy = pose.position.y-last.pose.position.y;
                const double dz = pose.position.z-last.pose.position.z;
                const double limit = std::max(8.0, double(scale)*64);
                if (tick <= last.tick || tick-last.tick > 1000 || dx*dx+dy*dy+dz*dz > limit*limit)
                    frames.clear();
            }
            sequence = seq;
            frames.push_back({tick, pose});
            while (frames.size() > 8) frames.pop_front();
        }

        NpcPose Sample(std::uint64_t now) const
        {
            if (frames.empty()) return {};
            const auto tick = now > 125 ? now-125 : 0;
            if (tick <= frames.front().tick) return frames.front().pose;
            for (size_t i=1; i<frames.size(); ++i)
            {
                const auto& a = frames[i-1]; const auto& b = frames[i];
                if (tick > b.tick) continue;
                const float t = float(tick-a.tick)/float(b.tick-a.tick);
                const auto& p = a.pose.position; const auto& q = b.pose.position;
                NpcPose result{{p.x+(q.x-p.x)*t, p.y+(q.y-p.y)*t, p.z+(q.z-p.z)*t}};
                const float sign = a.pose.qz*b.pose.qz+a.pose.qw*b.pose.qw < 0 ? -1.0f : 1.0f;
                result.qz = a.pose.qz+(b.pose.qz*sign-a.pose.qz)*t;
                result.qw = a.pose.qw+(b.pose.qw*sign-a.pose.qw)*t;
                const float length = std::hypot(result.qz, result.qw);
                result.qz /= length; result.qw /= length;
                return result;
            }
            return frames.back().pose;
        }
    };
}

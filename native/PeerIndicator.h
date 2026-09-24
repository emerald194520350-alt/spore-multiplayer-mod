#pragma once
#include <algorithm>
#include <cmath>

namespace CoopUi
{
    struct IndicatorPoint { float x=0, y=0, dx=0, dy=0; bool visible=false; };

    // cViewer::GetCameraToPoint (7C4800) unprojects through field_100,
    // stored column-major. Solve its inverse to project without assuming a
    // fixed Cell camera angle, zoom, aspect ratio or world-growth scale.
    inline bool ProjectPeer(const float* clipToWorld, double x, double y, double z,
        float& ndcX, float& ndcY)
    {
        double a[4][5]{};
        const double point[4]={x,y,z,1};
        for (int row=0; row<4; ++row)
        {
            for (int col=0; col<4; ++col)
            {
                a[row][col]=clipToWorld[col*4+row];
                if (!std::isfinite(a[row][col])) return false;
            }
            a[row][4]=point[row];
        }
        for (int col=0; col<4; ++col)
        {
            int pivot=col;
            for (int row=col+1; row<4; ++row)
                if (std::abs(a[row][col])>std::abs(a[pivot][col])) pivot=row;
            if (std::abs(a[pivot][col])<1e-12) return false;
            for (int j=col; j<5; ++j) std::swap(a[col][j],a[pivot][j]);
            const double scale=a[col][col];
            for (int j=col; j<5; ++j) a[col][j]/=scale;
            for (int row=0; row<4; ++row) if (row!=col)
            {
                const double factor=a[row][col];
                for (int j=col; j<5; ++j) a[row][j]-=factor*a[col][j];
            }
        }
        if (!std::isfinite(a[3][4]) || a[3][4]<=1e-9) return false;
        ndcX=float(a[0][4]/a[3][4]); ndcY=float(a[1][4]/a[3][4]);
        return std::isfinite(ndcX) && std::isfinite(ndcY);
    }

    inline IndicatorPoint PeerArrow(float screenX, float screenY, float width, float height)
    {
        IndicatorPoint out;
        if (!std::isfinite(screenX) || !std::isfinite(screenY) || width<180 || height<180 ||
            (screenX>=0 && screenX<=width && screenY>=0 && screenY<=height)) return out;
        const float cx=width*.5f, cy=height*.5f;
        const float vx=screenX-cx, vy=screenY-cy, length=std::hypot(vx,vy);
        if (length<1) return out;
        out.dx=vx/length; out.dy=vy/length;
        // Keep the tip clear of the window frame and the Cell HUD at the bottom.
        const float tx=std::abs(vx)>1e-5f ? (cx-32)/std::abs(vx) : 1e10f;
        const float ty=std::abs(vy)>1e-5f ? (vy<0 ? cy-32 : cy-90)/std::abs(vy) : 1e10f;
        const float t=std::min(tx,ty);
        out.x=cx+vx*t; out.y=cy+vy*t; out.visible=true;
        return out;
    }
}

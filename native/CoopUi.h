#pragma once
#include <Spore/UTFWin/ButtonDrawableStandard.h>
#include <cmath>
#include "UiGraphicsAbi.h"

namespace CoopUi
{
    enum class Style { Primary, Secondary, Label, Panel };

    inline void RoundedFill(UTFWin::Graphics2D& graphics, const Math::Rectangle& r,
        float radius, uint32_t color)
    {
        graphics.SetColor(Math::Color(color));
        graphics.FillRectangle(r.x1, r.y1 + radius, r.x2, r.y2 - radius);
        for (int y = 0; y < int(radius); ++y)
        {
            const float dy = radius - float(y) - 0.5f;
            const float inset = radius - std::sqrt(radius * radius - dy * dy);
            graphics.FillRectangle(r.x1 + inset, r.y1 + y, r.x2 - inset, r.y1 + y + 1);
            graphics.FillRectangle(r.x1 + inset, r.y2 - y - 1, r.x2 - inset, r.y2 - y);
        }
    }

    class Drawable : public UTFWin::ButtonDrawableStandard
    {
        Style mStyle;
    public:
        explicit Drawable(Style style) : mStyle(style) {}
        void Paint(UTFWin::UIRenderer* renderer, const Math::Rectangle& area,
            const UTFWin::RenderParams& params) override
        {
            if (!renderer || mStyle == Style::Label) return;
            auto& graphics = renderer->GetGraphics2D();
            const Math::Color oldColor(ReadGraphicsColor(graphics));
            const bool enabled = (params.state & UTFWin::kStateEnabled) != 0;
            const bool hover = (params.state & UTFWin::kStateHover) != 0;
            const bool down = (params.state & UTFWin::kStateClicked) != 0;
            const bool primary = mStyle == Style::Primary;
            const bool panel = mStyle == Style::Panel;
            Math::Rectangle r = area;
            if (down && !panel) { r.y1 += 1; r.y2 += 1; }
            RoundedFill(graphics, { r.x1, r.y1 + 3, r.x2, r.y2 + 3 }, 9, 0x80202C3A);
            RoundedFill(graphics, r, 9, panel ? 0xFF7CA7BD :
                (hover && enabled ? 0xFFFFE5A1 : 0xFF87A6B7));
            uint32_t fill = panel ? 0xF21B354A : primary ? 0xFFE7C571 : 0xFF35576D;
            if (!panel && !enabled) fill = 0xFF3B4C57;
            else if (!panel && down) fill = primary ? 0xFFD1AA52 : 0xFF294458;
            else if (!panel && hover) fill = primary ? 0xFFF9DD93 : 0xFF486E86;
            RoundedFill(graphics, { r.x1 + 1, r.y1 + 1, r.x2 - 1, r.y2 - 1 }, 8, fill);
            graphics.SetColor(Math::Color(panel ? 0xFF4B7288 : primary ? 0xFFFFEAB2 : 0xFF7594A7));
            graphics.DrawLine(r.x1 + 10, r.y1 + 2, r.x2 - 10, r.y1 + 2);
            graphics.SetColor(oldColor);
        }
    };
}

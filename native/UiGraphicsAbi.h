// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>

namespace UTFWin { class Graphics2D; }

namespace CoopUi
{
    inline std::uint32_t ReadGraphicsColor(UTFWin::Graphics2D& graphics)
    {
        static_assert(sizeof(void*) == 4, "SPORE's UI uses the x86 ABI");
        // SDK Graphics2D::GetColor declares a Math::Color structure return.
        // MSVC emits a hidden result pointer for that declaration, but SPORE's
        // vtable slot 2 returns a uint32_t in EAX and consumes no arguments.
        // Calling the SDK declaration leaves that extra pointer on the stack,
        // corrupting the caller's saved registers after Drawable::Paint.
        // Use the scalar ABI through the vtable, without a game-version address.
        const auto table = *reinterpret_cast<void***>(&graphics);
        using GetColorArgb = std::uint32_t(__thiscall*)(UTFWin::Graphics2D*);
        return reinterpret_cast<GetColorArgb>(table[2])(&graphics);
    }
}

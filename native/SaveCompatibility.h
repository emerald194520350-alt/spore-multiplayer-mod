// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>

namespace CoopSaves
{
    inline bool SameFile(const std::filesystem::path& source, const std::filesystem::path& target)
    {
        std::error_code error;
        const auto size = std::filesystem::file_size(source, error);
        if (error || size > 64 * 1024 * 1024) return false;
        const auto targetSize = std::filesystem::file_size(target, error);
        if (error || size != targetSize) return false;
        std::ifstream a(source, std::ios::binary), b(target, std::ios::binary);
        if (!a || !b) return false;
        std::array<char, 8192> left{}, right{};
        std::uintmax_t remaining = size;
        while (remaining)
        {
            const auto chunk = std::streamsize(std::min<std::uintmax_t>(remaining, left.size()));
            a.read(left.data(), chunk); b.read(right.data(), chunk);
            if (a.gcount() != chunk || b.gcount() != chunk ||
                !std::equal(left.begin(), left.begin() + chunk, right.begin())) return false;
            remaining -= chunk;
        }
        return true;
    }

    // The launcher prepares Game0 while both games are closed. Joining only
    // verifies it: a failed live copy must never leave a mixture of campaigns.
    inline bool Compatible(const std::filesystem::path& source, const std::filesystem::path& target)
    {
        std::error_code error;
        std::filesystem::directory_iterator entry(source, error), end;
        if (error || source == target) return false;
        bool found = false;
        for (; entry != end && !error; entry.increment(error))
        {
            if (entry->path().extension() != L".spo") continue;
            found = true;
            if (!SameFile(entry->path(), target / entry->path().filename())) return false;
        }
        if (error || !found) return false;
        for (const auto* name : {L"lastSave.pld", L"PlanetScripts.pld", L"stars.db"})
            if (!SameFile(source / name, target / name)) return false;
        std::filesystem::directory_iterator targetEntry(target, error);
        for (; targetEntry != end && !error; targetEntry.increment(error))
            if (targetEntry->path().extension() == L".spo" &&
                !SameFile(source / targetEntry->path().filename(), targetEntry->path())) return false;
        return !error;
    }
}

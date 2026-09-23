#pragma once
#include <cstdint>
#include <limits>
#include <set>

namespace CoopEditor
{
    // Price resources before their asynchronous graphics/rigblock load. Use
    // the same modelPrice property as native EditorRigblock::ParseProp.
    template<class Blocks, class ReadPrice>
    bool ResourcePrice(const Blocks& blocks, ReadPrice readPrice, int& price)
    {
        std::set<size_t> seen;
        std::int64_t total = 0;
        for (size_t i=0; i<blocks.size(); ++i)
        {
            if (!seen.insert(i).second) continue;
            const auto& block = blocks[i];
            int partPrice = 0;
            if (!readPrice(block, partPrice) || partPrice < 0) return false;
            if (!block.isAsymmetric && block.symmetricIndex >= 0)
            {
                const auto other = static_cast<size_t>(block.symmetricIndex);
                if (other >= blocks.size()) return false;
                const auto& mirror = blocks[other];
                if (mirror.isAsymmetric || mirror.symmetricIndex != static_cast<int>(i) ||
                    mirror.instanceID != block.instanceID || mirror.groupID != block.groupID) return false;
                seen.insert(other);
            }
            total += partPrice;
            if (total > std::numeric_limits<int>::max()) return false;
        }
        price = static_cast<int>(total);
        return true;
    }

    // A symmetric pair is one purchase in the editor. The mirror may also be
    // present in mRigblocks; count it only once, independent of vector order.
    template<class Parts>
    bool ModelPrice(const Parts& parts, int& price)
    {
        std::set<const void*> seen;
        std::int64_t total = 0;
        for (const auto& entry : parts)
        {
            const auto part = entry.get();
            if (!part || part->mModelPrice < 0) return false;
            if (!seen.insert(part).second) continue;
            if (part->mpSymmetricRigblock) seen.insert(part->mpSymmetricRigblock.get());
            total += part->mModelPrice;
            if (total > std::numeric_limits<int>::max()) return false;
        }
        price = static_cast<int>(total);
        return true;
    }

    inline bool ReplacementBudget(int available, int previousPrice, int nextPrice, int& result)
    {
        const std::int64_t next = std::int64_t(available) + previousPrice - nextPrice;
        if (available < 0 || previousPrice < 0 || nextPrice < 0 || next < 0 ||
            next > std::numeric_limits<int>::max()) return false;
        result = static_cast<int>(next);
        return true;
    }
}

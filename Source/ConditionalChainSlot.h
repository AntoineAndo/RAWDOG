#pragma once

#include "ChainSlot.h"
#include "PixelCondition.h"
#include <optional>
#include <variant>
#include <vector>

// A chain slot that branches into two full sub-chains based on a per-pixel condition,
// instead of a single plugin. Never nested - a branch only ever holds plain ChainSlots,
// never another ConditionalChainSlot - see ChainEntry/ChainPath below.
struct ConditionalChainSlot
{
    PixelCondition condition;
    CompositingMode mode = CompositingMode::masked;
    std::vector<ChainSlot> branchA; // condition true
    std::vector<ChainSlot> branchB; // condition false
    bool bypassed = false;          // skip entirely; buffer passes through unchanged
};

// One entry in MainComponent::pluginChain - either an ordinary plugin slot or a
// conditional slot holding two of them. std::variant/std::get_if gives every call site
// that walks pluginChain compiler-enforced exhaustiveness, rather than a manually
// checked tag.
using ChainEntry = std::variant<ChainSlot, ConditionalChainSlot>;

// Which of a ConditionalChainSlot's two branches a ChainPath addresses.
enum class Branch { a, b };

// Addresses either a top-level pluginChain entry, or a specific slot nested inside one
// branch of a top-level ConditionalChainSlot. Generalizes the flat `int index` used
// throughout MainComponent before this feature existed - branches make the chain two
// levels deep, but no deeper (a branch can't contain another conditional slot).
struct ChainPath
{
    int topIndex = -1;
    std::optional<Branch> branch; // nullopt = the path targets the top-level entry itself
    int branchIndex = -1;         // valid only when branch has a value

    bool operator== (const ChainPath& other) const
    {
        return topIndex == other.topIndex && branch == other.branch && branchIndex == other.branchIndex;
    }
};

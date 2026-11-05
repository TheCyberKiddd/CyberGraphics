#pragma once

#include <cyber/core/types.h>
#include <cyber/memory/span.h>
#include <cyber/memory/string_view.h>

namespace cyber::shaders
{

using cyber::core::u32;
using cyber::core::u64;

/// Hash of the empty keyword set, and the seed of every permutation hash.
inline constexpr u64 kPermutationHashSeed = 0x43475045524D5554ULL;  // "CGPERMUT"

/// Identity of one shader permutation.
///
/// `hash` depends only on the set of enabled keyword names: it is the same for
/// any order the caller lists them in, any order the source declares them in,
/// and on every machine. `mask` has bit `i` set when the source's `i`-th
/// declared keyword is enabled; it is meaningful only next to that source.
struct PermutationKey
{
    u64 hash = kPermutationHashSeed;
    u32 mask = 0;

    friend constexpr bool operator==(const PermutationKey&, const PermutationKey&) = default;
};

/// Stable hash of a keyword set.
///
/// The names are sorted by byte value, duplicates are dropped, and each name's
/// xxHash64 is combined into kPermutationHashSeed in that order. The empty set
/// hashes to kPermutationHashSeed.
///
/// @param keywords Enabled keyword names; borrowed. At most 64 distinct names
///        are considered; callers validate against kMaxKeywords first.
/// @return The permutation hash.
[[nodiscard]] u64 HashKeywordSet(memory::Span<const memory::StringView> keywords) noexcept;

}  // namespace cyber::shaders

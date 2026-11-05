#include <cyber/core/hash.h>
#include <cyber/shaders/permutation.h>

#include <array>

namespace cyber::shaders
{
namespace
{

using cyber::core::usize;
using cyber::memory::StringView;

constexpr usize kMaximumHashedKeywords = 64;

// Byte-wise lexicographic order, so the result never depends on locale.
[[nodiscard]] bool Less(StringView lhs, StringView rhs) noexcept
{
    const usize shared = lhs.Size() < rhs.Size() ? lhs.Size() : rhs.Size();
    for (usize index = 0; index < shared; ++index)
    {
        const auto left = static_cast<unsigned char>(lhs[index]);
        const auto right = static_cast<unsigned char>(rhs[index]);
        if (left != right)
        {
            return left < right;
        }
    }
    return lhs.Size() < rhs.Size();
}

}  // namespace

u64 HashKeywordSet(memory::Span<const memory::StringView> keywords) noexcept
{
    std::array<StringView, kMaximumHashedKeywords> sorted{};
    usize count = 0;
    for (usize index = 0; index < keywords.Size() && count < kMaximumHashedKeywords; ++index)
    {
        // Insertion sort with duplicate removal; the set is at most 64 names.
        const StringView name = keywords[index];
        usize position = 0;
        while (position < count && Less(sorted[position], name))
        {
            ++position;
        }
        if (position < count && sorted[position] == name)
        {
            continue;
        }
        for (usize move = count; move > position; --move)
        {
            sorted[move] = sorted[move - 1];
        }
        sorted[position] = name;
        ++count;
    }

    u64 hash = kPermutationHashSeed;
    for (usize index = 0; index < count; ++index)
    {
        hash = core::HashCombine(hash, core::HashBytes(sorted[index].Data(), sorted[index].Size(),
                                                       kPermutationHashSeed));
    }
    return hash;
}

}  // namespace cyber::shaders

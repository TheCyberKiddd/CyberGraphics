#pragma once

#include <cyber/core/types.h>

namespace cyber::rhi
{

using cyber::core::u32;

/// Description of a device-owned timestamp query heap.
struct TimestampQueryHeapDesc
{
    /// Number of timestamp slots; must be non-zero.
    u32 queryCount = 0;
    /// Optional borrowed name, which must outlive the heap.
    const char* label = nullptr;
};

}  // namespace cyber::rhi

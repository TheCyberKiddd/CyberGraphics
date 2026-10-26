#pragma once

#include <cyber/core/types.h>

namespace cyber::rhi
{

struct Backend;

// Whether this build contains the Direct3D 12 backend.
//
// A build without the SDK headers compiles the backend out entirely, so a
// caller that ships on several platforms checks this before it asks for
// `D3D12Backend()`. The value is a build constant: it never changes at runtime
// and is safe to read from any thread.
[[nodiscard]] constexpr bool IsD3D12CompiledIn() noexcept
{
#if defined(CYBER_RHI_HAS_D3D12)
    return true;
#else
    return false;
#endif
}

// Returns the Direct3D 12 backend, or null when this build does not contain it.
//
// The backend drives DXGI and D3D12 only; no D3D12 header is visible from this
// one. Its adapter list is built per call from a freshly created DXGI factory, so
// the backend keeps no global mutable state and there is nothing to initialize
// before the first call and nothing to tear down after the last.
//
// `Backend::enumerateAdapters` writes hardware adapters in DXGI's preference
// order, then Microsoft's WARP software adapter last, so a caller that takes the
// first entry takes the discrete GPU and CI can take the last one.
//
// Returns a pointer with static lifetime, or null when Direct3D 12 is not part
// of this build.
[[nodiscard]] const Backend* D3D12Backend() noexcept;

}  // namespace cyber::rhi

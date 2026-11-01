#pragma once

// The validating device: one rule set, checked on both callers (ADR 0018).
//
// A real backend checks almost none of what the RHI promises — argument
// validation, handle liveness, capability limits — so a device the caller
// opted into validation for gets a `DeviceInterface` whose `internal` is a
// wrapper state. Every call is checked against the shared rule set in
// `validation.h` and the wrapper's live-object tables, then forwarded to the
// backend the caller asked for.
//
// Checks by level:
//
// - `ValidationLevel::Off` never reaches this file: `Device::Create` only
//   installs the wrapper for a level that asks for it.
// - `Standard` checks descriptor validity, handle liveness, capability limits,
//   and destroy-while-in-flight. It never tracks subresource state: the
//   recorded state machine is the null backend's, and a real backend's own
//   answer to a state question is forwarded unchanged.
// - `Exhaustive` is `Standard` plus the leak report at device destruction and
//   the per-call trace, which land with this phase's remaining increments.
//
// The wrapper is transparent: every answer it forwards is the backend's own,
// and a call it cannot check (for a resource the backend created out of band,
// such as a swapchain image) is forwarded untouched rather than guessed at. A
// rule it would repair by itself is a bug in the wrapper, which is why nothing
// here rewrites a call.
//
// - No allocation without the device's allocator: the wrapper state and the
//   per-list records come from the allocator the device was created with.
// - No throw, no stdout: errors are `Status` failures and `CG_LOG` lines.
// - No global state: everything hangs off the wrapper state a creation
//   returns.

#include <cyber/core/error.h>
#include <cyber/core/result.h>
#include <cyber/rhi/backend.h>
#include <cyber/rhi/descriptors.h>

namespace cyber::rhi
{

// Wraps the device a backend already created. `real` is the backend's own
// creation (its per-device table and its state), `backend` is the backend
// itself so the wrapper can drive its destroy path, `level` selects how much
// the wrapper checks, and `allocator` backs the wrapper's own state. Returns
// the wrapper's own `DeviceCreation`, which the front end installs.
//
// Fails with `ErrorCode::Internal` when the wrapper cannot allocate its state;
// the caller must then destroy the real device it still holds. The wrapper
// never fails the creation of a valid backend device for any other reason.
[[nodiscard]] Result<DeviceCreation, Error>
CreateValidatingDevice(const DeviceCreation& real, const Backend& backend, ValidationLevel level,
                       memory::Allocator& allocator) noexcept;

// Releases one wrapper created above: forwards `waitIdle` to the real device,
// frees the wrapper's per-list records, calls the backend's own
// `destroyDevice` on the state the wrapper remembers, and frees the wrapper
// itself. The front end routes destruction here instead of to the backend
// whenever it is holding wrapper state.
void DestroyValidatingDevice(void* internal) noexcept;

// True when `interface` is the wrapper's own per-device table, which tells the
// front end that the state it holds beside the interface is wrapper state and
// must be released with `DestroyValidatingDevice` rather than the backend's
// own destroy path.
[[nodiscard]] bool IsWrapperInterface(const DeviceInterface& interface) noexcept;

}  // namespace cyber::rhi

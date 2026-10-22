#pragma once

#include <cyber/core/types.h>

namespace cyber::perf
{

using cyber::core::f64;

// Non-owning instrumentation interface. Implementations passed to worker
// threads must be thread-safe. Marker names are non-owning and must remain
// valid for the duration of the call when stored by an implementation.
class Profiler
{
public:
    Profiler() = default;
    Profiler(const Profiler&) = delete;
    Profiler& operator=(const Profiler&) = delete;
    virtual ~Profiler() = default;

    virtual void BeginScope(const char* name) noexcept = 0;
    virtual void EndScope() noexcept = 0;
    virtual void Counter(const char* name, f64 value) noexcept = 0;
};

// Disabled instrumentation. Every call is a no-op and performs no allocation.
class NullProfiler final : public Profiler
{
public:
    void BeginScope(const char* name) noexcept override;
    void EndScope() noexcept override;
    void Counter(const char* name, f64 value) noexcept override;
};

// Returns a process-lifetime no-op profiler. The object is immutable.
[[nodiscard]] NullProfiler& NullProfilerInstance() noexcept;

}  // namespace cyber::perf

#include "locked_allocator.h"

#include <cyber/core/log.h>
#include <cyber/memory/dynamic_array.h>
#include <cyber/platform/filesystem.h>
#include <cyber/shaders/library.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <memory>
#include <new>
#include <utility>

namespace cyber::shaders
{
namespace
{

using cyber::core::Error;
using cyber::core::ErrorCode;
using cyber::core::Result;
using cyber::core::Status;
using cyber::core::usize;
using cyber::memory::Byte;
using cyber::memory::DynamicArray;
using cyber::memory::Span;
using cyber::memory::String;
using cyber::memory::StringView;

constexpr u32 kMaximumPermutationLimit = 1U << kMaxKeywords;
constexpr u64 kNanosecondsPerMillisecond = 1000000ULL;

// Monotonic nanoseconds; only differences are meaningful.
[[nodiscard]] u64 NowNanoseconds() noexcept
{
    return static_cast<u64>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                std::chrono::steady_clock::now().time_since_epoch())
                                .count());
}

struct PermutationRequest
{
    std::array<ReflectedName, kMaxKeywords> names{};
    u32 count = 0;
    u64 hash = kPermutationHashSeed;
};

[[nodiscard]] bool IsKeywordName(StringView name) noexcept
{
    if (name.Empty() || name.Size() > kMaxNameLength)
    {
        return false;
    }
    for (usize index = 0; index < name.Size(); ++index)
    {
        const char value = name[index];
        const bool letter =
            (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') || value == '_';
        const bool digit = value >= '0' && value <= '9';
        if (!letter && !(digit && index != 0))
        {
            return false;
        }
    }
    return true;
}

[[nodiscard]] PermutationRequest MakeRequest(Span<const StringView> keywords) noexcept
{
    PermutationRequest request{};
    for (usize index = 0; index < keywords.Size(); ++index)
    {
        bool duplicate = false;
        for (u32 prior = 0; prior < request.count; ++prior)
        {
            duplicate = duplicate || request.names[prior].View() == keywords[index];
        }
        if (duplicate)
        {
            continue;
        }
        ReflectedName& name = request.names[request.count++];
        for (usize character = 0; character < keywords[index].Size(); ++character)
        {
            name.text[character] = keywords[index][character];
        }
        name.size = static_cast<cyber::core::u8>(keywords[index].Size());
    }
    std::array<StringView, kMaxKeywords> views{};
    for (u32 index = 0; index < request.count; ++index)
    {
        views[index] = request.names[index].View();
    }
    request.hash = HashKeywordSet(Span<const StringView>(views.data(), request.count));
    return request;
}

}  // namespace

struct CompileJob;

struct ShaderEntry
{
    explicit ShaderEntry(memory::Allocator& allocator) noexcept
        : path(allocator), requests(allocator), results(allocator)
    {
    }

    String path;
    DynamicArray<PermutationRequest> requests;
    DynamicArray<CompiledShader> results;
    KeywordList keywords{};
    bool requestAll = false;
    bool dirty = true;
    bool hasError = false;
    Diagnostic error{};
    u32 watchId = 0;
    u64 changeDetectedAt = 0;
    CompileJob* job = nullptr;
    jobs::JobHandle handle{};
    ShaderStats stats{};
};

struct CompileJob
{
    explicit CompileJob(memory::Allocator& allocator) noexcept
        : path(allocator), requests(allocator), results(allocator)
    {
    }

    // Inputs, copied when the job is created.
    memory::Allocator* allocator = nullptr;
    ShaderCache* cache = nullptr;
    const char* target = kHlslTarget;
    u32 limit = 0;
    String path;
    DynamicArray<PermutationRequest> requests;
    bool requestAll = false;

    // Outputs, written by the job and read after `done`.
    DynamicArray<CompiledShader> results;
    KeywordList keywords{};
    Diagnostic error{};
    bool ok = false;
    u64 compilations = 0;
    u64 cacheHits = 0;
    std::atomic<bool> done{false};
};

namespace
{

void Fail(CompileJob& job, DiagnosticKind kind, const char* message) noexcept
{
    job.ok = false;
    job.error = Diagnostic{kind, job.path.View(), 1, 1, message};
}

[[nodiscard]] bool HasRequest(const DynamicArray<PermutationRequest>& requests, u64 hash) noexcept
{
    for (usize index = 0; index < requests.Size(); ++index)
    {
        if (requests[index].hash == hash)
        {
            return true;
        }
    }
    return false;
}

// Adds every subset of the declared keywords to `requests`.
[[nodiscard]] bool ExpandAll(CompileJob& job) noexcept
{
    const u32 total = 1U << job.keywords.count;
    if (total > job.limit)
    {
        Fail(job, DiagnosticKind::Unsupported,
             "shader declares more permutations than the library limit allows");
        return false;
    }
    for (u32 mask = 0; mask < total; ++mask)
    {
        std::array<StringView, kMaxKeywords> names{};
        u32 count = 0;
        for (u32 bit = 0; bit < job.keywords.count; ++bit)
        {
            if ((mask & (1U << bit)) != 0)
            {
                names[count++] = job.keywords.names[bit].View();
            }
        }
        const PermutationRequest request = MakeRequest(Span<const StringView>(names.data(), count));
        if (!HasRequest(job.requests, request.hash) && !job.requests.TryPushBack(request))
        {
            Fail(job, DiagnosticKind::OutOfMemory, "unable to allocate permutation requests");
            return false;
        }
    }
    return true;
}

void RunCompileJob(void* userData) noexcept
{
    CompileJob& job = *static_cast<CompileJob*>(userData);
    job.ok = true;

    auto file = platform::ReadFile(job.path.CStr(), *job.allocator);
    if (!file)
    {
        Fail(job, DiagnosticKind::Unsupported, "unable to read shader file");
        job.done.store(true, std::memory_order_release);
        return;
    }
    const DynamicArray<Byte>& bytes = file.Value();
    const StringView source(reinterpret_cast<const char*>(bytes.Data()), bytes.Size());

    auto scanned = ScanKeywords(job.path.View(), source, *job.allocator);
    if (!scanned)
    {
        job.ok = false;
        job.error = scanned.Error();
        job.done.store(true, std::memory_order_release);
        return;
    }
    job.keywords = scanned.Value();
    if (job.requestAll && !ExpandAll(job))
    {
        job.done.store(true, std::memory_order_release);
        return;
    }
    if (job.requests.Size() > job.limit)
    {
        Fail(job, DiagnosticKind::Unsupported, "permutation request limit exceeded");
        job.done.store(true, std::memory_order_release);
        return;
    }

    const StringView target(job.target);
    for (usize index = 0; index < job.requests.Size() && job.ok; ++index)
    {
        const PermutationRequest& request = job.requests[index];
        PermutationKey permutation{};
        permutation.hash = request.hash;
        const ShaderCacheKey key = MakeShaderCacheKey(source, permutation, target);
        if (job.cache != nullptr)
        {
            auto cached = job.cache->Load(key, *job.allocator);
            if (cached)
            {
                ++job.cacheHits;
                if (!job.results.TryPushBack(std::move(cached.Value())))
                {
                    Fail(job, DiagnosticKind::OutOfMemory, "unable to store compiled shader");
                }
                continue;
            }
        }

        std::array<StringView, kMaxKeywords> names{};
        for (u32 name = 0; name < request.count; ++name)
        {
            names[name] = request.names[name].View();
        }
        CompileOptions options{};
        options.keywords = Span<const StringView>(names.data(), request.count);
        auto compiled = CompileHlsl(job.path.View(), source, options, *job.allocator);
        ++job.compilations;
        if (!compiled)
        {
            job.ok = false;
            job.error = compiled.Error();
            break;
        }
        if (job.cache != nullptr)
        {
            // A failed store only costs a recompile next time.
            static_cast<void>(job.cache->Store(key, compiled.Value(), *job.allocator));
        }
        if (!job.results.TryPushBack(std::move(compiled.Value())))
        {
            Fail(job, DiagnosticKind::OutOfMemory, "unable to store compiled shader");
        }
    }
    job.done.store(true, std::memory_order_release);
}

}  // namespace

struct ShaderLibrary::Impl
{
    Impl(const ShaderLibraryDesc& desc, memory::Allocator& upstream) noexcept
        : upstream(&upstream), locked(upstream), config(desc), entries(locked), events(locked),
          watcher(locked)
    {
    }

    ~Impl()
    {
        WaitAll();
        for (usize index = 0; index < entries.Size(); ++index)
        {
            DestroyEntry(entries[index]);
        }
    }

    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;

    [[nodiscard]] ShaderEntry* Find(ShaderId id) const noexcept
    {
        return id != 0 && id <= entries.Size() ? entries[id - 1] : nullptr;
    }

    void DestroyJob(CompileJob* job) noexcept
    {
        if (job != nullptr)
        {
            std::destroy_at(job);
            locked.Deallocate(job);
        }
    }

    void DestroyEntry(ShaderEntry* entry) noexcept
    {
        if (entry != nullptr)
        {
            DestroyJob(entry->job);
            std::destroy_at(entry);
            locked.Deallocate(entry);
        }
    }

    void PushEvent(const ShaderEvent& event) noexcept
    {
        if (eventRead == events.Size())
        {
            events.Clear();
            eventRead = 0;
        }
        // An event that cannot be queued is dropped; the shader's state and
        // version still reflect the outcome.
        static_cast<void>(events.TryPushBack(event));
    }

    void Publish(ShaderId id, ShaderEntry& entry, CompileJob& job, u64 now) noexcept
    {
        entry.stats.compilations += job.compilations;
        entry.stats.cacheHits += job.cacheHits;
        if (job.keywords.count > 0 || job.ok)
        {
            entry.keywords = job.keywords;
        }
        entry.stats.stage = entry.keywords.stage;
        entry.stats.declaredKeywords = entry.keywords.count;
        entry.stats.possiblePermutations = 1U << entry.keywords.count;
        entry.stats.requestedPermutations = static_cast<u32>(job.requests.Size());

        ShaderEvent event{};
        event.shader = id;
        event.latencyNanoseconds = now >= entry.changeDetectedAt ? now - entry.changeDetectedAt : 0;
        if (job.ok)
        {
            entry.results = std::move(job.results);
            entry.hasError = false;
            entry.error = Diagnostic{};
            ++entry.stats.version;
            entry.stats.compiledPermutations = static_cast<u32>(entry.results.Size());
            event.kind = ShaderEventKind::Compiled;
        }
        else
        {
            ++entry.stats.failures;
            entry.hasError = true;
            entry.error = job.error;
            entry.error.sourceName = entry.path.View();
            event.kind = ShaderEventKind::Failed;
            CG_LOG(Warning, Shaders, "%s:%u:%u: %s (keeping version %u)", entry.path.CStr(),
                   entry.error.line, entry.error.column,
                   entry.error.message != nullptr ? entry.error.message : "compilation failed",
                   entry.stats.version);
        }
        event.version = entry.stats.version;
        PushEvent(event);
    }

    // Publishes every finished job. With `wait`, blocks until each job ends.
    void Collect(bool wait) noexcept
    {
        for (usize index = 0; index < entries.Size(); ++index)
        {
            ShaderEntry& entry = *entries[index];
            if (entry.job == nullptr)
            {
                continue;
            }
            if (!wait && !entry.job->done.load(std::memory_order_acquire))
            {
                continue;
            }
            if (config.jobs != nullptr && entry.handle.IsValid())
            {
                config.jobs->Wait(entry.handle);
            }
            entry.handle = jobs::JobHandle{};
            Publish(static_cast<ShaderId>(index + 1), entry, *entry.job, NowNanoseconds());
            DestroyJob(entry.job);
            entry.job = nullptr;
        }
    }

    void WaitAll() noexcept
    {
        Collect(true);
    }

    [[nodiscard]] CompileJob* MakeJob(const ShaderEntry& entry) noexcept
    {
        void* memory = locked.Allocate(sizeof(CompileJob), alignof(CompileJob));
        if (memory == nullptr)
        {
            return nullptr;
        }
        CompileJob* job = std::construct_at(static_cast<CompileJob*>(memory), locked);
        job->allocator = &locked;
        job->cache = config.cache;
        job->target = config.target;
        job->limit = config.maxPermutationsPerShader;
        job->requestAll = entry.requestAll;
        bool copied = job->path.TryAssign(entry.path.View());
        for (usize index = 0; copied && index < entry.requests.Size(); ++index)
        {
            copied = job->requests.TryPushBack(entry.requests[index]);
        }
        if (!copied)
        {
            DestroyJob(job);
            return nullptr;
        }
        return job;
    }

    void StartDirty() noexcept
    {
        for (usize index = 0; index < entries.Size(); ++index)
        {
            ShaderEntry& entry = *entries[index];
            if (!entry.dirty || entry.job != nullptr)
            {
                continue;
            }
            CompileJob* job = MakeJob(entry);
            if (job == nullptr)
            {
                continue;  // Retried at the next Update.
            }
            if (config.jobs == nullptr)
            {
                entry.dirty = false;
                entry.job = job;
                RunCompileJob(job);
                continue;
            }
            jobs::JobDesc desc{};
            desc.name = "shader compile";
            desc.function = &RunCompileJob;
            desc.userData = job;
            const jobs::JobHandle handle = config.jobs->Submit(desc);
            if (!handle.IsValid())
            {
                DestroyJob(job);  // Queue full or stopping; retried later.
                continue;
            }
            entry.dirty = false;
            entry.job = job;
            entry.handle = handle;
        }
    }

    void PollFiles(u64 now) noexcept
    {
        if (!config.watchFiles)
        {
            return;
        }
        const u64 interval =
            static_cast<u64>(config.pollIntervalMilliseconds) * kNanosecondsPerMillisecond;
        if (lastPoll != 0 && now - lastPoll < interval)
        {
            return;
        }
        lastPoll = now;
        static_cast<void>(watcher.Poll());
        platform::FileChange change{};
        while (watcher.PollChange(change))
        {
            for (usize index = 0; index < entries.Size(); ++index)
            {
                ShaderEntry& entry = *entries[index];
                if (entry.watchId == change.watchId)
                {
                    if (!entry.dirty)
                    {
                        entry.changeDetectedAt = now;
                    }
                    entry.dirty = true;
                }
            }
        }
    }

    memory::Allocator* upstream = nullptr;
    detail::LockedAllocator locked;
    ShaderLibraryDesc config{};
    DynamicArray<ShaderEntry*> entries;
    DynamicArray<ShaderEvent> events;
    usize eventRead = 0;
    platform::FileWatcher watcher;
    u64 lastPoll = 0;
};

ShaderLibrary::ShaderLibrary(Impl* impl) noexcept : m_impl(impl) {}

ShaderLibrary::~ShaderLibrary()
{
    if (m_impl != nullptr)
    {
        memory::Allocator* upstream = m_impl->upstream;
        std::destroy_at(m_impl);
        upstream->Deallocate(m_impl);
        m_impl = nullptr;
    }
}

ShaderLibrary::ShaderLibrary(ShaderLibrary&& other) noexcept : m_impl(other.m_impl)
{
    other.m_impl = nullptr;
}

ShaderLibrary& ShaderLibrary::operator=(ShaderLibrary&& other) noexcept
{
    if (this != &other)
    {
        ShaderLibrary discarded(std::move(*this));
        m_impl = other.m_impl;
        other.m_impl = nullptr;
    }
    return *this;
}

Result<ShaderLibrary, Error> ShaderLibrary::Create(const ShaderLibraryDesc& desc,
                                                   memory::Allocator& allocator) noexcept
{
    if (desc.maxPermutationsPerShader == 0 ||
        desc.maxPermutationsPerShader > kMaximumPermutationLimit || desc.target == nullptr ||
        desc.target[0] == '\0')
    {
        return Result<ShaderLibrary, Error>::Err(
            Error{ErrorCode::InvalidArgument,
                  "shader library: permutation limit must be 1..256 and the target must be named"});
    }
    void* memory = allocator.Allocate(sizeof(Impl), alignof(Impl));
    if (memory == nullptr)
    {
        return Result<ShaderLibrary, Error>::Err(
            Error{ErrorCode::Internal, "shader library: allocation failed"});
    }
    Impl* impl = std::construct_at(static_cast<Impl*>(memory), desc, allocator);
    return Result<ShaderLibrary, Error>::Ok(ShaderLibrary(impl));
}

Result<ShaderId, Error> ShaderLibrary::Register(StringView utf8Path) noexcept
{
    if (m_impl == nullptr || utf8Path.Empty() || utf8Path.Contains('\0'))
    {
        return Result<ShaderId, Error>::Err(
            Error{ErrorCode::InvalidArgument, "shader library: invalid shader path"});
    }
    for (usize index = 0; index < m_impl->entries.Size(); ++index)
    {
        if (m_impl->entries[index]->path.View() == utf8Path)
        {
            return Result<ShaderId, Error>::Err(
                Error{ErrorCode::AlreadyExists, "shader library: path is already registered"});
        }
    }
    void* memory = m_impl->locked.Allocate(sizeof(ShaderEntry), alignof(ShaderEntry));
    if (memory == nullptr)
    {
        return Result<ShaderId, Error>::Err(
            Error{ErrorCode::Internal, "shader library: allocation failed"});
    }
    ShaderEntry* entry = std::construct_at(static_cast<ShaderEntry*>(memory), m_impl->locked);
    const PermutationRequest base = MakeRequest(Span<const StringView>{});
    if (!entry->path.TryAssign(utf8Path) || !entry->requests.TryPushBack(base) ||
        !m_impl->entries.TryPushBack(entry))
    {
        m_impl->DestroyEntry(entry);
        return Result<ShaderId, Error>::Err(
            Error{ErrorCode::Internal, "shader library: allocation failed"});
    }
    entry->changeDetectedAt = NowNanoseconds();
    if (m_impl->config.watchFiles)
    {
        entry->watchId = m_impl->watcher.Watch(entry->path.CStr());
    }
    return Result<ShaderId, Error>::Ok(static_cast<ShaderId>(m_impl->entries.Size()));
}

Result<u64, Error> ShaderLibrary::RequestPermutation(ShaderId shader,
                                                     Span<const StringView> keywords) noexcept
{
    ShaderEntry* entry = m_impl != nullptr ? m_impl->Find(shader) : nullptr;
    if (entry == nullptr || keywords.Size() > kMaxKeywords)
    {
        return Result<u64, Error>::Err(
            Error{ErrorCode::InvalidArgument, "shader library: invalid shader or keyword count"});
    }
    for (usize index = 0; index < keywords.Size(); ++index)
    {
        if (!IsKeywordName(keywords[index]))
        {
            return Result<u64, Error>::Err(
                Error{ErrorCode::InvalidArgument, "shader library: invalid keyword name"});
        }
    }
    const PermutationRequest request = MakeRequest(keywords);
    if (HasRequest(entry->requests, request.hash))
    {
        return Result<u64, Error>::Ok(request.hash);
    }
    if (entry->requests.Size() >= m_impl->config.maxPermutationsPerShader)
    {
        return Result<u64, Error>::Err(
            Error{ErrorCode::Overflow, "shader library: permutation limit reached"});
    }
    if (!entry->requests.TryPushBack(request))
    {
        return Result<u64, Error>::Err(
            Error{ErrorCode::Internal, "shader library: allocation failed"});
    }
    if (!entry->dirty)
    {
        entry->changeDetectedAt = NowNanoseconds();
    }
    entry->dirty = true;
    return Result<u64, Error>::Ok(request.hash);
}

Status<Error> ShaderLibrary::RequestAllPermutations(ShaderId shader) noexcept
{
    ShaderEntry* entry = m_impl != nullptr ? m_impl->Find(shader) : nullptr;
    if (entry == nullptr)
    {
        return Status<Error>::Err(Error{ErrorCode::InvalidArgument, "shader library: invalid id"});
    }
    if (!entry->requestAll)
    {
        entry->requestAll = true;
        if (!entry->dirty)
        {
            entry->changeDetectedAt = NowNanoseconds();
        }
        entry->dirty = true;
    }
    return Status<Error>::Ok();
}

void ShaderLibrary::Update() noexcept
{
    if (m_impl == nullptr)
    {
        return;
    }
    const u64 now = NowNanoseconds();
    m_impl->PollFiles(now);
    m_impl->Collect(false);
    m_impl->StartDirty();
    if (m_impl->config.jobs == nullptr)
    {
        m_impl->Collect(false);
    }
}

void ShaderLibrary::Invalidate(ShaderId shader) noexcept
{
    ShaderEntry* entry = m_impl != nullptr ? m_impl->Find(shader) : nullptr;
    if (entry != nullptr)
    {
        if (!entry->dirty)
        {
            entry->changeDetectedAt = NowNanoseconds();
        }
        entry->dirty = true;
    }
}

void ShaderLibrary::WaitIdle() noexcept
{
    if (m_impl != nullptr)
    {
        m_impl->WaitAll();
    }
}

bool ShaderLibrary::PollEvent(ShaderEvent& event) noexcept
{
    if (m_impl == nullptr || m_impl->eventRead >= m_impl->events.Size())
    {
        return false;
    }
    event = m_impl->events[m_impl->eventRead++];
    return true;
}

const CompiledShader* ShaderLibrary::Find(ShaderId shader, u64 permutationHash) const noexcept
{
    const ShaderEntry* entry = m_impl != nullptr ? m_impl->Find(shader) : nullptr;
    if (entry == nullptr)
    {
        return nullptr;
    }
    for (usize index = 0; index < entry->results.Size(); ++index)
    {
        if (entry->results[index].permutation.hash == permutationHash)
        {
            return &entry->results[index];
        }
    }
    return nullptr;
}

u32 ShaderLibrary::Version(ShaderId shader) const noexcept
{
    const ShaderEntry* entry = m_impl != nullptr ? m_impl->Find(shader) : nullptr;
    return entry != nullptr ? entry->stats.version : 0;
}

const Diagnostic* ShaderLibrary::LastError(ShaderId shader) const noexcept
{
    const ShaderEntry* entry = m_impl != nullptr ? m_impl->Find(shader) : nullptr;
    return entry != nullptr && entry->hasError ? &entry->error : nullptr;
}

ShaderStats ShaderLibrary::Stats(ShaderId shader) const noexcept
{
    const ShaderEntry* entry = m_impl != nullptr ? m_impl->Find(shader) : nullptr;
    return entry != nullptr ? entry->stats : ShaderStats{};
}

StringView ShaderLibrary::Path(ShaderId shader) const noexcept
{
    const ShaderEntry* entry = m_impl != nullptr ? m_impl->Find(shader) : nullptr;
    return entry != nullptr ? entry->path.View() : StringView{};
}

u32 ShaderLibrary::ShaderCount() const noexcept
{
    return m_impl != nullptr ? static_cast<u32>(m_impl->entries.Size()) : 0;
}

bool ShaderLibrary::WritePermutationReport(String& out) const noexcept
{
    if (m_impl == nullptr)
    {
        return false;
    }
    char line[256]{};
    const auto append = [&out, &line](int written) noexcept
    {
        return written >= 0 && static_cast<usize>(written) < sizeof(line) &&
               out.TryAppend(StringView(line, static_cast<usize>(written)));
    };

    bool ok = append(std::snprintf(line, sizeof(line),
                                   "# CyberGraphics shader permutation report\n"
                                   "compiler_version %u, target %s, permutation_limit %u\n",
                                   kCompilerVersion, m_impl->config.target,
                                   m_impl->config.maxPermutationsPerShader));
    ok = ok && out.TryAppend("shader | stage | keywords | possible | requested | compiled | "
                             "compilations | cache_hits | failures | version | last_error\n");
    u64 possible = 0;
    u64 compiled = 0;
    u64 compilations = 0;
    u64 hits = 0;
    for (usize index = 0; ok && index < m_impl->entries.Size(); ++index)
    {
        const ShaderEntry& entry = *m_impl->entries[index];
        const ShaderStats& stats = entry.stats;
        ok = out.TryAppend(entry.path.View()) && out.TryAppend(" | ") &&
             out.TryAppend(StageName(stats.stage)) && out.TryAppend(" | ");
        if (entry.keywords.count == 0)
        {
            ok = ok && out.TryAppend("-");
        }
        for (u32 keyword = 0; ok && keyword < entry.keywords.count; ++keyword)
        {
            ok = (keyword == 0 || out.TryAppend(",")) &&
                 out.TryAppend(entry.keywords.names[keyword].View());
        }
        ok = ok &&
             append(std::snprintf(
                 line, sizeof(line), " | %u | %u | %u | %llu | %llu | %llu | %u | ",
                 stats.possiblePermutations, stats.requestedPermutations,
                 stats.compiledPermutations, static_cast<unsigned long long>(stats.compilations),
                 static_cast<unsigned long long>(stats.cacheHits),
                 static_cast<unsigned long long>(stats.failures), stats.version));
        if (entry.hasError)
        {
            ok = ok && append(std::snprintf(
                           line, sizeof(line), "%u:%u %s\n", entry.error.line, entry.error.column,
                           entry.error.message != nullptr ? entry.error.message : "error"));
        }
        else
        {
            ok = ok && out.TryAppend("-\n");
        }
        possible += stats.possiblePermutations;
        compiled += stats.compiledPermutations;
        compilations += stats.compilations;
        hits += stats.cacheHits;
    }
    ok = ok && append(std::snprintf(line, sizeof(line),
                                    "totals: shaders %u, possible %llu, compiled %llu, "
                                    "compilations %llu, cache_hits %llu\n",
                                    static_cast<u32>(m_impl->entries.Size()),
                                    static_cast<unsigned long long>(possible),
                                    static_cast<unsigned long long>(compiled),
                                    static_cast<unsigned long long>(compilations),
                                    static_cast<unsigned long long>(hits)));
    return ok;
}

}  // namespace cyber::shaders

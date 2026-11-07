// cyber_cooker: offline import and cooking for CyberGraphics content.
//
// The tool is a batch build step: it scans a content root, imports every source
// it understands, writes versioned cooked payloads, and records the asset
// database manifest next to them. It touches no GPU and no engine runtime, so it
// produces identical output on a build machine and on a workstation.

#include <cyber/assets/asset.h>
#include <cyber/assets/asset_database.h>
#include <cyber/assets/cooked.h>
#include <cyber/assets/gltf.h>
#include <cyber/assets/image.h>
#include <cyber/assets/obj.h>
#include <cyber/assets/texture.h>
#include <cyber/core/types.h>
#include <cyber/memory/dynamic_array.h>
#include <cyber/memory/heap_allocator.h>
#include <cyber/memory/span.h>
#include <cyber/memory/string.h>
#include <cyber/memory/string_view.h>
#include <cyber/platform/filesystem.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace
{

namespace fs = std::filesystem;

using cyber::assets::AssetDatabase;
using cyber::assets::AssetId;
using cyber::assets::AssetImportSettings;
using cyber::assets::AssetKind;
using cyber::assets::AssetRecord;
using cyber::assets::CookCacheEntry;
using cyber::assets::CookedAsset;
using cyber::assets::CookedFile;
using cyber::assets::CookMaterial;
using cyber::assets::CookMesh;
using cyber::assets::CookScene;
using cyber::assets::CookTexture;
using cyber::assets::DecodeImage;
using cyber::assets::HashSourceBytes;
using cyber::assets::ImportedScene;
using cyber::assets::ImportError;
using cyber::assets::ImportGltfFile;
using cyber::assets::ImportObjFile;
using cyber::assets::ImportResult;
using cyber::assets::ImportStatus;
using cyber::assets::kCookedTextureSlots;
using cyber::assets::kInvalidAssetId;
using cyber::assets::ParseDds;
using cyber::assets::ParseKtx2;
using cyber::assets::ResolveVirtualPath;
using cyber::assets::SourceFormat;
using cyber::assets::SourceFormatFromPath;
using cyber::assets::SourceFormatName;
using cyber::assets::TextureAsset;
using cyber::assets::TextureFormat;
using cyber::assets::TextureFromImage;
using cyber::assets::TextureLevel;
using cyber::assets::TextureLevelSize;
using cyber::core::Byte;
using cyber::core::ErrorCode;
using cyber::core::u32;
using cyber::core::u64;
using cyber::core::usize;
using cyber::memory::Allocator;
using cyber::memory::DynamicArray;
using cyber::memory::HeapAllocator;
using cyber::memory::Span;
using cyber::memory::String;
using cyber::memory::StringView;

constexpr const char* kUsage =
    "cyber_cooker - import and cook CyberGraphics assets\n"
    "\n"
    "usage: cyber_cooker --source <dir> --output <dir> [options] [<path>...]\n"
    "\n"
    "  --source <dir>     content root that virtual asset paths are relative to\n"
    "  --output <dir>     directory that receives cooked payloads and the manifest\n"
    "  --manifest <file>  manifest path (default: <output>/assets.json)\n"
    "  --stats <file>     write a JSON run summary to this path\n"
    "  --force            re-cook assets even when the cache entry verifies\n"
    "  --verbose          report reused assets and skipped references\n"
    "  --help             print this text\n"
    "\n"
    "With no paths, every known source under --source is cooked. Paths are\n"
    "relative to --source and may name single files.\n";

struct Options
{
    fs::path sourceRoot;
    fs::path outputRoot;
    fs::path manifestPath;
    fs::path statsPath;
    std::vector<std::string> inputs;
    bool force = false;
    bool verbose = false;
    bool help = false;
};

struct RunStats
{
    u32 sources = 0;
    u32 cooked = 0;
    u32 reused = 0;
    u32 failed = 0;
};

[[nodiscard]] std::string Utf8Path(const fs::path& path)
{
    const std::u8string text = path.u8string();
    return std::string(reinterpret_cast<const char*>(text.data()), text.size());
}

/// Converts a UTF-8 string to a file-system path without going through the
/// native narrow encoding, so non-ASCII names survive on Windows.
[[nodiscard]] fs::path FromUtf8(std::string_view text)
{
    return fs::path(std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size()));
}

/// Text appended before the cooked extension, so a file always has one purpose.
[[nodiscard]] const char* CookedExtension(AssetKind kind) noexcept
{
    switch (kind)
    {
    case AssetKind::Mesh:
        return ".cymesh";
    case AssetKind::Texture:
        return ".cytex";
    case AssetKind::Material:
        return ".cymat";
    case AssetKind::Scene:
        return ".cyscene";
    case AssetKind::Unknown:
        return ".cooked";
    }
    return ".cooked";
}

/// Cooked path relative to the output root, so a manifest can move with its
/// package. The whole source file name is kept: `rock.png` and `rock.dds` are
/// different assets and must never write the same cooked file.
[[nodiscard]] std::string CookedPathFor(const std::string& virtualPath, AssetKind kind,
                                        std::string_view disambiguator = {})
{
    std::string relative = Utf8Path(FromUtf8(virtualPath));
    relative.append(disambiguator);
    relative.append(CookedExtension(kind));
    return relative;
}

void Report(const ImportError& error, const std::string& path) noexcept
{
    std::fprintf(stderr, "error: %s: %s", path.c_str(),
                 error.message != nullptr ? error.message : "unknown failure");
    if (error.line != 0)
    {
        std::fprintf(stderr, " (line %u, column %u)", static_cast<unsigned>(error.line),
                     static_cast<unsigned>(error.column));
    }
    else if (error.offset != 0)
    {
        std::fprintf(stderr, " (offset %llu)", static_cast<unsigned long long>(error.offset));
    }
    std::fprintf(stderr, "\n");
}

[[nodiscard]] bool ParseOptions(int argc, char** argv, Options& options, std::string& error)
{
    for (int index = 1; index < argc; ++index)
    {
        const std::string_view argument(argv[index]);
        auto readValue = [&](fs::path& target) noexcept -> bool
        {
            if (index + 1 >= argc)
            {
                error = "missing value for ";
                error.append(argument);
                return false;
            }
            target = fs::path(argv[++index]);
            return true;
        };
        auto readText = [&](std::string& target) noexcept -> bool
        {
            if (index + 1 >= argc)
            {
                error = "missing value for ";
                error.append(argument);
                return false;
            }
            target = argv[++index];
            return true;
        };

        if (argument == "--source")
        {
            if (!readValue(options.sourceRoot))
            {
                return false;
            }
        }
        else if (argument == "--output")
        {
            if (!readValue(options.outputRoot))
            {
                return false;
            }
        }
        else if (argument == "--manifest")
        {
            if (!readValue(options.manifestPath))
            {
                return false;
            }
        }
        else if (argument == "--stats")
        {
            if (!readValue(options.statsPath))
            {
                return false;
            }
        }
        else if (argument == "--force")
        {
            options.force = true;
        }
        else if (argument == "--verbose")
        {
            options.verbose = true;
        }
        else if (argument == "--help" || argument == "-h")
        {
            options.help = true;
        }
        else if (!argument.empty() && argument.front() == '-')
        {
            error = "unknown option ";
            error.append(argument);
            return false;
        }
        else
        {
            options.inputs.emplace_back(argument);
        }
    }

    if (options.help)
    {
        return true;
    }
    if (options.sourceRoot.empty())
    {
        error = "--source is required";
        return false;
    }
    if (options.outputRoot.empty())
    {
        error = "--output is required";
        return false;
    }
    if (options.manifestPath.empty())
    {
        options.manifestPath = options.outputRoot / "assets.json";
    }
    return true;
}

/// Collects the sources to cook: explicit paths when given, otherwise a
/// recursive scan. The list is sorted so the manifest and output directory are
/// byte-identical across runs and machines.
[[nodiscard]] bool CollectSources(const Options& options, std::vector<fs::path>& files,
                                  std::string& error)
{
    std::error_code code;
    if (!fs::is_directory(options.sourceRoot, code))
    {
        error = "source root is not a directory: " + Utf8Path(options.sourceRoot);
        return false;
    }

    if (!options.inputs.empty())
    {
        for (const std::string& input : options.inputs)
        {
            const fs::path path = options.sourceRoot / FromUtf8(input);
            if (!fs::is_regular_file(path, code))
            {
                error = "input is not a file: " + input;
                return false;
            }
            files.push_back(path);
        }
    }
    else
    {
        fs::recursive_directory_iterator iterator(
            options.sourceRoot, fs::directory_options::skip_permission_denied, code);
        const fs::recursive_directory_iterator end;
        while (iterator != end)
        {
            const fs::directory_entry entry = *iterator;
            if (entry.is_regular_file(code) &&
                SourceFormatFromPath(StringView(Utf8Path(entry.path()).c_str())) !=
                    SourceFormat::Unknown)
            {
                files.push_back(entry.path());
            }
            iterator.increment(code);
            if (code)
            {
                error = "content scan failed: " + code.message();
                return false;
            }
        }
    }

    std::sort(files.begin(), files.end(), [](const fs::path& left, const fs::path& right)
              { return Utf8Path(left) < Utf8Path(right); });
    return true;
}

class Cooker
{
public:
    Cooker(const Options& options, AssetDatabase& database, HeapAllocator& allocator) noexcept
        : m_options(options), m_database(database), m_allocator(allocator)
    {
    }

    [[nodiscard]] bool CookOne(const fs::path& absolutePath, const std::string& virtualPath,
                               RunStats& stats);

private:
    [[nodiscard]] bool WriteCooked(const std::string& relativePath, const DynamicArray<Byte>& bytes,
                                   const std::string& sourcePath);

    [[nodiscard]] bool VerifyCached(AssetId id, u64 sourceHash) const;
    [[nodiscard]] bool Store(AssetId id, const std::string& relativePath, u64 sourceHash,
                             const DynamicArray<Byte>& bytes, const std::string& sourcePath);

    [[nodiscard]] bool CookMeshSource(AssetId id, const std::string& virtualPath, u64 sourceHash,
                                      const std::string& sourcePath);
    [[nodiscard]] bool CookSceneSource(AssetId id, const std::string& virtualPath, u64 sourceHash,
                                       const std::string& sourcePath);
    [[nodiscard]] bool CookTextureSource(AssetId id, const std::string& virtualPath, u64 sourceHash,
                                         SourceFormat format, const std::string& sourcePath);
    [[nodiscard]] bool CookDerivedMesh(const ImportedScene& scene, usize index, AssetId sceneId,
                                       const std::string& virtualPath, u64 sourceHash);
    [[nodiscard]] bool CookEmbeddedImage(const cyber::assets::ImportedImage& image, usize index,
                                         AssetId sceneId, const std::string& virtualPath,
                                         u64 sourceHash);

    [[nodiscard]] fs::path OutputPath(std::string_view relativePath) const
    {
        return m_options.outputRoot / FromUtf8(relativePath);
    }

    const Options& m_options;
    AssetDatabase& m_database;
    HeapAllocator& m_allocator;
    AssetImportSettings m_settings{};
};

bool Cooker::WriteCooked(const std::string& relativePath, const DynamicArray<Byte>& bytes,
                         const std::string& sourcePath)
{
    const fs::path destination = OutputPath(relativePath);
    std::error_code code;
    fs::create_directories(destination.parent_path(), code);
    if (code)
    {
        std::fprintf(stderr, "error: %s: cannot create output directory: %s\n", sourcePath.c_str(),
                     code.message().c_str());
        return false;
    }
    const auto written = cyber::platform::WriteFile(Utf8Path(destination).c_str(),
                                                    Span<const Byte>(bytes.Data(), bytes.Size()));
    if (written.IsError())
    {
        std::fprintf(stderr, "error: %s: cannot write cooked file: %s\n", sourcePath.c_str(),
                     cyber::core::ErrorCodeName(written.Error().code));
        return false;
    }
    return true;
}

bool Cooker::VerifyCached(AssetId id, u64 sourceHash) const
{
    CookCacheEntry entry{};
    if (!m_database.TryGetCooked(id, sourceHash, m_settings, entry))
    {
        return false;
    }
    const AssetRecord* record = m_database.Find(id);
    if (record == nullptr || !record->IsCooked())
    {
        return false;
    }
    // The manifest alone is not proof: the payload is re-read and its checksum,
    // id, source hash, and settings are all compared before a cook is skipped.
    auto file =
        CookedFile::Load(Utf8Path(OutputPath(record->cookedPath.View())).c_str(), m_allocator);
    if (file.IsError())
    {
        if (m_options.verbose)
        {
            std::fprintf(stderr, "note: %s: cached payload did not load: %s\n",
                         record->virtualPath.CStr(),
                         file.Error().message != nullptr ? file.Error().message : "unknown");
        }
        return false;
    }
    const CookedAsset& asset = file.Value().Asset();
    const u64 settingsHash = cyber::assets::ImportSettingsHash(m_settings);
    if (asset.Id() == id && asset.SourceHash() == sourceHash &&
        asset.PayloadChecksum() == entry.checksum && asset.SettingsHash() == settingsHash)
    {
        return true;
    }
    if (m_options.verbose)
    {
        std::fprintf(stderr,
                     "note: %s: cached payload does not match (id %s, source %s, checksum "
                     "%s, settings %s)\n",
                     record->virtualPath.CStr(), asset.Id() == id ? "ok" : "differs",
                     asset.SourceHash() == sourceHash ? "ok" : "differs",
                     asset.PayloadChecksum() == entry.checksum ? "ok" : "differs",
                     asset.SettingsHash() == settingsHash ? "ok" : "differs");
    }
    return false;
}

bool Cooker::Store(AssetId id, const std::string& relativePath, u64 sourceHash,
                   const DynamicArray<Byte>& bytes, const std::string& sourcePath)
{
    // Verify the payload before it reaches disk: a broken cooked file must never
    // be published, even transiently.
    const auto parsed = CookedAsset::FromBytes(Span<const Byte>(bytes.Data(), bytes.Size()));
    if (parsed.IsError())
    {
        Report(parsed.Error(), sourcePath);
        return false;
    }
    if (!WriteCooked(relativePath, bytes, sourcePath))
    {
        return false;
    }
    const ImportStatus stored = m_database.StoreCooked(
        id, relativePath.c_str(), sourceHash, m_settings, parsed.Value().PayloadChecksum());
    if (stored.IsError())
    {
        Report(stored.Error(), sourcePath);
        return false;
    }
    return true;
}

bool Cooker::CookMeshSource(AssetId id, const std::string& virtualPath, u64 sourceHash,
                            const std::string& sourcePath)
{
    auto imported = ImportObjFile(Utf8Path(m_options.sourceRoot / FromUtf8(virtualPath)).c_str(),
                                  m_allocator, m_settings);
    if (imported.IsError())
    {
        Report(imported.Error(), sourcePath);
        return false;
    }
    if (!imported.Value().mesh.name.TryAssign(StringView(virtualPath)))
    {
        std::fprintf(stderr, "error: %s: out of memory\n", sourcePath.c_str());
        return false;
    }
    const ImportStatus validated = imported.Value().mesh.Validate();
    if (validated.IsError())
    {
        Report(validated.Error(), sourcePath);
        return false;
    }

    auto cooked = CookMesh(imported.Value().mesh, id, sourceHash, m_settings, m_allocator);
    if (cooked.IsError())
    {
        Report(cooked.Error(), sourcePath);
        return false;
    }
    if (!Store(id, CookedPathFor(virtualPath, AssetKind::Mesh), sourceHash, cooked.Value(),
               sourcePath))
    {
        return false;
    }

    // OBJ only names materials and libraries; neither is a cooked source format
    // in this phase, so the mesh keeps its material indices without edges.
    if (m_options.verbose && !imported.Value().dependencies.Empty())
    {
        std::fprintf(stderr, "note: %s: %zu material library reference(s) not cooked\n",
                     sourcePath.c_str(), imported.Value().dependencies.Size());
    }
    return true;
}

bool Cooker::CookSceneSource(AssetId id, const std::string& virtualPath, u64 sourceHash,
                             const std::string& sourcePath)
{
    cyber::assets::GltfImportOptions gltfOptions{};
    gltfOptions.settings = m_settings;
    gltfOptions.documentName = virtualPath.c_str();

    auto imported = ImportGltfFile(Utf8Path(m_options.sourceRoot / FromUtf8(virtualPath)).c_str(),
                                   m_allocator, gltfOptions);
    if (imported.IsError())
    {
        Report(imported.Error(), sourcePath);
        return false;
    }
    const ImportedScene& scene = imported.Value();

    auto cooked = CookScene(scene, id, sourceHash, m_settings, m_allocator, virtualPath.c_str());
    if (cooked.IsError())
    {
        Report(cooked.Error(), sourcePath);
        return false;
    }
    if (!Store(id, CookedPathFor(virtualPath, AssetKind::Scene), sourceHash, cooked.Value(),
               sourcePath))
    {
        return false;
    }

    for (usize index = 0; index < scene.meshes.Size(); ++index)
    {
        if (!CookDerivedMesh(scene, index, id, virtualPath, sourceHash))
        {
            return false;
        }
    }

    DynamicArray<AssetId> dependencies(m_allocator);
    for (usize index = 0; index < scene.images.Size(); ++index)
    {
        const cyber::assets::ImportedImage& image = scene.images[index];
        if (image.uri.Empty())
        {
            if (!CookEmbeddedImage(image, index, id, virtualPath, sourceHash))
            {
                return false;
            }
            continue;
        }

        // External images are their own source assets; the scene just records
        // that it references them, so a texture change invalidates the scene.
        String resolved(m_allocator);
        if (!ResolveVirtualPath(StringView(virtualPath), image.uri.View(), resolved))
        {
            std::fprintf(stderr, "error: %s: image URI escapes the content root\n",
                         sourcePath.c_str());
            return false;
        }
        const AssetRecord* target = m_database.Find(resolved.View());
        if (target == nullptr)
        {
            if (m_options.verbose)
            {
                std::fprintf(stderr, "note: %s: image %.*s lives outside the cooked content root\n",
                             sourcePath.c_str(), static_cast<int>(resolved.View().Size()),
                             resolved.View().Data());
            }
            continue;
        }
        if (!dependencies.TryPushBack(target->id))
        {
            std::fprintf(stderr, "error: %s: out of memory for dependencies\n", sourcePath.c_str());
            return false;
        }
    }

    // Replacing, not appending, is what keeps a removed image reference from
    // surviving in the manifest across cooks.
    const ImportStatus linked = m_database.SetDependencies(
        id, Span<const AssetId>(dependencies.Data(), dependencies.Size()));
    if (linked.IsError())
    {
        Report(linked.Error(), sourcePath);
        return false;
    }
    return true;
}

bool Cooker::CookDerivedMesh(const ImportedScene& scene, usize index, AssetId sceneId,
                             const std::string& virtualPath, u64 sourceHash)
{
    char suffix[32] = {};
    const int written = std::snprintf(suffix, sizeof(suffix), "#mesh%zu", index);
    if (written <= 0 || static_cast<usize>(written) >= sizeof(suffix))
    {
        std::fprintf(stderr, "error: %s: too many meshes for a stable name\n", virtualPath.c_str());
        return false;
    }
    std::string derivedPath = virtualPath;
    derivedPath.append(suffix);

    const auto registered = m_database.RegisterDerived(StringView(derivedPath), AssetKind::Mesh);
    if (registered.IsError())
    {
        Report(registered.Error(), derivedPath);
        return false;
    }
    const AssetId sceneDependency[] = {sceneId};
    const ImportStatus linked =
        m_database.SetDependencies(registered.Value(), Span<const AssetId>(sceneDependency, 1));
    if (linked.IsError())
    {
        Report(linked.Error(), derivedPath);
        return false;
    }

    auto cooked =
        CookMesh(scene.meshes[index], registered.Value(), sourceHash, m_settings, m_allocator);
    if (cooked.IsError())
    {
        Report(cooked.Error(), derivedPath);
        return false;
    }

    const std::string fileSuffix = std::string("_mesh") + std::to_string(index);
    return Store(registered.Value(), CookedPathFor(virtualPath, AssetKind::Mesh, fileSuffix),
                 sourceHash, cooked.Value(), derivedPath);
}

bool Cooker::CookEmbeddedImage(const cyber::assets::ImportedImage& image, usize index,
                               AssetId sceneId, const std::string& virtualPath, u64 sourceHash)
{
    auto decoded =
        DecodeImage(Span<const Byte>(image.bytes.Data(), image.bytes.Size()), m_allocator);
    if (decoded.IsError())
    {
        Report(decoded.Error(), virtualPath);
        return false;
    }

    char suffix[32] = {};
    const int written = std::snprintf(suffix, sizeof(suffix), "#image%zu", index);
    if (written <= 0 || static_cast<usize>(written) >= sizeof(suffix))
    {
        std::fprintf(stderr, "error: %s: too many images for a stable name\n", virtualPath.c_str());
        return false;
    }
    std::string derivedPath = virtualPath;
    derivedPath.append(suffix);
    auto converted = TextureFromImage(decoded.Value(), StringView(derivedPath), m_allocator);
    if (converted.IsError())
    {
        Report(converted.Error(), virtualPath);
        return false;
    }
    TextureAsset texture = std::move(converted.Value());

    const auto registered = m_database.RegisterDerived(StringView(derivedPath), AssetKind::Texture);
    if (registered.IsError())
    {
        Report(registered.Error(), derivedPath);
        return false;
    }
    const AssetId sceneDependency[] = {sceneId};
    const ImportStatus linked =
        m_database.SetDependencies(registered.Value(), Span<const AssetId>(sceneDependency, 1));
    if (linked.IsError())
    {
        Report(linked.Error(), derivedPath);
        return false;
    }

    auto cooked = CookTexture(texture, registered.Value(), sourceHash, m_settings, m_allocator);
    if (cooked.IsError())
    {
        Report(cooked.Error(), virtualPath);
        return false;
    }
    const std::string fileSuffix = std::string("_image") + std::to_string(index);
    return Store(registered.Value(), CookedPathFor(virtualPath, AssetKind::Texture, fileSuffix),
                 sourceHash, cooked.Value(), derivedPath);
}

bool Cooker::CookTextureSource(AssetId id, const std::string& virtualPath, u64 sourceHash,
                               SourceFormat format, const std::string& sourcePath)
{
    auto file = cyber::platform::ReadFile(
        Utf8Path(m_options.sourceRoot / FromUtf8(virtualPath)).c_str(), m_allocator);
    if (file.IsError())
    {
        std::fprintf(stderr, "error: %s: %s\n", sourcePath.c_str(),
                     file.Error().message != nullptr ? file.Error().message : "read failed");
        return false;
    }
    const Span<const Byte> bytes(file.Value().Data(), file.Value().Size());

    auto texture = TextureAsset(m_allocator);
    if (format == SourceFormat::Png || format == SourceFormat::Jpeg)
    {
        auto decoded = DecodeImage(bytes, m_allocator);
        if (decoded.IsError())
        {
            Report(decoded.Error(), sourcePath);
            return false;
        }
        auto converted = TextureFromImage(decoded.Value(), StringView(virtualPath), m_allocator);
        if (converted.IsError())
        {
            Report(converted.Error(), sourcePath);
            return false;
        }
        texture = std::move(converted.Value());
    }
    else
    {
        auto parsed = format == SourceFormat::Dds ? ParseDds(bytes, m_allocator)
                                                  : ParseKtx2(bytes, m_allocator);
        if (parsed.IsError())
        {
            Report(parsed.Error(), sourcePath);
            return false;
        }
        texture = std::move(parsed.Value());
        if (!texture.name.TryAssign(StringView(virtualPath)))
        {
            std::fprintf(stderr, "error: %s: out of memory for a texture\n", sourcePath.c_str());
            return false;
        }
    }

    const ImportStatus validated = texture.Validate();
    if (validated.IsError())
    {
        Report(validated.Error(), sourcePath);
        return false;
    }

    auto cooked = CookTexture(texture, id, sourceHash, m_settings, m_allocator);
    if (cooked.IsError())
    {
        Report(cooked.Error(), sourcePath);
        return false;
    }
    return Store(id, CookedPathFor(virtualPath, AssetKind::Texture), sourceHash, cooked.Value(),
                 sourcePath);
}

bool Cooker::CookOne(const fs::path& absolutePath, const std::string& virtualPath, RunStats& stats)
{
    auto file = cyber::platform::ReadFile(Utf8Path(absolutePath).c_str(), m_allocator);
    if (file.IsError())
    {
        std::fprintf(stderr, "error: %s: %s\n", virtualPath.c_str(),
                     file.Error().message != nullptr ? file.Error().message : "read failed");
        return false;
    }
    const u64 sourceHash =
        HashSourceBytes(Span<const Byte>(file.Value().Data(), file.Value().Size()));

    auto registered = m_database.Register(StringView(virtualPath));
    if (registered.IsError())
    {
        Report(registered.Error(), virtualPath);
        return false;
    }
    const AssetId id = registered.Value();

    if (!m_options.force && VerifyCached(id, sourceHash))
    {
        ++stats.reused;
        if (m_options.verbose)
        {
            std::printf("reused  %s\n", virtualPath.c_str());
        }
        return true;
    }

    const SourceFormat format = SourceFormatFromPath(StringView(virtualPath));
    if (format == SourceFormat::CyberMaterial)
    {
        std::fprintf(stderr,
                     "error: %s: .cybermat sources are not readable yet; cook materials from "
                     "their glTF document\n",
                     virtualPath.c_str());
        return false;
    }

    bool ok = false;
    switch (format)
    {
    case SourceFormat::Gltf:
    case SourceFormat::Glb:
        ok = CookSceneSource(id, virtualPath, sourceHash, virtualPath);
        break;
    case SourceFormat::Obj:
        ok = CookMeshSource(id, virtualPath, sourceHash, virtualPath);
        break;
    case SourceFormat::Png:
    case SourceFormat::Jpeg:
    case SourceFormat::Dds:
    case SourceFormat::Ktx2:
        ok = CookTextureSource(id, virtualPath, sourceHash, format, virtualPath);
        break;
    case SourceFormat::CyberMaterial:
        ok = false;
        break;
    case SourceFormat::Unknown:
        std::fprintf(stderr, "error: %s: source format is not supported\n", virtualPath.c_str());
        ok = false;
        break;
    }

    if (!ok)
    {
        // A failed cook must not leave a stale entry that the next run trusts.
        m_database.Invalidate(id);
        return false;
    }
    ++stats.cooked;
    if (m_options.verbose)
    {
        std::printf("cooked  %s\n", virtualPath.c_str());
    }
    return true;
}

[[nodiscard]] bool WriteStats(const Options& options, const RunStats& stats)
{
    if (options.statsPath.empty())
    {
        return true;
    }
    char text[256] = {};
    const int written = std::snprintf(
        text, sizeof(text),
        "{\n  \"sources\": %u,\n  \"cooked\": %u,\n  \"reused\": %u,\n  \"failed\": %u\n}\n",
        static_cast<unsigned>(stats.sources), static_cast<unsigned>(stats.cooked),
        static_cast<unsigned>(stats.reused), static_cast<unsigned>(stats.failed));
    if (written <= 0)
    {
        return false;
    }
    std::error_code code;
    const fs::path parent = options.statsPath.parent_path();
    if (!parent.empty())
    {
        fs::create_directories(parent, code);
        if (code)
        {
            return false;
        }
    }
    return cyber::platform::WriteFile(
               Utf8Path(options.statsPath).c_str(),
               Span<const Byte>(reinterpret_cast<const Byte*>(text), static_cast<usize>(written)))
        .IsOk();
}

}  // namespace

int main(int argc, char** argv)
{
    Options options;
    std::string error;
    if (!ParseOptions(argc, argv, options, error))
    {
        std::fprintf(stderr, "cyber_cooker: %s\n\n%s", error.c_str(), kUsage);
        return 2;
    }
    if (options.help)
    {
        std::printf("%s", kUsage);
        return 0;
    }

    std::vector<fs::path> files;
    if (!CollectSources(options, files, error))
    {
        std::fprintf(stderr, "cyber_cooker: %s\n", error.c_str());
        return 2;
    }

    std::error_code code;
    fs::create_directories(options.outputRoot, code);
    if (code)
    {
        std::fprintf(stderr, "cyber_cooker: cannot create output root: %s\n",
                     code.message().c_str());
        return 2;
    }

    HeapAllocator allocator;
    AssetDatabase database(allocator);
    if (fs::exists(options.manifestPath, code))
    {
        const ImportStatus loaded = database.Load(Utf8Path(options.manifestPath).c_str());
        if (loaded.IsError())
        {
            // Starting over is correct: the manifest is a cache and never the
            // only copy of content. The warning makes the loss visible.
            std::fprintf(stderr, "warning: %s: %s; rebuilding the database from scratch\n",
                         Utf8Path(options.manifestPath).c_str(),
                         loaded.Error().message != nullptr ? loaded.Error().message : "invalid");
            database.Clear();
        }
    }

    RunStats stats;
    stats.sources = static_cast<u32>(files.size());
    Cooker cooker(options, database, allocator);

    for (const fs::path& file : files)
    {
        std::error_code relativeCode;
        const fs::path relativePath = fs::relative(file, options.sourceRoot, relativeCode);
        if (relativeCode)
        {
            std::fprintf(stderr, "error: %s: %s\n", Utf8Path(file).c_str(),
                         relativeCode.message().c_str());
            ++stats.failed;
            continue;
        }
        const std::string virtualPath = relativePath.generic_string();
        if (!cooker.CookOne(file, virtualPath, stats))
        {
            ++stats.failed;
        }
    }

    const ImportStatus saved = database.Save(Utf8Path(options.manifestPath).c_str());
    if (saved.IsError())
    {
        Report(saved.Error(), Utf8Path(options.manifestPath));
        ++stats.failed;
    }

    if (!WriteStats(options, stats))
    {
        std::fprintf(stderr, "error: cannot write stats file %s\n",
                     Utf8Path(options.statsPath).c_str());
        ++stats.failed;
    }

    std::printf("cooked %u, reused %u, failed %u, sources %u\n",
                static_cast<unsigned>(stats.cooked), static_cast<unsigned>(stats.reused),
                static_cast<unsigned>(stats.failed), static_cast<unsigned>(stats.sources));
    return stats.failed == 0 ? 0 : 1;
}

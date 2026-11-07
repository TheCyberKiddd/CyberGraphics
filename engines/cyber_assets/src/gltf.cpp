#include <cyber/assets/gltf.h>
#include <cyber/assets/image.h>
#include <cyber/assets/json.h>
#include <cyber/core/hash.h>
#include <cyber/math/quat.h>
#include <cyber/math/transform.h>
#include <cyber/platform/filesystem.h>

#include <bit>
#include <cstdio>
#include <cstring>

namespace cyber::assets
{
namespace
{

using cyber::core::ErrorCode;
using cyber::core::f32;
using cyber::core::f64;
using cyber::core::u16;
using cyber::core::u32;
using cyber::core::u64;
using cyber::core::u8;
using cyber::core::usize;
using cyber::math::Mat4;
using cyber::math::Quat;
using cyber::math::Transform;
using cyber::math::Vec2;
using cyber::math::Vec3;
using cyber::memory::StringView;

constexpr u32 kGlbMagic = 0x46546c67u;  // "glTF", little-endian
constexpr u32 kGlbVersion = 2;
constexpr u32 kChunkJson = 0x4e4f534au;
constexpr u32 kChunkBinary = 0x004e4942u;
constexpr u32 kMaxImageBytes = 256u * 1024u * 1024u;

[[nodiscard]] u32 ReadLittleEndian32(const Byte* data) noexcept
{
    return static_cast<u32>(std::to_integer<u8>(data[0])) |
           (static_cast<u32>(std::to_integer<u8>(data[1])) << 8u) |
           (static_cast<u32>(std::to_integer<u8>(data[2])) << 16u) |
           (static_cast<u32>(std::to_integer<u8>(data[3])) << 24u);
}

[[nodiscard]] u32 ComponentSize(u32 componentType) noexcept
{
    switch (componentType)
    {
    case 5120:  // BYTE
    case 5121:  // UNSIGNED_BYTE
        return 1;
    case 5122:  // SHORT
    case 5123:  // UNSIGNED_SHORT
        return 2;
    case 5125:  // UNSIGNED_INT
    case 5126:  // FLOAT
        return 4;
    default:
        return 0;
    }
}

[[nodiscard]] u32 ComponentCount(StringView type) noexcept
{
    if (type.Size() == 6 && std::memcmp(type.Data(), "SCALAR", 6) == 0)
    {
        return 1;
    }
    if (type.Size() == 4 && std::memcmp(type.Data(), "VEC2", 4) == 0)
    {
        return 2;
    }
    if (type.Size() == 4 && std::memcmp(type.Data(), "VEC3", 4) == 0)
    {
        return 3;
    }
    if (type.Size() == 4 && std::memcmp(type.Data(), "VEC4", 4) == 0)
    {
        return 4;
    }
    return 0;
}

[[nodiscard]] bool ReadFloatComponent(const Byte* data, u32 componentType, bool normalized,
                                      f32& value) noexcept
{
    switch (componentType)
    {
    case 5126:
    {
        const u32 bits = ReadLittleEndian32(data);
        value = std::bit_cast<f32>(bits);
        return true;
    }
    case 5120:
    {
        const auto raw = static_cast<cyber::core::i8>(std::to_integer<u8>(data[0]));
        value = normalized ? static_cast<f32>(raw) / 127.0f : static_cast<f32>(raw);
        return true;
    }
    case 5121:
    {
        const u32 raw = std::to_integer<u8>(data[0]);
        value = normalized ? static_cast<f32>(raw) / 255.0f : static_cast<f32>(raw);
        return true;
    }
    case 5122:
    {
        const u16 raw = static_cast<u16>(static_cast<u32>(std::to_integer<u8>(data[0])) |
                                         (static_cast<u32>(std::to_integer<u8>(data[1])) << 8u));
        const auto signedRaw = static_cast<cyber::core::i16>(raw);
        value = normalized ? static_cast<f32>(signedRaw) / 32767.0f : static_cast<f32>(signedRaw);
        return true;
    }
    case 5123:
    {
        const u32 raw = static_cast<u32>(std::to_integer<u8>(data[0])) |
                        (static_cast<u32>(std::to_integer<u8>(data[1])) << 8u);
        value = normalized ? static_cast<f32>(raw) / 65535.0f : static_cast<f32>(raw);
        return true;
    }
    case 5125:
    {
        const u32 raw = ReadLittleEndian32(data);
        value = normalized ? static_cast<f32>(raw) / 4294967295.0f : static_cast<f32>(raw);
        return true;
    }
    default:
        return false;
    }
}

[[nodiscard]] bool ReadUnsignedComponent(const Byte* data, u32 componentType, u32& value) noexcept
{
    switch (componentType)
    {
    case 5121:
        value = std::to_integer<u8>(data[0]);
        return true;
    case 5123:
        value = static_cast<u32>(std::to_integer<u8>(data[0])) |
                (static_cast<u32>(std::to_integer<u8>(data[1])) << 8u);
        return true;
    case 5125:
        value = ReadLittleEndian32(data);
        return true;
    default:
        return false;
    }
}

[[nodiscard]] bool DecodeBase64(StringView input, DynamicArray<Byte>& output) noexcept
{
    auto valueOf = [](char character) noexcept -> i32
    {
        if (character >= 'A' && character <= 'Z')
        {
            return character - 'A';
        }
        if (character >= 'a' && character <= 'z')
        {
            return 26 + (character - 'a');
        }
        if (character >= '0' && character <= '9')
        {
            return 52 + (character - '0');
        }
        if (character == '+' || character == '-')
        {
            return 62;
        }
        if (character == '/' || character == '_')
        {
            return 63;
        }
        return -1;
    };

    output.Clear();
    u32 accumulator = 0;
    u32 bitCount = 0;
    usize index = 0;
    for (; index < input.Size(); ++index)
    {
        const char character = input[index];
        if (character == '=')
        {
            break;
        }
        if (character == '\n' || character == '\r' || character == ' ' || character == '\t')
        {
            continue;
        }
        const i32 value = valueOf(character);
        if (value < 0)
        {
            return false;
        }
        accumulator = (accumulator << 6u) | static_cast<u32>(value);
        bitCount += 6;
        if (bitCount >= 8)
        {
            bitCount -= 8;
            const auto byte = static_cast<Byte>((accumulator >> bitCount) & 0xffu);
            if (!output.TryPushBack(byte))
            {
                return false;
            }
        }
    }
    // Remaining '=' padding must be the only thing left and must be valid.
    for (; index < input.Size(); ++index)
    {
        const char character = input[index];
        if (character != '=' && character != '\n' && character != '\r' && character != ' ' &&
            character != '\t')
        {
            return false;
        }
    }
    return true;
}

/// One resolved accessor: a strided view over a buffer view.
struct AccessorView
{
    const Byte* data = nullptr;
    usize stride = 0;
    u32 componentType = 0;
    u32 components = 0;
    u32 count = 0;
    bool normalized = false;
};

class GltfLoader
{
public:
    GltfLoader(Allocator& allocator, const JsonDocument& document,
               cyber::memory::Span<const Byte> binary, const char* baseDirectory,
               const GltfImportOptions& options) noexcept
        : m_allocator(allocator), m_document(document), m_binary(binary),
          m_baseDirectory(baseDirectory == nullptr ? "" : baseDirectory), m_options(options),
          m_scene(allocator), m_buffers(allocator), m_ownedBuffers(allocator),
          m_imageIds(allocator), m_childFirst(allocator), m_childCount(allocator),
          m_children(allocator)
    {
    }

    [[nodiscard]] ImportResult<ImportedScene> Run() noexcept;

private:
    [[nodiscard]] ImportError Error(ErrorCode code, const char* message) const noexcept
    {
        return MakeError(code, message);
    }

    [[nodiscard]] bool LoadBuffers(ImportError& error) noexcept;
    [[nodiscard]] bool LoadImages(ImportError& error) noexcept;
    [[nodiscard]] bool LoadMaterials(ImportError& error) noexcept;
    [[nodiscard]] bool LoadMeshes(ImportError& error) noexcept;
    [[nodiscard]] bool LoadNodes(ImportError& error) noexcept;

    [[nodiscard]] const JsonValue Array(const char* name) const noexcept;
    [[nodiscard]] bool Accessor(u32 index, AccessorView& view, ImportError& error) const noexcept;
    [[nodiscard]] bool BufferViewBytes(u32 index, cyber::memory::Span<const Byte>& bytes,
                                       ImportError& error) const noexcept;
    [[nodiscard]] bool ResolveTexture(u32 textureIndex, u32 texCoord, TextureReference& reference,
                                      ImportError& error) const noexcept;
    [[nodiscard]] bool ResolveImage(const JsonValue& image, usize imageIndex,
                                    ImportError& error) noexcept;
    [[nodiscard]] bool VisitNode(u32 index, const Mat4& parentWorld, u32 depth,
                                 DynamicArray<u8>& state, ImportError& error) noexcept;
    [[nodiscard]] bool ReadTransform(const JsonValue& node, Mat4& matrix,
                                     ImportError& error) const noexcept;
    [[nodiscard]] bool ReadMeshVertexAttributes(const JsonValue& primitive, u32 vertexIndex,
                                                MeshVertex& vertex,
                                                ImportError& error) const noexcept;

    Allocator& m_allocator;
    const JsonDocument& m_document;
    cyber::memory::Span<const Byte> m_binary;
    const char* m_baseDirectory;
    GltfImportOptions m_options;
    ImportedScene m_scene;
    DynamicArray<cyber::memory::Span<const Byte>> m_buffers;
    DynamicArray<DynamicArray<Byte>> m_ownedBuffers;
    DynamicArray<AssetId> m_imageIds;
    DynamicArray<u32> m_childFirst;
    DynamicArray<u32> m_childCount;
    DynamicArray<u32> m_children;
};

const JsonValue GltfLoader::Array(const char* name) const noexcept
{
    const JsonValue value = m_document.Root().Member(StringView(name));
    return value.IsArray() ? value : JsonValue();
}

bool GltfLoader::LoadBuffers(ImportError& error) noexcept
{
    const JsonValue buffers = Array("buffers");
    for (usize index = 0; index < buffers.Size(); ++index)
    {
        const JsonValue buffer = buffers.Element(index);
        if (!buffer.IsObject())
        {
            error = Error(ErrorCode::ParseError, "glTF buffer entry is not an object");
            return false;
        }
        if (!m_ownedBuffers.TryEmplaceBack(m_allocator))
        {
            error = Error(ErrorCode::OutOfRange, "out of memory for glTF buffers");
            return false;
        }
        DynamicArray<Byte>& storage = m_ownedBuffers[m_ownedBuffers.Size() - 1];

        StringView uri;
        if (buffer.Member("uri").TryGetString(uri))
        {
            if (uri.StartsWith(StringView("data:")))
            {
                const usize comma = [&]() noexcept
                {
                    for (usize position = 0; position < uri.Size(); ++position)
                    {
                        if (uri[position] == ',')
                        {
                            return position;
                        }
                    }
                    return uri.Size();
                }();
                const StringView header = uri.Substr(0, comma);
                if (comma >= uri.Size() || !header.EndsWith(StringView(";base64")))
                {
                    error = Error(ErrorCode::Unsupported, "glTF buffer data URI is not base64");
                    return false;
                }
                if (!DecodeBase64(uri.Substr(comma + 1), storage))
                {
                    error = Error(ErrorCode::ParseError, "glTF buffer data URI is malformed");
                    return false;
                }
            }
            else
            {
                if (uri.Size() >= 512)
                {
                    error = Error(ErrorCode::OutOfRange, "glTF buffer URI is too long");
                    return false;
                }
                char path[1024] = {};
                const usize baseLength = std::strlen(m_baseDirectory);
                if (baseLength + uri.Size() + 2 >= sizeof(path))
                {
                    error = Error(ErrorCode::OutOfRange, "glTF buffer path is too long");
                    return false;
                }
                if (baseLength > 0)
                {
                    std::memcpy(path, m_baseDirectory, baseLength);
                    usize cursor = baseLength;
                    if (path[cursor - 1] != '/' && path[cursor - 1] != '\\')
                    {
                        path[cursor++] = '/';
                    }
                    std::memcpy(path + cursor, uri.Data(), uri.Size());
                }
                else
                {
                    std::memcpy(path, uri.Data(), uri.Size());
                }
                auto file = cyber::platform::ReadFile(path, m_allocator);
                if (file.IsError())
                {
                    error = Error(file.Error().code, "glTF buffer file could not be read");
                    return false;
                }
                storage = std::move(file.Value());
            }
        }
        else if (index == 0 && !m_binary.Empty())
        {
            if (!m_buffers.TryPushBack(m_binary))
            {
                error = Error(ErrorCode::OutOfRange, "out of memory for glTF buffers");
                return false;
            }
            continue;
        }
        else
        {
            error = Error(ErrorCode::ParseError, "glTF buffer has neither a URI nor binary data");
            return false;
        }

        if (!m_buffers.TryPushBack(cyber::memory::Span<const Byte>(storage.Data(), storage.Size())))
        {
            error = Error(ErrorCode::OutOfRange, "out of memory for glTF buffers");
            return false;
        }
    }
    return true;
}

bool GltfLoader::BufferViewBytes(u32 index, cyber::memory::Span<const Byte>& bytes,
                                 ImportError& error) const noexcept
{
    const JsonValue views = Array("bufferViews");
    if (index >= views.Size())
    {
        error = Error(ErrorCode::OutOfRange, "glTF buffer view index is out of range");
        return false;
    }
    const JsonValue view = views.Element(index);
    u32 bufferIndex = 0;
    u32 byteOffset = 0;
    u32 byteLength = 0;
    if (!view.Member("buffer").TryGetU32(bufferIndex) ||
        !view.Member("byteLength").TryGetU32(byteLength))
    {
        error = Error(ErrorCode::ParseError, "glTF buffer view is missing its buffer or length");
        return false;
    }
    const JsonValue offsetValue = view.Member("byteOffset");
    if (offsetValue.IsValid() && !offsetValue.TryGetU32(byteOffset))
    {
        error = Error(ErrorCode::ParseError, "glTF buffer view offset is invalid");
        return false;
    }
    if (bufferIndex >= m_buffers.Size())
    {
        error = Error(ErrorCode::OutOfRange, "glTF buffer view references a missing buffer");
        return false;
    }
    const cyber::memory::Span<const Byte> buffer = m_buffers[bufferIndex];
    if (byteOffset > buffer.Size() || byteLength > buffer.Size() - byteOffset)
    {
        error = Error(ErrorCode::ParseError, "glTF buffer view overruns its buffer");
        return false;
    }
    bytes = buffer.Subspan(byteOffset).First(byteLength);
    return true;
}

bool GltfLoader::Accessor(u32 index, AccessorView& view, ImportError& error) const noexcept
{
    const JsonValue accessors = Array("accessors");
    if (index >= accessors.Size())
    {
        error = Error(ErrorCode::OutOfRange, "glTF accessor index is out of range");
        return false;
    }
    const JsonValue accessor = accessors.Element(index);
    u32 componentType = 0;
    u32 count = 0;
    StringView type;
    if (!accessor.IsObject() || !accessor.Member("componentType").TryGetU32(componentType) ||
        !accessor.Member("count").TryGetU32(count) || !accessor.Member("type").TryGetString(type))
    {
        error = Error(ErrorCode::ParseError, "glTF accessor is missing a required field");
        return false;
    }
    if (accessor.HasMember("sparse"))
    {
        error = Error(ErrorCode::Unsupported, "glTF sparse accessors are not supported");
        return false;
    }
    const u32 components = ComponentCount(type);
    const u32 componentSize = ComponentSize(componentType);
    if (components == 0 || componentSize == 0)
    {
        error = Error(ErrorCode::Unsupported, "glTF accessor type is unsupported");
        return false;
    }

    view = AccessorView{};
    view.componentType = componentType;
    view.components = components;
    view.count = count;
    const JsonValue normalizedValue = accessor.Member("normalized");
    if (normalizedValue.IsValid())
    {
        bool normalized = false;
        if (!normalizedValue.TryGetBool(normalized))
        {
            error = Error(ErrorCode::ParseError, "glTF accessor normalized flag is invalid");
            return false;
        }
        view.normalized = normalized;
    }

    const JsonValue viewValue = accessor.Member("bufferView");
    if (!viewValue.IsValid())
    {
        // A missing buffer view means the accessor is defined as zeros.
        view.stride = componentSize * components;
        return true;
    }
    u32 bufferViewIndex = 0;
    if (!viewValue.TryGetU32(bufferViewIndex))
    {
        error = Error(ErrorCode::ParseError, "glTF accessor buffer view is invalid");
        return false;
    }
    cyber::memory::Span<const Byte> bytes;
    if (!BufferViewBytes(bufferViewIndex, bytes, error))
    {
        return false;
    }

    u32 accessorOffset = 0;
    const JsonValue offsetValue = accessor.Member("byteOffset");
    if (offsetValue.IsValid() && !offsetValue.TryGetU32(accessorOffset))
    {
        error = Error(ErrorCode::ParseError, "glTF accessor byte offset is invalid");
        return false;
    }
    u32 stride = componentSize * components;
    const JsonValue strideValue =
        Array("bufferViews").Element(bufferViewIndex).Member("byteStride");
    if (strideValue.IsValid())
    {
        if (!strideValue.TryGetU32(stride))
        {
            error = Error(ErrorCode::ParseError, "glTF buffer view stride is invalid");
            return false;
        }
        if (stride < componentSize * components)
        {
            error = Error(ErrorCode::ParseError, "glTF buffer view stride is too small");
            return false;
        }
    }
    if (stride == 0)
    {
        error = Error(ErrorCode::ParseError, "glTF accessor stride is zero");
        return false;
    }

    const u64 lastByte = static_cast<u64>(accessorOffset) +
                         static_cast<u64>(count > 0 ? count - 1 : 0) * stride +
                         static_cast<u64>(componentSize) * components;
    if (count > 0 && lastByte > bytes.Size())
    {
        error = Error(ErrorCode::ParseError, "glTF accessor overruns its buffer view");
        return false;
    }
    view.data = bytes.Data() + accessorOffset;
    view.stride = stride;
    return true;
}

bool GltfLoader::ResolveImage(const JsonValue& image, usize imageIndex, ImportError& error) noexcept
{
    if (!image.IsObject())
    {
        error = Error(ErrorCode::ParseError, "glTF image entry is not an object");
        return false;
    }

    ImportedImage imported(m_allocator);
    StringView uri;
    StringView mimeType;
    const JsonValue mimeValue = image.Member("mimeType");
    if (mimeValue.IsValid() && !mimeValue.TryGetString(mimeType))
    {
        error = Error(ErrorCode::ParseError, "glTF image mime type is invalid");
        return false;
    }

    if (image.Member("uri").TryGetString(uri))
    {
        if (uri.StartsWith(StringView("data:")))
        {
            const usize comma = [&]() noexcept
            {
                for (usize position = 0; position < uri.Size(); ++position)
                {
                    if (uri[position] == ',')
                    {
                        return position;
                    }
                }
                return uri.Size();
            }();
            if (comma >= uri.Size())
            {
                error = Error(ErrorCode::ParseError, "glTF image data URI is malformed");
                return false;
            }
            const StringView header = uri.Substr(0, comma);
            if (!header.EndsWith(StringView(";base64")))
            {
                error = Error(ErrorCode::Unsupported, "glTF image data URI is not base64");
                return false;
            }
            if (!DecodeBase64(uri.Substr(comma + 1), imported.bytes))
            {
                error = Error(ErrorCode::ParseError, "glTF image data URI is malformed");
                return false;
            }
            const StringView declared = header.Substr(5);
            const usize semicolon = [&]() noexcept
            {
                for (usize position = 0; position < declared.Size(); ++position)
                {
                    if (declared[position] == ';')
                    {
                        return position;
                    }
                }
                return declared.Size();
            }();
            if (!imported.mimeType.TryAssign(declared.Substr(0, semicolon)))
            {
                error = Error(ErrorCode::OutOfRange, "out of memory for glTF image data");
                return false;
            }
            char synthetic[128] = {};
            const usize documentLength = std::strlen(m_options.documentName);
            if (documentLength + 32 >= sizeof(synthetic))
            {
                error = Error(ErrorCode::OutOfRange, "glTF document name is too long");
                return false;
            }
            std::memcpy(synthetic, m_options.documentName, documentLength);
            usize cursor = documentLength;
            synthetic[cursor++] = '#';
            synthetic[cursor++] = 'i';
            synthetic[cursor++] = 'm';
            synthetic[cursor++] = 'a';
            synthetic[cursor++] = 'g';
            synthetic[cursor++] = 'e';
            if (imageIndex >= 100000)
            {
                error = Error(ErrorCode::OutOfRange, "glTF has too many embedded images");
                return false;
            }
            char digits[8] = {};
            u32 digitCount = 0;
            usize value = imageIndex;
            do
            {
                digits[digitCount++] = static_cast<char>('0' + (value % 10));
                value /= 10;
            } while (value != 0 && digitCount < sizeof(digits));
            while (digitCount > 0)
            {
                synthetic[cursor++] = digits[--digitCount];
            }
            synthetic[cursor] = '\0';
            imported.id = AssetIdFromPath(StringView(synthetic, cursor));
        }
        else
        {
            if (!imported.uri.TryAssign(uri))
            {
                error = Error(ErrorCode::OutOfRange, "out of memory for glTF image URI");
                return false;
            }
            // The identity of an external image is its virtual path, not the
            // raw URI: `../textures/x.png` from `models/scene.gltf` must hash
            // like the `textures/x.png` asset the cooker registers.
            cyber::memory::String resolved(m_allocator);
            if (!ResolveVirtualPath(m_options.documentName, uri, resolved))
            {
                error =
                    Error(ErrorCode::InvalidArgument, "glTF image URI escapes the document root");
                return false;
            }
            imported.id = AssetIdFromPath(resolved.View());

            char path[1024] = {};
            const usize baseLength = std::strlen(m_baseDirectory);
            if (baseLength + uri.Size() + 2 >= sizeof(path))
            {
                error = Error(ErrorCode::OutOfRange, "glTF image path is too long");
                return false;
            }
            usize cursor = 0;
            if (baseLength > 0)
            {
                std::memcpy(path, m_baseDirectory, baseLength);
                cursor = baseLength;
                if (path[cursor - 1] != '/' && path[cursor - 1] != '\\')
                {
                    path[cursor++] = '/';
                }
            }
            std::memcpy(path + cursor, uri.Data(), uri.Size());
            auto file = cyber::platform::ReadFile(path, m_allocator);
            if (file.IsError())
            {
                error = Error(file.Error().code, "glTF image file could not be read");
                return false;
            }
            imported.bytes = std::move(file.Value());
        }
    }
    else
    {
        u32 bufferViewIndex = 0;
        if (!image.Member("bufferView").TryGetU32(bufferViewIndex))
        {
            error = Error(ErrorCode::ParseError, "glTF image has neither a URI nor a buffer view");
            return false;
        }
        cyber::memory::Span<const Byte> bytes;
        if (!BufferViewBytes(bufferViewIndex, bytes, error))
        {
            return false;
        }
        if (!imported.bytes.TryResize(bytes.Size()))
        {
            error = Error(ErrorCode::OutOfRange, "out of memory for glTF image data");
            return false;
        }
        if (!bytes.Empty())
        {
            std::memcpy(imported.bytes.Data(), bytes.Data(), bytes.Size());
        }
        if (!imported.mimeType.TryAssign(mimeType))
        {
            error = Error(ErrorCode::OutOfRange, "out of memory for glTF image data");
            return false;
        }
        char synthetic[128] = {};
        const usize documentLength = std::strlen(m_options.documentName);
        if (documentLength + 24 >= sizeof(synthetic))
        {
            error = Error(ErrorCode::OutOfRange, "glTF document name is too long");
            return false;
        }
        const int written = std::snprintf(synthetic, sizeof(synthetic), "%s#image%zu",
                                          m_options.documentName, imageIndex);
        if (written <= 0)
        {
            error = Error(ErrorCode::Internal, "glTF image name could not be built");
            return false;
        }
        imported.id = AssetIdFromPath(StringView(synthetic, static_cast<usize>(written)));
    }

    if (imported.bytes.Empty() || imported.bytes.Size() > kMaxImageBytes)
    {
        error = Error(ErrorCode::OutOfRange, "glTF image payload size is invalid");
        return false;
    }
    if (IdentifyImage(cyber::memory::Span<const Byte>(
            imported.bytes.Data(), imported.bytes.Size())) == ImageFileFormat::Unknown)
    {
        error = Error(ErrorCode::Unsupported, "glTF image is not PNG or JPEG");
        return false;
    }
    if (!m_imageIds.TryPushBack(imported.id))
    {
        error = Error(ErrorCode::OutOfRange, "out of memory for glTF images");
        return false;
    }
    if (!m_scene.images.TryPushBack(std::move(imported)))
    {
        error = Error(ErrorCode::OutOfRange, "out of memory for glTF images");
        return false;
    }
    return true;
}

bool GltfLoader::LoadImages(ImportError& error) noexcept
{
    const JsonValue images = Array("images");
    for (usize index = 0; index < images.Size(); ++index)
    {
        if (!ResolveImage(images.Element(index), index, error))
        {
            return false;
        }
    }
    return true;
}

bool GltfLoader::ResolveTexture(u32 textureIndex, u32 texCoord, TextureReference& reference,
                                ImportError& error) const noexcept
{
    reference = TextureReference{};
    const JsonValue textures = Array("textures");
    if (textureIndex >= textures.Size())
    {
        error = Error(ErrorCode::OutOfRange, "glTF texture index is out of range");
        return false;
    }
    const JsonValue texture = textures.Element(textureIndex);
    u32 source = 0;
    if (!texture.Member("source").TryGetU32(source))
    {
        error = Error(ErrorCode::Unsupported, "glTF texture has no image source");
        return false;
    }
    if (source >= m_imageIds.Size())
    {
        error = Error(ErrorCode::OutOfRange, "glTF texture references a missing image");
        return false;
    }
    reference.image = m_imageIds[source];
    reference.texCoord = texCoord;
    u32 sampler = 0;
    const JsonValue samplerValue = texture.Member("sampler");
    if (samplerValue.IsValid() && !samplerValue.TryGetU32(sampler))
    {
        error = Error(ErrorCode::ParseError, "glTF texture sampler index is invalid");
        return false;
    }
    reference.sampler = sampler;
    return true;
}

bool GltfLoader::LoadMaterials(ImportError& error) noexcept
{
    const JsonValue materials = Array("materials");
    for (usize index = 0; index < materials.Size(); ++index)
    {
        const JsonValue materialJson = materials.Element(index);
        if (!materialJson.IsObject())
        {
            error = Error(ErrorCode::ParseError, "glTF material entry is not an object");
            return false;
        }
        if (!m_scene.materials.TryEmplaceBack(m_allocator))
        {
            error = Error(ErrorCode::OutOfRange, "out of memory for glTF materials");
            return false;
        }
        MaterialAsset& material = m_scene.materials[m_scene.materials.Size() - 1];
        // glTF exposes no strength for the emissive slot; the value is parsed and discarded.
        f32 emissiveScale = 1.0f;

        StringView name;
        if (materialJson.Member("name").TryGetString(name) && !material.name.TryAssign(name))
        {
            error = Error(ErrorCode::OutOfRange, "out of memory for glTF names");
            return false;
        }

        const JsonValue pbr = materialJson.Member("pbrMetallicRoughness");
        if (pbr.IsObject())
        {
            const JsonValue color = pbr.Member("baseColorFactor");
            if (color.IsArray() && color.Size() == 4)
            {
                f32 components[4] = {};
                for (usize component = 0; component < 4; ++component)
                {
                    f64 value = 0.0;
                    if (!color.Element(component).TryGetNumber(value))
                    {
                        error =
                            Error(ErrorCode::ParseError, "glTF base color factor is not numeric");
                        return false;
                    }
                    components[component] = static_cast<f32>(value);
                }
                material.baseColorFactor =
                    cyber::math::Vec4{components[0], components[1], components[2], components[3]};
            }

            f64 number = 0.0;
            if (pbr.Member("metallicFactor").TryGetNumber(number))
            {
                material.metallicFactor = static_cast<f32>(number);
            }
            if (pbr.Member("roughnessFactor").TryGetNumber(number))
            {
                material.roughnessFactor = static_cast<f32>(number);
            }
            const JsonValue baseColorTexture = pbr.Member("baseColorTexture");
            if (baseColorTexture.IsObject())
            {
                u32 textureIndex = 0;
                u32 texCoord = 0;
                if (!baseColorTexture.Member("index").TryGetU32(textureIndex) ||
                    !ResolveTexture(textureIndex, texCoord, material.baseColorTexture, error))
                {
                    if (error.message == nullptr)
                    {
                        error =
                            Error(ErrorCode::ParseError, "glTF base color texture is malformed");
                    }
                    return false;
                }
                const JsonValue texCoordValue = baseColorTexture.Member("texCoord");
                if (texCoordValue.IsValid() &&
                    !texCoordValue.TryGetU32(material.baseColorTexture.texCoord))
                {
                    error = Error(ErrorCode::ParseError, "glTF texture coordinate is invalid");
                    return false;
                }
            }
        }

        const JsonValue emissive = materialJson.Member("emissiveFactor");
        if (emissive.IsArray() && emissive.Size() == 3)
        {
            f32 components[3] = {};
            for (usize component = 0; component < 3; ++component)
            {
                f64 value = 0.0;
                if (!emissive.Element(component).TryGetNumber(value))
                {
                    error = Error(ErrorCode::ParseError, "glTF emissive factor is not numeric");
                    return false;
                }
                components[component] = static_cast<f32>(value);
            }
            material.emissiveFactor = Vec3{components[0], components[1], components[2]};
        }

        auto resolveSlot = [&](const char* slotName, TextureReference& slot, const char* scaleName,
                               f32& scale) noexcept
        {
            const JsonValue slotJson = materialJson.Member(StringView(slotName));
            if (!slotJson.IsObject())
            {
                return true;
            }
            u32 textureIndex = 0;
            if (!slotJson.Member("index").TryGetU32(textureIndex))
            {
                error = Error(ErrorCode::ParseError, "glTF texture slot has no index");
                return false;
            }
            if (!ResolveTexture(textureIndex, 0, slot, error))
            {
                return false;
            }
            const JsonValue texCoordValue = slotJson.Member("texCoord");
            if (texCoordValue.IsValid() && !texCoordValue.TryGetU32(slot.texCoord))
            {
                error = Error(ErrorCode::ParseError, "glTF texture coordinate is invalid");
                return false;
            }
            f64 number = 0.0;
            if (slotJson.Member(StringView(scaleName)).TryGetNumber(number))
            {
                scale = static_cast<f32>(number);
            }
            return true;
        };

        if (!resolveSlot("normalTexture", material.normalTexture, "scale", material.normalScale) ||
            !resolveSlot("occlusionTexture", material.occlusionTexture, "strength",
                         material.occlusionStrength) ||
            !resolveSlot("emissiveTexture", material.emissiveTexture, "scale", emissiveScale))
        {
            return false;
        }

        StringView alphaMode;
        if (materialJson.Member("alphaMode").TryGetString(alphaMode))
        {
            if (alphaMode.Size() == 4 && std::memcmp(alphaMode.Data(), "MASK", 4) == 0)
            {
                material.alphaMode = AlphaMode::Mask;
            }
            else if (alphaMode.Size() == 5 && std::memcmp(alphaMode.Data(), "BLEND", 5) == 0)
            {
                material.alphaMode = AlphaMode::Blend;
            }
            else if (alphaMode.Size() == 6 && std::memcmp(alphaMode.Data(), "OPAQUE", 6) == 0)
            {
                material.alphaMode = AlphaMode::Opaque;
            }
            else
            {
                error = Error(ErrorCode::ParseError, "glTF alpha mode is invalid");
                return false;
            }
        }

        f64 number = 0.0;
        if (materialJson.Member("alphaCutoff").TryGetNumber(number))
        {
            material.alphaCutoff = static_cast<f32>(number);
        }
        bool doubleSided = false;
        if (materialJson.Member("doubleSided").IsValid())
        {
            if (!materialJson.Member("doubleSided").TryGetBool(doubleSided))
            {
                error = Error(ErrorCode::ParseError, "glTF double-sided flag is invalid");
                return false;
            }
            material.doubleSided = doubleSided;
        }

        auto validated = material.Validate();
        if (validated.IsError())
        {
            error = validated.Error();
            return false;
        }
    }
    return true;
}

bool GltfLoader::ReadMeshVertexAttributes(const JsonValue& primitive, u32 vertexIndex,
                                          MeshVertex& vertex, ImportError& error) const noexcept
{
    const JsonValue attributes = primitive.Member("attributes");
    if (!attributes.IsObject())
    {
        error = Error(ErrorCode::ParseError, "glTF primitive has no attributes");
        return false;
    }

    auto readAccessor = [&](StringView name, AccessorView& view) noexcept
    {
        const JsonValue value = attributes.Member(name);
        if (!value.IsValid())
        {
            return false;
        }
        u32 accessorIndex = 0;
        if (!value.TryGetU32(accessorIndex))
        {
            error = Error(ErrorCode::ParseError, "glTF attribute accessor index is invalid");
            return false;
        }
        return Accessor(accessorIndex, view, error);
    };

    auto componentAt = [](const AccessorView& view, u32 element, u32 component, f32& value) noexcept
    {
        if (view.data == nullptr)
        {
            value = 0.0f;
            return true;
        }
        return ReadFloatComponent(view.data + element * view.stride +
                                      component * ComponentSize(view.componentType),
                                  view.componentType, view.normalized, value);
    };

    AccessorView positions;
    if (!readAccessor(StringView("POSITION"), positions))
    {
        if (error.message == nullptr)
        {
            error = Error(ErrorCode::ParseError, "glTF primitive has no POSITION attribute");
        }
        return false;
    }
    if (positions.components != 3 || positions.componentType != 5126)
    {
        error = Error(ErrorCode::Unsupported, "glTF POSITION must be a float VEC3");
        return false;
    }

    f32 values[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    for (u32 component = 0; component < 3; ++component)
    {
        if (!componentAt(positions, vertexIndex, component, values[component]))
        {
            error = Error(ErrorCode::ParseError, "glTF POSITION data is invalid");
            return false;
        }
    }
    vertex.position = Vec3{values[0], values[1], values[2]};

    AccessorView normals;
    if (readAccessor(StringView("NORMAL"), normals))
    {
        if (normals.components != 3)
        {
            error = Error(ErrorCode::ParseError, "glTF NORMAL is not a VEC3");
            return false;
        }
        for (u32 component = 0; component < 3; ++component)
        {
            if (!componentAt(normals, vertexIndex, component, values[component]))
            {
                error = Error(ErrorCode::ParseError, "glTF NORMAL data is invalid");
                return false;
            }
        }
        const Vec3 normal{values[0], values[1], values[2]};
        const f32 length = cyber::math::Length(normal);
        vertex.normal = length > 0.0f ? normal / length : Vec3{0.0f, 0.0f, 1.0f};
    }

    AccessorView tangents;
    if (readAccessor(StringView("TANGENT"), tangents))
    {
        if (tangents.components != 4)
        {
            error = Error(ErrorCode::ParseError, "glTF TANGENT is not a VEC4");
            return false;
        }
        for (u32 component = 0; component < 4; ++component)
        {
            if (!componentAt(tangents, vertexIndex, component, values[component]))
            {
                error = Error(ErrorCode::ParseError, "glTF TANGENT data is invalid");
                return false;
            }
        }
        vertex.tangent = PackedFloat4(values[0], values[1], values[2], values[3]);
    }

    AccessorView uv0;
    if (readAccessor(StringView("TEXCOORD_0"), uv0))
    {
        if (uv0.components != 2)
        {
            error = Error(ErrorCode::ParseError, "glTF TEXCOORD_0 is not a VEC2");
            return false;
        }
        if (!componentAt(uv0, vertexIndex, 0, values[0]) ||
            !componentAt(uv0, vertexIndex, 1, values[1]))
        {
            error = Error(ErrorCode::ParseError, "glTF TEXCOORD_0 data is invalid");
            return false;
        }
        vertex.texCoord0 =
            Vec2{values[0], m_options.settings.flipTexCoordV ? 1.0f - values[1] : values[1]};
    }

    AccessorView uv1;
    if (readAccessor(StringView("TEXCOORD_1"), uv1))
    {
        if (uv1.components != 2)
        {
            error = Error(ErrorCode::ParseError, "glTF TEXCOORD_1 is not a VEC2");
            return false;
        }
        if (!componentAt(uv1, vertexIndex, 0, values[0]) ||
            !componentAt(uv1, vertexIndex, 1, values[1]))
        {
            error = Error(ErrorCode::ParseError, "glTF TEXCOORD_1 data is invalid");
            return false;
        }
        vertex.texCoord1 = Vec2{values[0], values[1]};
    }

    AccessorView colors;
    if (readAccessor(StringView("COLOR_0"), colors))
    {
        if (colors.components != 3 && colors.components != 4)
        {
            error = Error(ErrorCode::ParseError, "glTF COLOR_0 is not a VEC3 or VEC4");
            return false;
        }
        for (u32 component = 0; component < colors.components; ++component)
        {
            if (!componentAt(colors, vertexIndex, component, values[component]))
            {
                error = Error(ErrorCode::ParseError, "glTF COLOR_0 data is invalid");
                return false;
            }
        }
        vertex.color = PackedFloat4(values[0], values[1], values[2],
                                    colors.components == 4 ? values[3] : 1.0f);
    }

    AccessorView joints;
    if (readAccessor(StringView("JOINTS_0"), joints))
    {
        if (joints.components != 4)
        {
            error = Error(ErrorCode::ParseError, "glTF JOINTS_0 is not a VEC4");
            return false;
        }
        for (u32 component = 0; component < 4; ++component)
        {
            if (joints.data == nullptr)
            {
                vertex.joints[component] = 0;
                continue;
            }
            u32 value = 0;
            if (!ReadUnsignedComponent(joints.data + vertexIndex * joints.stride +
                                           component * ComponentSize(joints.componentType),
                                       joints.componentType, value) ||
                value > 0xffffu)
            {
                error = Error(ErrorCode::ParseError, "glTF JOINTS_0 data is invalid");
                return false;
            }
            vertex.joints[component] = static_cast<u16>(value);
        }
    }

    AccessorView weights;
    if (readAccessor(StringView("WEIGHTS_0"), weights))
    {
        if (weights.components != 4)
        {
            error = Error(ErrorCode::ParseError, "glTF WEIGHTS_0 is not a VEC4");
            return false;
        }
        f32 sum = 0.0f;
        for (u32 component = 0; component < 4; ++component)
        {
            if (!componentAt(weights, vertexIndex, component, values[component]))
            {
                error = Error(ErrorCode::ParseError, "glTF WEIGHTS_0 data is invalid");
                return false;
            }
            vertex.weights[component] = values[component];
            sum += values[component];
        }
        if (sum > 0.0f)
        {
            for (f32& weight : vertex.weights)
            {
                weight /= sum;
            }
        }
        else
        {
            vertex.weights[0] = 1.0f;
        }
    }

    return true;
}

bool GltfLoader::LoadMeshes(ImportError& error) noexcept
{
    const JsonValue meshes = Array("meshes");
    for (usize meshIndex = 0; meshIndex < meshes.Size(); ++meshIndex)
    {
        const JsonValue meshJson = meshes.Element(meshIndex);
        const JsonValue primitives = meshJson.Member("primitives");
        if (!meshJson.IsObject() || !primitives.IsArray() || primitives.Size() == 0)
        {
            error = Error(ErrorCode::ParseError, "glTF mesh has no primitives");
            return false;
        }
        if (!m_scene.meshes.TryEmplaceBack(m_allocator))
        {
            error = Error(ErrorCode::OutOfRange, "out of memory for glTF meshes");
            return false;
        }
        MeshAsset& mesh = m_scene.meshes[m_scene.meshes.Size() - 1];

        StringView name;
        if (meshJson.Member("name").TryGetString(name))
        {
            if (!mesh.name.TryAssign(name))
            {
                error = Error(ErrorCode::OutOfRange, "out of memory for glTF mesh names");
                return false;
            }
        }
        else
        {
            char generated[32] = {};
            const int written = std::snprintf(generated, sizeof(generated), "mesh%zu", meshIndex);
            if (written <= 0 ||
                !mesh.name.TryAssign(StringView(generated, static_cast<usize>(written))))
            {
                error = Error(ErrorCode::OutOfRange, "out of memory for glTF mesh names");
                return false;
            }
        }

        bool anyPrimitiveWithoutTangents = false;
        for (usize primitiveIndex = 0; primitiveIndex < primitives.Size(); ++primitiveIndex)
        {
            const JsonValue primitive = primitives.Element(primitiveIndex);
            u32 mode = 4;
            const JsonValue modeValue = primitive.Member("mode");
            if (modeValue.IsValid() && !modeValue.TryGetU32(mode))
            {
                error = Error(ErrorCode::ParseError, "glTF primitive mode is invalid");
                return false;
            }
            if (mode != 4)
            {
                error = Error(ErrorCode::Unsupported, "glTF primitive is not a triangle list");
                return false;
            }

            AccessorView positions;
            const JsonValue positionValue = primitive.Member("attributes").Member("POSITION");
            u32 positionAccessor = 0;
            if (!positionValue.TryGetU32(positionAccessor) ||
                !Accessor(positionAccessor, positions, error))
            {
                if (error.message == nullptr)
                {
                    error =
                        Error(ErrorCode::ParseError, "glTF primitive has no POSITION attribute");
                }
                return false;
            }
            if (positions.count == 0)
            {
                error = Error(ErrorCode::ParseError, "glTF primitive has no vertices");
                return false;
            }
            if (positions.count > 0xffffffffull - mesh.vertices.Size())
            {
                error = Error(ErrorCode::Overflow, "glTF mesh has too many vertices");
                return false;
            }

            const u32 vertexOffset = static_cast<u32>(mesh.vertices.Size());
            if (!mesh.vertices.TryReserve(mesh.vertices.Size() + positions.count))
            {
                error = Error(ErrorCode::OutOfRange, "out of memory for glTF vertices");
                return false;
            }
            for (u32 vertexIndex = 0; vertexIndex < positions.count; ++vertexIndex)
            {
                MeshVertex vertex;
                if (!ReadMeshVertexAttributes(primitive, vertexIndex, vertex, error))
                {
                    return false;
                }
                if (!mesh.vertices.TryPushBack(vertex))
                {
                    error = Error(ErrorCode::OutOfRange, "out of memory for glTF vertices");
                    return false;
                }
            }

            u32 indexStart = static_cast<u32>(mesh.indices.Size());
            const JsonValue indexValue = primitive.Member("indices");
            if (indexValue.IsValid())
            {
                u32 indexAccessor = 0;
                if (!indexValue.TryGetU32(indexAccessor))
                {
                    error = Error(ErrorCode::ParseError, "glTF index accessor is invalid");
                    return false;
                }
                AccessorView indices;
                if (!Accessor(indexAccessor, indices, error))
                {
                    return false;
                }
                if (indices.components != 1 || indices.normalized ||
                    (indices.componentType != 5121 && indices.componentType != 5123 &&
                     indices.componentType != 5125))
                {
                    error =
                        Error(ErrorCode::Unsupported, "glTF indices must be 8/16/32-bit integers");
                    return false;
                }
                if (indices.count % 3 != 0)
                {
                    error = Error(ErrorCode::ParseError, "glTF index count is not a triangle list");
                    return false;
                }
                if (!mesh.indices.TryReserve(mesh.indices.Size() + indices.count))
                {
                    error = Error(ErrorCode::OutOfRange, "out of memory for glTF indices");
                    return false;
                }
                for (u32 index = 0; index < indices.count; ++index)
                {
                    u32 value = 0;
                    if (indices.data == nullptr ||
                        !ReadUnsignedComponent(indices.data + index * indices.stride,
                                               indices.componentType, value))
                    {
                        error = Error(ErrorCode::ParseError, "glTF index data is invalid");
                        return false;
                    }
                    if (value >= positions.count)
                    {
                        error =
                            Error(ErrorCode::ParseError, "glTF index references a missing vertex");
                        return false;
                    }
                    if (!mesh.indices.TryPushBack(vertexOffset + value))
                    {
                        error = Error(ErrorCode::OutOfRange, "out of memory for glTF indices");
                        return false;
                    }
                }
            }
            else
            {
                if (positions.count % 3 != 0)
                {
                    error = Error(ErrorCode::ParseError,
                                  "glTF non-indexed primitive is not a triangle list");
                    return false;
                }
                if (!mesh.indices.TryReserve(mesh.indices.Size() + positions.count))
                {
                    error = Error(ErrorCode::OutOfRange, "out of memory for glTF indices");
                    return false;
                }
                for (u32 index = 0; index < positions.count; ++index)
                {
                    if (!mesh.indices.TryPushBack(vertexOffset + index))
                    {
                        error = Error(ErrorCode::OutOfRange, "out of memory for glTF indices");
                        return false;
                    }
                }
            }

            SubmeshAsset submesh;
            submesh.firstIndex = indexStart;
            submesh.indexCount = static_cast<u32>(mesh.indices.Size()) - indexStart;
            u32 materialIndex = 0;
            const JsonValue materialValue = primitive.Member("material");
            if (materialValue.IsValid() && !materialValue.TryGetU32(materialIndex))
            {
                error = Error(ErrorCode::ParseError, "glTF material index is invalid");
                return false;
            }
            const bool materialMissing = m_scene.materials.Empty()
                                             ? materialIndex != 0
                                             : materialIndex >= m_scene.materials.Size();
            if (materialMissing)
            {
                error =
                    Error(ErrorCode::OutOfRange, "glTF primitive references a missing material");
                return false;
            }
            submesh.materialIndex = materialIndex;
            if (!mesh.submeshes.TryPushBack(submesh))
            {
                error = Error(ErrorCode::OutOfRange, "out of memory for glTF submeshes");
                return false;
            }

            if (!primitive.Member("attributes").Member("TANGENT").IsValid())
            {
                anyPrimitiveWithoutTangents = true;
            }
        }

        // Tangents are generated once for the whole mesh. Regenerating inside
        // the primitive loop would re-walk every earlier primitive's triangles.
        if (m_options.settings.generateTangents && anyPrimitiveWithoutTangents)
        {
            auto tangents = GenerateTangents(mesh, m_allocator);
            if (tangents.IsError())
            {
                error = tangents.Error();
                return false;
            }
        }

        auto validated = mesh.Validate();
        if (validated.IsError())
        {
            error = validated.Error();
            return false;
        }
    }
    return true;
}

bool GltfLoader::ReadTransform(const JsonValue& node, Mat4& matrix,
                               ImportError& error) const noexcept
{
    const JsonValue matrixValue = node.Member("matrix");
    if (matrixValue.IsArray())
    {
        if (matrixValue.Size() != 16)
        {
            error = Error(ErrorCode::ParseError, "glTF node matrix must have 16 entries");
            return false;
        }
        f32 values[16] = {};
        for (usize index = 0; index < 16; ++index)
        {
            f64 value = 0.0;
            if (!matrixValue.Element(index).TryGetNumber(value))
            {
                error = Error(ErrorCode::ParseError, "glTF node matrix is not numeric");
                return false;
            }
            values[index] = static_cast<f32>(value);
        }
        matrix = Mat4{cyber::math::Vec4{values[0], values[1], values[2], values[3]},
                      cyber::math::Vec4{values[4], values[5], values[6], values[7]},
                      cyber::math::Vec4{values[8], values[9], values[10], values[11]},
                      cyber::math::Vec4{values[12], values[13], values[14], values[15]}};
        return true;
    }

    Transform transform = Transform::Identity();
    auto readVector = [&](const char* name, f32* components, usize count) noexcept
    {
        const JsonValue value = node.Member(StringView(name));
        if (!value.IsValid())
        {
            return true;
        }
        if (!value.IsArray() || value.Size() != count)
        {
            error = Error(ErrorCode::ParseError, "glTF node transform has the wrong size");
            return false;
        }
        for (usize index = 0; index < count; ++index)
        {
            f64 number = 0.0;
            if (!value.Element(index).TryGetNumber(number))
            {
                error = Error(ErrorCode::ParseError, "glTF node transform is not numeric");
                return false;
            }
            components[index] = static_cast<f32>(number);
        }
        return true;
    };

    f32 translation[3] = {0.0f, 0.0f, 0.0f};
    f32 rotation[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    f32 scale[3] = {1.0f, 1.0f, 1.0f};
    if (!readVector("translation", translation, 3) || !readVector("rotation", rotation, 4) ||
        !readVector("scale", scale, 3))
    {
        return false;
    }
    transform.translation = Vec3{translation[0], translation[1], translation[2]};
    transform.rotation = Quat{rotation[0], rotation[1], rotation[2], rotation[3]};
    transform.scale = Vec3{scale[0], scale[1], scale[2]};
    matrix = transform.ToMatrix();
    return true;
}

bool GltfLoader::VisitNode(u32 index, const Mat4& parentWorld, u32 depth, DynamicArray<u8>& state,
                           ImportError& error) noexcept
{
    if (index >= m_scene.nodes.Size())
    {
        error = Error(ErrorCode::OutOfRange, "glTF child references a missing node");
        return false;
    }
    if (depth > m_options.maxNodes)
    {
        error = Error(ErrorCode::OutOfRange, "glTF node hierarchy is too deep");
        return false;
    }
    if (state[index] == 1)
    {
        error = Error(ErrorCode::ParseError, "glTF node hierarchy contains a cycle");
        return false;
    }
    if (state[index] == 2)
    {
        return true;
    }
    state[index] = 1;

    ImportedNode& node = m_scene.nodes[index];
    node.world = parentWorld * node.local;

    if (node.mesh >= 0)
    {
        ImportedInstance instance;
        instance.node = index;
        instance.mesh = static_cast<u32>(node.mesh);
        instance.transform = node.world;
        if (!m_scene.instances.TryPushBack(instance))
        {
            error = Error(ErrorCode::OutOfRange, "out of memory for glTF instances");
            return false;
        }
    }

    const u32 first = m_childFirst[index];
    const u32 count = m_childCount[index];
    for (u32 child = 0; child < count; ++child)
    {
        const u32 childIndex = m_children[first + child];
        m_scene.nodes[childIndex].parent = index;
        m_scene.nodes[childIndex].hasParent = true;
        if (!VisitNode(childIndex, node.world, depth + 1, state, error))
        {
            return false;
        }
    }

    state[index] = 2;
    return true;
}

bool GltfLoader::LoadNodes(ImportError& error) noexcept
{
    const JsonValue nodes = Array("nodes");
    if (nodes.Size() > m_options.maxNodes)
    {
        error = Error(ErrorCode::OutOfRange, "glTF has too many nodes");
        return false;
    }
    if (!m_childFirst.TryResize(nodes.Size()) || !m_childCount.TryResize(nodes.Size()))
    {
        error = Error(ErrorCode::OutOfRange, "out of memory for glTF nodes");
        return false;
    }

    for (usize index = 0; index < nodes.Size(); ++index)
    {
        const JsonValue nodeJson = nodes.Element(index);
        if (!nodeJson.IsObject())
        {
            error = Error(ErrorCode::ParseError, "glTF node entry is not an object");
            return false;
        }
        if (!m_scene.nodes.TryEmplaceBack(m_allocator))
        {
            error = Error(ErrorCode::OutOfRange, "out of memory for glTF nodes");
            return false;
        }
        ImportedNode& node = m_scene.nodes[m_scene.nodes.Size() - 1];

        StringView name;
        if (nodeJson.Member("name").TryGetString(name) && !node.name.TryAssign(name))
        {
            error = Error(ErrorCode::OutOfRange, "out of memory for glTF names");
            return false;
        }

        const JsonValue meshValue = nodeJson.Member("mesh");
        if (meshValue.IsValid())
        {
            u32 meshIndex = 0;
            if (!meshValue.TryGetU32(meshIndex))
            {
                error = Error(ErrorCode::ParseError, "glTF node mesh index is invalid");
                return false;
            }
            if (meshIndex >= m_scene.meshes.Size())
            {
                error = Error(ErrorCode::OutOfRange, "glTF node references a missing mesh");
                return false;
            }
            node.mesh = static_cast<i32>(meshIndex);
        }

        if (!ReadTransform(nodeJson, node.local, error))
        {
            return false;
        }

        m_childFirst[index] = static_cast<u32>(m_children.Size());
        const JsonValue children = nodeJson.Member("children");
        u32 childCount = 0;
        if (children.IsArray())
        {
            for (usize child = 0; child < children.Size(); ++child)
            {
                u32 childIndex = 0;
                if (!children.Element(child).TryGetU32(childIndex) || childIndex >= nodes.Size())
                {
                    error = Error(ErrorCode::ParseError, "glTF child node index is invalid");
                    return false;
                }
                if (!m_children.TryPushBack(childIndex))
                {
                    error = Error(ErrorCode::OutOfRange, "out of memory for glTF children");
                    return false;
                }
                ++childCount;
            }
        }
        m_childCount[index] = childCount;
    }

    DynamicArray<u8> state(m_allocator);
    if (!state.TryResize(nodes.Size()))
    {
        error = Error(ErrorCode::OutOfRange, "out of memory for glTF node state");
        return false;
    }
    for (usize index = 0; index < state.Size(); ++index)
    {
        state[index] = 0;
    }

    const Mat4 identity = Mat4::Identity();
    const JsonValue scenes = Array("scenes");
    const JsonValue sceneValue = m_document.Root().Member("scene");
    u32 sceneIndex = 0;
    if (sceneValue.IsValid() && !sceneValue.TryGetU32(sceneIndex))
    {
        error = Error(ErrorCode::ParseError, "glTF default scene index is invalid");
        return false;
    }
    if (scenes.IsArray() && scenes.Size() > 0)
    {
        if (sceneIndex >= scenes.Size())
        {
            error = Error(ErrorCode::OutOfRange, "glTF default scene is out of range");
            return false;
        }
        const JsonValue roots = scenes.Element(sceneIndex).Member("nodes");
        if (roots.IsArray())
        {
            for (usize root = 0; root < roots.Size(); ++root)
            {
                u32 rootIndex = 0;
                if (!roots.Element(root).TryGetU32(rootIndex))
                {
                    error = Error(ErrorCode::ParseError, "glTF scene root index is invalid");
                    return false;
                }
                if (!VisitNode(rootIndex, identity, 0, state, error))
                {
                    return false;
                }
            }
        }
    }
    else
    {
        for (usize index = 0; index < m_scene.nodes.Size(); ++index)
        {
            if (!m_scene.nodes[index].hasParent)
            {
                if (!VisitNode(static_cast<u32>(index), identity, 0, state, error))
                {
                    return false;
                }
            }
        }
    }
    return true;
}

ImportResult<ImportedScene> GltfLoader::Run() noexcept
{
    const JsonValue root = m_document.Root();
    if (!root.IsObject())
    {
        return ImportResult<ImportedScene>::Err(
            Error(ErrorCode::ParseError, "glTF root is not an object"));
    }
    StringView version;
    if (!root.Member("asset").Member("version").TryGetString(version) || version.Empty() ||
        version[0] != '2')
    {
        return ImportResult<ImportedScene>::Err(
            Error(ErrorCode::Unsupported, "glTF asset version is not 2.x"));
    }

    ImportError error;
    if (!LoadBuffers(error) || !LoadImages(error) || !LoadMaterials(error) || !LoadMeshes(error) ||
        !LoadNodes(error))
    {
        if (error.message == nullptr)
        {
            error = MakeError(ErrorCode::Internal, "glTF import failed");
        }
        return ImportResult<ImportedScene>::Err(error);
    }
    return ImportResult<ImportedScene>::Ok(std::move(m_scene));
}

/// Splits an absolute path into its directory and file name.
void SplitPath(const char* path, char* directory, usize directorySize, char* name,
               usize nameSize) noexcept
{
    usize length = std::strlen(path);
    usize separator = length;
    for (usize index = length; index > 0; --index)
    {
        if (path[index - 1] == '/' || path[index - 1] == '\\')
        {
            separator = index - 1;
            break;
        }
    }
    const usize directoryLength =
        separator == length ? 0 : (separator < directorySize ? separator : directorySize - 1);
    if (directoryLength > 0)
    {
        std::memcpy(directory, path, directoryLength);
    }
    directory[directoryLength] = '\0';

    const usize nameLength = length - separator > 1 ? length - separator - 1 : 0;
    const usize copied = nameLength < nameSize - 1 ? nameLength : nameSize - 1;
    if (copied > 0)
    {
        std::memcpy(name, path + separator + 1, copied);
    }
    name[copied] = '\0';
}

}  // namespace

usize ImportedScene::TotalTriangleCount() const noexcept
{
    usize triangles = 0;
    for (usize index = 0; index < meshes.Size(); ++index)
    {
        triangles += meshes[index].indices.Size() / 3;
    }
    return triangles;
}

ImportResult<ImportedScene> ImportGltf(cyber::memory::Span<const Byte> bytes,
                                       const char* baseDirectory, Allocator& allocator,
                                       const GltfImportOptions& options) noexcept
{
    cyber::memory::Span<const Byte> jsonBytes = bytes;
    cyber::memory::Span<const Byte> binary;

    if (bytes.Size() >= 12 && ReadLittleEndian32(bytes.Data()) == kGlbMagic)
    {
        const u32 version = ReadLittleEndian32(bytes.Data() + 4);
        const u32 totalLength = ReadLittleEndian32(bytes.Data() + 8);
        if (version != kGlbVersion)
        {
            return ImportResult<ImportedScene>::Err(
                MakeError(ErrorCode::Unsupported, "GLB version is not 2"));
        }
        if (totalLength > bytes.Size() || totalLength < 20)
        {
            return ImportResult<ImportedScene>::Err(
                MakeError(ErrorCode::ParseError, "GLB length is invalid"));
        }

        bool sawJson = false;
        usize offset = 12;
        while (offset + 8 <= totalLength)
        {
            const u32 chunkLength = ReadLittleEndian32(bytes.Data() + offset);
            const u32 chunkType = ReadLittleEndian32(bytes.Data() + offset + 4);
            if (chunkLength > totalLength - offset - 8)
            {
                return ImportResult<ImportedScene>::Err(
                    MakeError(ErrorCode::ParseError, "GLB chunk overruns the file"));
            }
            const cyber::memory::Span<const Byte> chunk =
                bytes.Subspan(offset + 8).First(chunkLength);
            if (chunkType == kChunkJson)
            {
                jsonBytes = chunk;
                sawJson = true;
            }
            else if (chunkType == kChunkBinary)
            {
                binary = chunk;
            }
            offset += 8 + chunkLength;
            if ((chunkLength % 4) != 0)
            {
                offset += 4 - (chunkLength % 4);
            }
        }
        if (!sawJson)
        {
            return ImportResult<ImportedScene>::Err(
                MakeError(ErrorCode::ParseError, "GLB has no JSON chunk"));
        }
    }

    JsonParseOptions parseOptions;
    parseOptions.maxDepth = 64;
    auto document = JsonDocument::Parse(
        StringView(reinterpret_cast<const char*>(jsonBytes.Data()), jsonBytes.Size()), allocator,
        parseOptions);
    if (document.IsError())
    {
        return ImportResult<ImportedScene>::Err(document.Error());
    }

    GltfLoader loader(allocator, document.Value(), binary, baseDirectory, options);
    return loader.Run();
}

ImportResult<ImportedScene> ImportGltfFile(const char* utf8Path, Allocator& allocator,
                                           const GltfImportOptions& options) noexcept
{
    auto bytes = cyber::platform::ReadFile(utf8Path, allocator);
    if (bytes.IsError())
    {
        return ImportResult<ImportedScene>::Err(
            MakeError(bytes.Error().code, bytes.Error().message));
    }

    char directory[512] = {};
    char name[256] = {};
    SplitPath(utf8Path, directory, sizeof(directory), name, sizeof(name));

    GltfImportOptions resolved = options;
    if (resolved.documentName == nullptr || std::strlen(resolved.documentName) == 0 ||
        std::strcmp(resolved.documentName, "scene.gltf") == 0)
    {
        resolved.documentName = name;
    }
    return ImportGltf(cyber::memory::Span<const Byte>(bytes.Value().Data(), bytes.Value().Size()),
                      directory, allocator, resolved);
}

}  // namespace cyber::assets

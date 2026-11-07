#include <cyber/assets/obj.h>
#include <cyber/core/hash.h>
#include <cyber/platform/filesystem.h>

#include <cstring>

namespace cyber::assets
{
namespace
{

using cyber::core::ErrorCode;
using cyber::core::f64;
using cyber::core::HashCombine;
using cyber::core::i32;
using cyber::core::i64;
using cyber::core::u32;
using cyber::core::u64;
using cyber::core::usize;
using cyber::math::Vec2;
using cyber::math::Vec3;
using cyber::memory::StringView;

constexpr u32 kEmptySlot = 0xffffffffu;

[[nodiscard]] bool IsSpace(char value) noexcept
{
    return value == ' ' || value == '\t' || value == '\r';
}

[[nodiscard]] bool IsDigit(char value) noexcept
{
    return value >= '0' && value <= '9';
}

/// Splits the next whitespace-delimited token off `line`.
[[nodiscard]] StringView NextToken(StringView& line) noexcept
{
    while (!line.Empty() && IsSpace(line[0]))
    {
        line = line.Substr(1);
    }
    usize length = 0;
    while (length < line.Size() && !IsSpace(line[length]) && line[length] != '#')
    {
        ++length;
    }
    const StringView token = line.Substr(0, length);
    line = line.Substr(length);
    return token;
}

/// Parses a decimal integer without accepting overflow.
[[nodiscard]] bool ParseInteger(StringView token, i32& value) noexcept
{
    if (token.Empty())
    {
        return false;
    }
    usize index = 0;
    bool negative = false;
    if (token[0] == '+' || token[0] == '-')
    {
        negative = token[0] == '-';
        index = 1;
    }
    if (index >= token.Size())
    {
        return false;
    }
    i64 accumulated = 0;
    for (; index < token.Size(); ++index)
    {
        if (!IsDigit(token[index]))
        {
            return false;
        }
        accumulated = accumulated * 10 + (token[index] - '0');
        if (accumulated > 2147483647ll)
        {
            return false;
        }
    }
    value = static_cast<i32>(negative ? -accumulated : accumulated);
    return true;
}

[[nodiscard]] f64 Pow10(i32 exponent) noexcept;

/// Parses one floating-point token with the same grammar OBJ writers emit.
[[nodiscard]] bool ParseFloat(StringView token, f32& value) noexcept
{
    if (token.Empty())
    {
        return false;
    }
    usize index = 0;
    bool negative = false;
    if (token[0] == '+' || token[0] == '-')
    {
        negative = token[0] == '-';
        index = 1;
    }
    f64 mantissa = 0.0;
    i32 exponent = 0;
    i32 digits = 0;
    bool sawDigit = false;
    while (index < token.Size() && IsDigit(token[index]))
    {
        if (digits < 19)
        {
            mantissa = mantissa * 10.0 + static_cast<f64>(token[index] - '0');
            ++digits;
        }
        sawDigit = true;
        ++index;
    }
    if (index < token.Size() && token[index] == '.')
    {
        ++index;
        while (index < token.Size() && IsDigit(token[index]))
        {
            if (digits < 19)
            {
                mantissa = mantissa * 10.0 + static_cast<f64>(token[index] - '0');
                ++digits;
                --exponent;
            }
            sawDigit = true;
            ++index;
        }
    }
    if (!sawDigit)
    {
        return false;
    }
    if (index < token.Size() && (token[index] == 'e' || token[index] == 'E'))
    {
        ++index;
        bool exponentNegative = false;
        if (index < token.Size() && (token[index] == '+' || token[index] == '-'))
        {
            exponentNegative = token[index] == '-';
            ++index;
        }
        i32 parsed = 0;
        bool sawExponent = false;
        while (index < token.Size() && IsDigit(token[index]))
        {
            if (parsed < 10000)
            {
                parsed = parsed * 10 + (token[index] - '0');
            }
            sawExponent = true;
            ++index;
        }
        if (!sawExponent)
        {
            return false;
        }
        exponent += exponentNegative ? -parsed : parsed;
    }
    if (index != token.Size())
    {
        return false;
    }

    f64 result = mantissa;
    while (exponent > 0)
    {
        const i32 step = exponent > 22 ? 22 : exponent;
        result *= step == 22 ? 1.0e22 : Pow10(step);
        exponent -= step;
    }
    while (exponent < 0)
    {
        const i32 step = exponent < -22 ? 22 : -exponent;
        result /= step == 22 ? 1.0e22 : Pow10(step);
        exponent += step;
    }
    value = static_cast<f32>(negative ? -result : result);
    return true;
}

[[nodiscard]] f64 Pow10(i32 exponent) noexcept
{
    f64 value = 1.0;
    for (i32 step = 0; step < exponent; ++step)
    {
        value *= 10.0;
    }
    return value;
}

struct CornerKey
{
    u32 position = 0;
    u32 texCoord = 0;
    u32 normal = 0;
};

[[nodiscard]] bool Equal(const CornerKey& lhs, const CornerKey& rhs) noexcept
{
    return lhs.position == rhs.position && lhs.texCoord == rhs.texCoord && lhs.normal == rhs.normal;
}

/// Open-addressing map from an OBJ corner (position/uv/normal triple) to the
/// deduplicated vertex index.
class CornerMap
{
public:
    explicit CornerMap(Allocator& allocator) noexcept
        : m_keys(allocator), m_values(allocator), m_slots(allocator)
    {
    }

    [[nodiscard]] bool TryReserve(usize count) noexcept
    {
        if (count > static_cast<usize>(kEmptySlot) || count > static_cast<usize>(-1) / 2)
        {
            return false;
        }
        usize capacity = 8;
        while (capacity < count * 2)
        {
            if (capacity > static_cast<usize>(-1) / 2)
            {
                return false;
            }
            capacity *= 2;
        }
        if (!m_keys.TryReserve(count) || !m_values.TryReserve(count))
        {
            return false;
        }
        if (!m_slots.TryResize(capacity))
        {
            return false;
        }
        for (usize index = 0; index < capacity; ++index)
        {
            m_slots[index] = kEmptySlot;
        }

        for (usize index = 0; index < m_keys.Size(); ++index)
        {
            const u64 hash = HashCombine(
                HashCombine(m_keys[index].position, m_keys[index].texCoord), m_keys[index].normal);
            usize slot = static_cast<usize>(hash) & (capacity - 1);
            while (m_slots[slot] != kEmptySlot)
            {
                slot = (slot + 1) & (capacity - 1);
            }
            m_slots[slot] = static_cast<u32>(index);
        }
        return true;
    }

    [[nodiscard]] u32 FindOrInsert(const CornerKey& key, u32 vertexIndex) noexcept
    {
        if (m_slots.Empty())
        {
            if (!TryReserve(64))
            {
                return kEmptySlot;
            }
        }
        if (m_keys.Size() >= m_slots.Size() / 2 && !TryReserve(m_keys.Size() + 1))
        {
            return kEmptySlot;
        }
        const u64 hash = HashCombine(HashCombine(key.position, key.texCoord), key.normal);
        usize slot = static_cast<usize>(hash) & (m_slots.Size() - 1);
        while (true)
        {
            const u32 entry = m_slots[slot];
            if (entry == kEmptySlot)
            {
                if (!m_keys.TryPushBack(key) || !m_values.TryPushBack(vertexIndex))
                {
                    return kEmptySlot;
                }
                m_slots[slot] = static_cast<u32>(m_keys.Size() - 1);
                return vertexIndex;
            }
            if (Equal(m_keys[entry], key))
            {
                return m_values[entry];
            }
            slot = (slot + 1) & (m_slots.Size() - 1);
        }
    }

private:
    DynamicArray<CornerKey> m_keys;
    DynamicArray<u32> m_values;
    DynamicArray<u32> m_slots;
};

struct FaceCorner
{
    i32 position = 0;
    i32 texCoord = 0;
    i32 normal = 0;
};

/// Parses `v`, `v/vt`, `v//vn`, or `v/vt/vn`.
[[nodiscard]] bool ParseCorner(StringView token, FaceCorner& corner) noexcept
{
    const usize firstSlash = [&]() noexcept
    {
        for (usize index = 0; index < token.Size(); ++index)
        {
            if (token[index] == '/')
            {
                return index;
            }
        }
        return token.Size();
    }();

    corner = FaceCorner{};
    if (!ParseInteger(token.Substr(0, firstSlash), corner.position))
    {
        return false;
    }
    if (firstSlash == token.Size())
    {
        return true;
    }
    const StringView rest = token.Substr(firstSlash + 1);
    usize secondSlash = rest.Size();
    for (usize index = 0; index < rest.Size(); ++index)
    {
        if (rest[index] == '/')
        {
            secondSlash = index;
            break;
        }
    }
    if (secondSlash > 0 && !ParseInteger(rest.Substr(0, secondSlash), corner.texCoord))
    {
        return false;
    }
    if (secondSlash < rest.Size() && !ParseInteger(rest.Substr(secondSlash + 1), corner.normal))
    {
        return false;
    }
    return true;
}

[[nodiscard]] bool ResolveIndex(i32 value, usize count, u32& resolved) noexcept
{
    if (value == 0 || count == 0)
    {
        return false;
    }
    const i64 index =
        value > 0 ? static_cast<i64>(value) - 1 : static_cast<i64>(count) + static_cast<i64>(value);
    if (index < 0 || index >= static_cast<i64>(count))
    {
        return false;
    }
    resolved = static_cast<u32>(index);
    return true;
}

}  // namespace

ImportResult<ObjImport> ImportObj(cyber::memory::Span<const Byte> text, Allocator& allocator,
                                  const AssetImportSettings& settings) noexcept
{
    ObjImport result(allocator);
    const StringView source(reinterpret_cast<const char*>(text.Data()), text.Size());

    DynamicArray<Vec3> positions(allocator);
    DynamicArray<Vec2> texCoords(allocator);
    DynamicArray<Vec3> normals(allocator);
    DynamicArray<u32> faceCorners(allocator);
    CornerMap corners(allocator);
    StringView pendingMaterialName;

    u32 currentMaterial = 0;
    u32 submeshStart = 0;
    u32 lineNumber = 0;

    usize cursor = 0;
    while (cursor <= source.Size())
    {
        usize lineEnd = cursor;
        while (lineEnd < source.Size() && source[lineEnd] != '\n')
        {
            ++lineEnd;
        }
        StringView line = source.Substr(cursor, lineEnd - cursor);
        cursor = lineEnd + 1;
        ++lineNumber;

        // Strip comments, then trim.
        for (usize index = 0; index < line.Size(); ++index)
        {
            if (line[index] == '#')
            {
                line = line.Substr(0, index);
                break;
            }
        }
        while (!line.Empty() && IsSpace(line[0]))
        {
            line = line.Substr(1);
        }
        while (!line.Empty() && IsSpace(line[line.Size() - 1]))
        {
            line = line.Substr(0, line.Size() - 1);
        }
        if (line.Empty())
        {
            continue;
        }

        const StringView keyword = NextToken(line);
        if (keyword.Size() == 1 && keyword[0] == 'v')
        {
            f32 x = 0.0f;
            f32 y = 0.0f;
            f32 z = 0.0f;
            const bool parsed = ParseFloat(NextToken(line), x) && ParseFloat(NextToken(line), y) &&
                                ParseFloat(NextToken(line), z);
            if (!parsed)
            {
                return ImportResult<ObjImport>::Err(
                    MakeTextError(ErrorCode::ParseError, "OBJ vertex is malformed", lineNumber, 1));
            }
            if (!positions.TryPushBack(Vec3{x, y, z}))
            {
                return ImportResult<ObjImport>::Err(
                    MakeError(ErrorCode::OutOfRange, "out of memory for OBJ vertices"));
            }
        }
        else if (keyword.Size() == 2 && keyword[0] == 'v' && keyword[1] == 't')
        {
            f32 u = 0.0f;
            f32 v = 0.0f;
            const bool parsed = ParseFloat(NextToken(line), u) && ParseFloat(NextToken(line), v);
            if (!parsed)
            {
                return ImportResult<ObjImport>::Err(MakeTextError(
                    ErrorCode::ParseError, "OBJ texture coordinate is malformed", lineNumber, 1));
            }
            if (settings.flipTexCoordV)
            {
                v = 1.0f - v;
            }
            if (!texCoords.TryPushBack(Vec2{u, v}))
            {
                return ImportResult<ObjImport>::Err(
                    MakeError(ErrorCode::OutOfRange, "out of memory for OBJ texture coordinates"));
            }
        }
        else if (keyword.Size() == 2 && keyword[0] == 'v' && keyword[1] == 'n')
        {
            f32 x = 0.0f;
            f32 y = 0.0f;
            f32 z = 0.0f;
            const bool parsed = ParseFloat(NextToken(line), x) && ParseFloat(NextToken(line), y) &&
                                ParseFloat(NextToken(line), z);
            if (!parsed)
            {
                return ImportResult<ObjImport>::Err(
                    MakeTextError(ErrorCode::ParseError, "OBJ normal is malformed", lineNumber, 1));
            }
            if (!normals.TryPushBack(Vec3{x, y, z}))
            {
                return ImportResult<ObjImport>::Err(
                    MakeError(ErrorCode::OutOfRange, "out of memory for OBJ normals"));
            }
        }
        else if (keyword.Size() == 1 && keyword[0] == 'f')
        {
            if (positions.Empty())
            {
                return ImportResult<ObjImport>::Err(MakeTextError(
                    ErrorCode::ParseError, "OBJ face appears before any vertex", lineNumber, 1));
            }
            if (result.materialNames.Empty())
            {
                // OBJ without `usemtl` still needs a valid material index 0.
                if (!result.materialNames.TryEmplaceBack(allocator, StringView("default")))
                {
                    return ImportResult<ObjImport>::Err(
                        MakeError(ErrorCode::OutOfRange, "out of memory for OBJ materials"));
                }
            }

            faceCorners.Clear();
            while (true)
            {
                const StringView token = NextToken(line);
                if (token.Empty())
                {
                    break;
                }
                FaceCorner corner;
                if (!ParseCorner(token, corner))
                {
                    return ImportResult<ObjImport>::Err(MakeTextError(
                        ErrorCode::ParseError, "OBJ face corner is malformed", lineNumber, 1));
                }
                u32 positionIndex = 0;
                u32 texCoordIndex = 0;
                u32 normalIndex = 0;
                if (!ResolveIndex(corner.position, positions.Size(), positionIndex) ||
                    (corner.texCoord != 0 &&
                     !ResolveIndex(corner.texCoord, texCoords.Size(), texCoordIndex)) ||
                    (corner.normal != 0 &&
                     !ResolveIndex(corner.normal, normals.Size(), normalIndex)))
                {
                    return ImportResult<ObjImport>::Err(MakeTextError(
                        ErrorCode::ParseError, "OBJ face index is out of range", lineNumber, 1));
                }

                const CornerKey key{positionIndex, texCoordIndex + 1, normalIndex + 1};
                const u32 vertexIndex =
                    corners.FindOrInsert(key, static_cast<u32>(result.mesh.vertices.Size()));
                if (vertexIndex == kEmptySlot)
                {
                    return ImportResult<ObjImport>::Err(
                        MakeError(ErrorCode::OutOfRange, "out of memory for OBJ vertices"));
                }
                if (static_cast<usize>(vertexIndex) == result.mesh.vertices.Size())
                {
                    MeshVertex vertex;
                    vertex.position = positions[positionIndex];
                    if (texCoordIndex != 0 || corner.texCoord != 0)
                    {
                        vertex.texCoord0 = texCoords[texCoordIndex];
                    }
                    if (corner.normal != 0)
                    {
                        const Vec3& normal = normals[normalIndex];
                        const f32 length = cyber::math::Length(normal);
                        vertex.normal = length > 0.0f ? normal / length : Vec3{0.0f, 0.0f, 1.0f};
                    }
                    if (!result.mesh.vertices.TryPushBack(vertex))
                    {
                        return ImportResult<ObjImport>::Err(
                            MakeError(ErrorCode::OutOfRange, "out of memory for OBJ vertices"));
                    }
                }
                if (!faceCorners.TryPushBack(vertexIndex))
                {
                    return ImportResult<ObjImport>::Err(
                        MakeError(ErrorCode::OutOfRange, "out of memory for OBJ faces"));
                }
            }

            if (faceCorners.Size() < 3)
            {
                return ImportResult<ObjImport>::Err(MakeTextError(
                    ErrorCode::ParseError, "OBJ face has fewer than three corners", lineNumber, 1));
            }
            for (usize corner = 2; corner < faceCorners.Size(); ++corner)
            {
                if (!result.mesh.indices.TryPushBack(faceCorners[0]) ||
                    !result.mesh.indices.TryPushBack(faceCorners[corner - 1]) ||
                    !result.mesh.indices.TryPushBack(faceCorners[corner]))
                {
                    return ImportResult<ObjImport>::Err(
                        MakeError(ErrorCode::OutOfRange, "out of memory for OBJ indices"));
                }
            }
        }
        else if (keyword.Size() == 6 && keyword.StartsWith(StringView("usemtl")))
        {
            const StringView name = NextToken(line);
            if (name.Empty())
            {
                return ImportResult<ObjImport>::Err(MakeTextError(
                    ErrorCode::ParseError, "usemtl has no material name", lineNumber, 1));
            }

            u32 materialIndex = 0;
            bool found = false;
            for (usize index = 0; index < result.materialNames.Size(); ++index)
            {
                if (result.materialNames[index].View().Size() == name.Size() &&
                    std::memcmp(result.materialNames[index].View().Data(), name.Data(),
                                name.Size()) == 0)
                {
                    materialIndex = static_cast<u32>(index);
                    found = true;
                    break;
                }
            }
            if (!found)
            {
                if (result.materialNames.Size() >= 0xffffffffull)
                {
                    return ImportResult<ObjImport>::Err(
                        MakeError(ErrorCode::Overflow, "OBJ has too many materials"));
                }
                if (!result.materialNames.TryEmplaceBack(allocator, name))
                {
                    return ImportResult<ObjImport>::Err(
                        MakeError(ErrorCode::OutOfRange, "out of memory for OBJ materials"));
                }
                materialIndex = static_cast<u32>(result.materialNames.Size() - 1);
            }

            if (materialIndex != currentMaterial)
            {
                const u32 indexCount = static_cast<u32>(result.mesh.indices.Size());
                if (indexCount > submeshStart)
                {
                    SubmeshAsset submesh;
                    submesh.firstIndex = submeshStart;
                    submesh.indexCount = indexCount - submeshStart;
                    submesh.materialIndex = currentMaterial;
                    if (!result.mesh.submeshes.TryPushBack(submesh))
                    {
                        return ImportResult<ObjImport>::Err(
                            MakeError(ErrorCode::OutOfRange, "out of memory for OBJ submeshes"));
                    }
                    submeshStart = indexCount;
                }
                currentMaterial = materialIndex;
            }
        }
        else if (keyword.Size() == 6 && keyword.StartsWith(StringView("mtllib")))
        {
            while (true)
            {
                const StringView name = NextToken(line);
                if (name.Empty())
                {
                    break;
                }
                if (!result.dependencies.TryEmplaceBack(allocator, name))
                {
                    return ImportResult<ObjImport>::Err(
                        MakeError(ErrorCode::OutOfRange, "out of memory for OBJ dependencies"));
                }
            }
        }
        // `o`, `g`, `s`, and unknown statements do not change the geometry.
    }

    if (result.mesh.indices.Empty())
    {
        return ImportResult<ObjImport>::Err(
            MakeError(ErrorCode::ParseError, "OBJ contains no faces"));
    }

    const u32 indexCount = static_cast<u32>(result.mesh.indices.Size());
    if (indexCount > submeshStart)
    {
        SubmeshAsset submesh;
        submesh.firstIndex = submeshStart;
        submesh.indexCount = indexCount - submeshStart;
        submesh.materialIndex = currentMaterial;
        if (!result.mesh.submeshes.TryPushBack(submesh))
        {
            return ImportResult<ObjImport>::Err(
                MakeError(ErrorCode::OutOfRange, "out of memory for OBJ submeshes"));
        }
    }

    if (result.materialNames.Empty() &&
        !result.materialNames.TryEmplaceBack(allocator, StringView("default")))
    {
        return ImportResult<ObjImport>::Err(
            MakeError(ErrorCode::OutOfRange, "out of memory for OBJ materials"));
    }

    if (settings.generateTangents)
    {
        auto tangents = GenerateTangents(result.mesh, allocator);
        if (tangents.IsError())
        {
            return ImportResult<ObjImport>::Err(tangents.Error());
        }
    }

    auto validated = result.mesh.Validate();
    if (validated.IsError())
    {
        return ImportResult<ObjImport>::Err(validated.Error());
    }
    return ImportResult<ObjImport>::Ok(std::move(result));
}

ImportResult<ObjImport> ImportObjFile(const char* utf8Path, Allocator& allocator,
                                      const AssetImportSettings& settings) noexcept
{
    auto bytes = cyber::platform::ReadFile(utf8Path, allocator);
    if (bytes.IsError())
    {
        return ImportResult<ObjImport>::Err(MakeError(bytes.Error().code, bytes.Error().message));
    }
    return ImportObj(cyber::memory::Span<const Byte>(bytes.Value().Data(), bytes.Value().Size()),
                     allocator, settings);
}

}  // namespace cyber::assets

#include <cyber/platform/filesystem.h>
#include <cyber/software/mesh.h>

#include <cmath>
#include <utility>

namespace cyber::software
{

namespace
{

using cyber::core::f64;
using cyber::core::i32;
using cyber::core::i64;
using cyber::core::u32;

constexpr usize kMaxRecordBytes = 4096;
constexpr u32 kMaxFaceCorners = 64;

bool IsRecordSpace(char value) noexcept
{
    return value == ' ' || value == '\t' || value == '\r';
}

void SkipSpaces(const char*& cursor, const char* end) noexcept
{
    while (cursor < end && IsRecordSpace(*cursor))
    {
        ++cursor;
    }
}

bool ParseFloat(const char*& cursor, const char* end, f32& value) noexcept
{
    SkipSpaces(cursor, end);
    bool negative = false;
    if (cursor < end && (*cursor == '+' || *cursor == '-'))
    {
        negative = *cursor == '-';
        ++cursor;
    }

    f64 magnitude = 0.0;
    bool anyDigits = false;
    while (cursor < end && *cursor >= '0' && *cursor <= '9')
    {
        magnitude = magnitude * 10.0 + static_cast<f64>(*cursor - '0');
        anyDigits = true;
        ++cursor;
    }
    if (cursor < end && *cursor == '.')
    {
        ++cursor;
        f64 scale = 0.1;
        while (cursor < end && *cursor >= '0' && *cursor <= '9')
        {
            magnitude += static_cast<f64>(*cursor - '0') * scale;
            scale *= 0.1;
            anyDigits = true;
            ++cursor;
        }
    }
    if (!anyDigits)
    {
        return false;
    }
    if (cursor < end && (*cursor == 'e' || *cursor == 'E'))
    {
        ++cursor;
        bool negativeExponent = false;
        if (cursor < end && (*cursor == '+' || *cursor == '-'))
        {
            negativeExponent = *cursor == '-';
            ++cursor;
        }
        i32 exponent = 0;
        bool anyExponent = false;
        while (cursor < end && *cursor >= '0' && *cursor <= '9')
        {
            if (exponent < 1000)
            {
                exponent = exponent * 10 + (*cursor - '0');
            }
            anyExponent = true;
            ++cursor;
        }
        if (!anyExponent)
        {
            return false;
        }
        f64 factor = 1.0;
        for (i32 index = 0; index < exponent && index < 64; ++index)
        {
            factor *= 10.0;
        }
        magnitude = negativeExponent ? magnitude / factor : magnitude * factor;
    }

    value = static_cast<f32>(negative ? -magnitude : magnitude);
    return std::isfinite(value);
}

bool ParseInt(const char*& cursor, const char* end, i32& value) noexcept
{
    SkipSpaces(cursor, end);
    bool negative = false;
    if (cursor < end && (*cursor == '+' || *cursor == '-'))
    {
        negative = *cursor == '-';
        ++cursor;
    }
    i64 magnitude = 0;
    bool anyDigits = false;
    while (cursor < end && *cursor >= '0' && *cursor <= '9')
    {
        magnitude = magnitude * 10 + (*cursor - '0');
        if (magnitude > 0x7FFFFFFF)
        {
            return false;
        }
        anyDigits = true;
        ++cursor;
    }
    if (!anyDigits)
    {
        return false;
    }
    value = static_cast<i32>(negative ? -magnitude : magnitude);
    return true;
}

struct Corner
{
    i32 position = 0;
    i32 uv = 0;
    i32 normal = 0;
};

bool ParseCorner(const char*& cursor, const char* end, Corner& corner) noexcept
{
    corner = Corner{};
    if (!ParseInt(cursor, end, corner.position))
    {
        return false;
    }
    if (cursor < end && *cursor == '/')
    {
        ++cursor;
        if (cursor < end && *cursor != '/')
        {
            if (!ParseInt(cursor, end, corner.uv))
            {
                return false;
            }
        }
        if (cursor < end && *cursor == '/')
        {
            ++cursor;
            if (!ParseInt(cursor, end, corner.normal))
            {
                return false;
            }
        }
    }
    return true;
}

i32 ResolveIndex(i32 index, usize count) noexcept
{
    if (index > 0)
    {
        const i32 resolved = index - 1;
        return static_cast<usize>(resolved) < count ? resolved : -1;
    }
    if (index < 0)
    {
        const i64 resolved = static_cast<i64>(count) + index;
        return resolved >= 0 && static_cast<usize>(resolved) < count ? static_cast<i32>(resolved)
                                                                     : -1;
    }
    return -1;
}

}  // namespace

core::Result<Mesh, core::Error> LoadObjFromMemory(const char* text, usize size,
                                                  memory::Allocator& allocator) noexcept
{
    using ResultType = core::Result<Mesh, core::Error>;

    if (text == nullptr && size != 0)
    {
        return ResultType::Err(core::Error{core::ErrorCode::InvalidArgument, "obj text is null"});
    }

    memory::DynamicArray<math::Vec3> positions(allocator);
    memory::DynamicArray<math::Vec3> normals(allocator);
    memory::DynamicArray<math::Vec2> uvs(allocator);
    Mesh mesh(allocator);

    const char* cursor = text;
    const char* end = text + size;

    while (cursor < end)
    {
        const char* lineEnd = cursor;
        while (lineEnd < end && *lineEnd != '\n')
        {
            ++lineEnd;
        }
        if (static_cast<usize>(lineEnd - cursor) > kMaxRecordBytes)
        {
            return ResultType::Err(
                core::Error{core::ErrorCode::InvalidArgument, "obj record is too long"});
        }

        const char* position = cursor;
        SkipSpaces(position, lineEnd);
        if (position < lineEnd && *position != '#')
        {
            const char* keyword = position;
            while (position < lineEnd && !IsRecordSpace(*position))
            {
                ++position;
            }
            const usize keywordLength = static_cast<usize>(position - keyword);
            const bool isVertex = keywordLength == 1 && keyword[0] == 'v';
            const bool isNormal = keywordLength == 2 && keyword[0] == 'v' && keyword[1] == 'n';
            const bool isUv = keywordLength == 2 && keyword[0] == 'v' && keyword[1] == 't';
            const bool isFace = keywordLength == 1 && keyword[0] == 'f';

            if (isVertex)
            {
                f32 x = 0.0f;
                f32 y = 0.0f;
                f32 z = 0.0f;
                if (!ParseFloat(position, lineEnd, x) || !ParseFloat(position, lineEnd, y) ||
                    !ParseFloat(position, lineEnd, z))
                {
                    return ResultType::Err(
                        core::Error{core::ErrorCode::InvalidArgument, "malformed obj position"});
                }
                if (!positions.TryPushBack(math::Vec3{x, y, z}))
                {
                    return ResultType::Err(
                        core::Error{core::ErrorCode::Internal, "failed to store a position"});
                }
            }
            else if (isNormal)
            {
                f32 x = 0.0f;
                f32 y = 0.0f;
                f32 z = 0.0f;
                if (!ParseFloat(position, lineEnd, x) || !ParseFloat(position, lineEnd, y) ||
                    !ParseFloat(position, lineEnd, z))
                {
                    return ResultType::Err(
                        core::Error{core::ErrorCode::InvalidArgument, "malformed obj normal"});
                }
                if (!normals.TryPushBack(math::Vec3{x, y, z}))
                {
                    return ResultType::Err(
                        core::Error{core::ErrorCode::Internal, "failed to store a normal"});
                }
            }
            else if (isUv)
            {
                f32 u = 0.0f;
                f32 v = 0.0f;
                if (!ParseFloat(position, lineEnd, u) || !ParseFloat(position, lineEnd, v))
                {
                    return ResultType::Err(
                        core::Error{core::ErrorCode::InvalidArgument, "malformed obj texcoord"});
                }
                if (!uvs.TryPushBack(math::Vec2{u, v}))
                {
                    return ResultType::Err(
                        core::Error{core::ErrorCode::Internal, "failed to store a texcoord"});
                }
            }
            else if (isFace)
            {
                Corner corners[kMaxFaceCorners];
                u32 cornerCount = 0;
                for (;;)
                {
                    SkipSpaces(position, lineEnd);
                    if (position >= lineEnd)
                    {
                        break;
                    }
                    if (cornerCount >= kMaxFaceCorners)
                    {
                        return ResultType::Err(core::Error{core::ErrorCode::InvalidArgument,
                                                           "obj face has too many corners"});
                    }
                    if (!ParseCorner(position, lineEnd, corners[cornerCount]))
                    {
                        return ResultType::Err(
                            core::Error{core::ErrorCode::InvalidArgument, "malformed obj face"});
                    }
                    ++cornerCount;
                }
                if (cornerCount < 3)
                {
                    return ResultType::Err(core::Error{core::ErrorCode::InvalidArgument,
                                                       "obj face has fewer than three corners"});
                }

                VertexInput expanded[kMaxFaceCorners];
                for (u32 index = 0; index < cornerCount; ++index)
                {
                    const Corner& corner = corners[index];
                    const i32 positionIndex = ResolveIndex(corner.position, positions.Size());
                    if (positionIndex < 0)
                    {
                        return ResultType::Err(core::Error{core::ErrorCode::InvalidArgument,
                                                           "obj position index is out of range"});
                    }
                    i32 uvIndex = 0;
                    if (corner.uv != 0)
                    {
                        uvIndex = ResolveIndex(corner.uv, uvs.Size());
                        if (uvIndex < 0)
                        {
                            return ResultType::Err(
                                core::Error{core::ErrorCode::InvalidArgument,
                                            "obj texcoord index is out of range"});
                        }
                    }
                    i32 normalIndex = 0;
                    if (corner.normal != 0)
                    {
                        normalIndex = ResolveIndex(corner.normal, normals.Size());
                        if (normalIndex < 0)
                        {
                            return ResultType::Err(core::Error{core::ErrorCode::InvalidArgument,
                                                               "obj normal index is out of range"});
                        }
                    }

                    VertexInput vertex;
                    vertex.position = positions[static_cast<usize>(positionIndex)];
                    vertex.uv =
                        corner.uv != 0 ? uvs[static_cast<usize>(uvIndex)] : math::Vec2{0.0f, 0.0f};
                    vertex.normal =
                        corner.normal != 0
                            ? cyber::math::Normalize(normals[static_cast<usize>(normalIndex)])
                            : math::Vec3{0.0f, 0.0f, 1.0f};
                    vertex.color = math::Color{1.0f, 1.0f, 1.0f, 1.0f};
                    expanded[index] = vertex;
                }

                for (u32 index = 1; index + 1 < cornerCount; ++index)
                {
                    if (!mesh.vertices.TryPushBack(expanded[0]) ||
                        !mesh.vertices.TryPushBack(expanded[index]) ||
                        !mesh.vertices.TryPushBack(expanded[index + 1]))
                    {
                        return ResultType::Err(
                            core::Error{core::ErrorCode::Internal, "failed to store a triangle"});
                    }
                }
            }
        }

        cursor = lineEnd < end ? lineEnd + 1 : end;
    }

    return ResultType::Ok(std::move(mesh));
}

core::Result<Mesh, core::Error> LoadObjFile(const char* utf8Path,
                                            memory::Allocator& allocator) noexcept
{
    using ResultType = core::Result<Mesh, core::Error>;
    if (utf8Path == nullptr || utf8Path[0] == '\0')
    {
        return ResultType::Err(core::Error{core::ErrorCode::InvalidArgument, "obj path is empty"});
    }

    auto file = platform::ReadFile(utf8Path, allocator);
    if (!file)
    {
        return ResultType::Err(file.Error());
    }
    memory::DynamicArray<memory::Byte>& bytes = file.Value();
    return LoadObjFromMemory(reinterpret_cast<const char*>(bytes.Data()), bytes.Size(), allocator);
}

}  // namespace cyber::software

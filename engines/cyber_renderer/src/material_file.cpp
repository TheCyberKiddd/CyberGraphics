#include <cyber/renderer/material.h>

#include <charconv>
#include <cmath>
#include <cstring>
#include <limits>

namespace cyber::renderer
{
namespace
{

using cyber::core::Error;
using cyber::core::ErrorCode;
using cyber::core::Result;
using cyber::memory::Span;
using cyber::memory::StringView;

struct Token
{
    StringView value{};
    u32 column = 0;
    bool quoted = false;
    std::array<char, kMaxMaterialUriBytes + 1> decoded{};
};

struct TokenList
{
    std::array<Token, 32> values{};
    u32 count = 0;
};

[[nodiscard]] MaterialParseError ParseError(ErrorCode code, const char* message, u32 line,
                                            u32 column) noexcept
{
    return MaterialParseError{Error{code, message}, line, column};
}

[[nodiscard]] bool IsSpace(char ch) noexcept
{
    return ch == ' ' || ch == '\t';
}

[[nodiscard]] bool IsIdentifier(StringView value) noexcept
{
    if (value.Data() == nullptr || value.Size() == 0 || value.Size() > kMaxMaterialNameBytes)
    {
        return false;
    }
    const auto first = static_cast<unsigned char>(value[0]);
    if (!((first >= 'A' && first <= 'Z') || (first >= 'a' && first <= 'z') || first == '_'))
    {
        return false;
    }
    for (cyber::core::usize index = 1; index < value.Size(); ++index)
    {
        const auto ch = static_cast<unsigned char>(value[index]);
        if (!((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') ||
              ch == '_'))
        {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool IsValidUtf8(StringView text) noexcept
{
    cyber::core::usize index = 0;
    while (index < text.Size())
    {
        const u8 first = static_cast<u8>(text[index]);
        if (first < 0x80)
        {
            if (first < 0x20 && first != '\n' && first != '\r' && first != '\t')
            {
                return false;
            }
            if (first == 0x7F)
            {
                return false;
            }
            ++index;
            continue;
        }

        u32 continuationCount = 0;
        u32 codePoint = 0;
        if ((first & 0xE0U) == 0xC0U)
        {
            continuationCount = 1;
            codePoint = first & 0x1FU;
        }
        else if ((first & 0xF0U) == 0xE0U)
        {
            continuationCount = 2;
            codePoint = first & 0x0FU;
        }
        else if ((first & 0xF8U) == 0xF0U)
        {
            continuationCount = 3;
            codePoint = first & 0x07U;
        }
        else
        {
            return false;
        }
        if (continuationCount > text.Size() - index - 1)
        {
            return false;
        }
        for (u32 continuation = 1; continuation <= continuationCount; ++continuation)
        {
            const u8 next = static_cast<u8>(text[index + continuation]);
            if ((next & 0xC0U) != 0x80U)
            {
                return false;
            }
            codePoint = (codePoint << 6U) | (next & 0x3FU);
        }
        if ((continuationCount == 1 && codePoint < 0x80U) ||
            (continuationCount == 2 && codePoint < 0x800U) ||
            (continuationCount == 3 && codePoint < 0x10000U) || codePoint > 0x10FFFFU ||
            (codePoint >= 0xD800U && codePoint <= 0xDFFFU) ||
            (codePoint >= 0x80U && codePoint <= 0x9FU))
        {
            return false;
        }
        index += continuationCount + 1;
    }
    return true;
}

[[nodiscard]] bool IsValidUri(StringView value) noexcept
{
    if (value.Data() == nullptr || value.Size() == 0 || value.Size() > kMaxMaterialUriBytes ||
        !IsValidUtf8(value))
    {
        return false;
    }
    for (cyber::core::usize index = 0; index < value.Size(); ++index)
    {
        const auto ch = static_cast<unsigned char>(value[index]);
        if (ch < 0x20U || ch == 0x7FU)
        {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool Tokenize(StringView line, u32 lineNumber, TokenList& tokens,
                            MaterialParseError& error) noexcept
{
    cyber::core::usize cursor = 0;
    while (cursor < line.Size())
    {
        while (cursor < line.Size() && IsSpace(line[cursor]))
        {
            ++cursor;
        }
        if (cursor == line.Size())
        {
            break;
        }
        if (tokens.count == tokens.values.size())
        {
            error = ParseError(ErrorCode::OutOfRange, "material file: too many fields", lineNumber,
                               static_cast<u32>(cursor + 1));
            return false;
        }
        Token& token = tokens.values[tokens.count++];
        token.column = static_cast<u32>(cursor + 1);
        if (line[cursor] != '"')
        {
            const cyber::core::usize start = cursor;
            while (cursor < line.Size() && !IsSpace(line[cursor]))
            {
                if (line[cursor] == '"')
                {
                    error = ParseError(ErrorCode::InvalidArgument,
                                       "material file: quote must start a token", lineNumber,
                                       static_cast<u32>(cursor + 1));
                    return false;
                }
                ++cursor;
            }
            token.value = StringView(line.Data() + start, cursor - start);
            continue;
        }

        token.quoted = true;
        ++cursor;
        u32 written = 0;
        bool closed = false;
        while (cursor < line.Size())
        {
            const char ch = line[cursor++];
            if (ch == '"')
            {
                closed = true;
                break;
            }
            char decoded = ch;
            if (ch == '\\')
            {
                if (cursor == line.Size() || (line[cursor] != '"' && line[cursor] != '\\'))
                {
                    error = ParseError(ErrorCode::InvalidArgument,
                                       "material file: invalid quoted-string escape", lineNumber,
                                       static_cast<u32>(cursor));
                    return false;
                }
                decoded = line[cursor++];
            }
            if (static_cast<unsigned char>(decoded) < 0x20U ||
                static_cast<unsigned char>(decoded) == 0x7FU || written == kMaxMaterialUriBytes)
            {
                error = ParseError(ErrorCode::OutOfRange,
                                   "material file: quoted value contains a control or is too long",
                                   lineNumber, token.column);
                return false;
            }
            token.decoded[written++] = decoded;
        }
        if (!closed)
        {
            error = ParseError(ErrorCode::InvalidArgument, "material file: unterminated string",
                               lineNumber, token.column);
            return false;
        }
        token.decoded[written] = '\0';
        token.value = StringView(token.decoded.data(), written);
        if (cursor < line.Size() && !IsSpace(line[cursor]))
        {
            error = ParseError(ErrorCode::InvalidArgument,
                               "material file: quoted value must end at a field boundary",
                               lineNumber, static_cast<u32>(cursor + 1));
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool Word(const TokenList& tokens, u32 index, const char* expected) noexcept
{
    return index < tokens.count && !tokens.values[index].quoted &&
           tokens.values[index].value == StringView(expected);
}

[[nodiscard]] bool ParseFloat(const Token& token, float& value) noexcept
{
    if (token.quoted || token.value.Empty())
    {
        return false;
    }
    const char* begin = token.value.Data();
    const char* end = begin + token.value.Size();
    const auto parsed = std::from_chars(begin, end, value, std::chars_format::general);
    return parsed.ec == std::errc{} && parsed.ptr == end && std::isfinite(value);
}

[[nodiscard]] shaders::ValueType ParseValueType(const Token& token) noexcept
{
    if (token.quoted)
    {
        return static_cast<shaders::ValueType>(255);
    }
    if (token.value == StringView("float"))
    {
        return shaders::ValueType::Float;
    }
    if (token.value == StringView("float2"))
    {
        return shaders::ValueType::Float2;
    }
    if (token.value == StringView("float3"))
    {
        return shaders::ValueType::Float3;
    }
    if (token.value == StringView("float4"))
    {
        return shaders::ValueType::Float4;
    }
    return static_cast<shaders::ValueType>(255);
}

[[nodiscard]] u32 ValueComponents(shaders::ValueType type) noexcept
{
    switch (type)
    {
    case shaders::ValueType::Float:
        return 1;
    case shaders::ValueType::Float2:
        return 2;
    case shaders::ValueType::Float3:
        return 3;
    case shaders::ValueType::Float4:
        return 4;
    default:
        return 0;
    }
}

[[nodiscard]] bool GltfParameterMatches(StringView name, const MaterialValue& defaultValue,
                                        const MaterialValue& minimum,
                                        const MaterialValue& maximum) noexcept
{
    shaders::ValueType requiredType = static_cast<shaders::ValueType>(255);
    if (name == StringView("baseColorFactor"))
    {
        requiredType = shaders::ValueType::Float4;
    }
    else if (name == StringView("metallicFactor") || name == StringView("roughnessFactor") ||
             name == StringView("alphaCutoff") || name == StringView("occlusionStrength"))
    {
        requiredType = shaders::ValueType::Float;
    }
    else if (name == StringView("emissiveFactor"))
    {
        requiredType = shaders::ValueType::Float3;
    }
    else
    {
        return true;
    }
    if (defaultValue.type != requiredType || minimum.type != requiredType ||
        maximum.type != requiredType)
    {
        return false;
    }
    for (u32 component = 0; component < ValueComponents(requiredType); ++component)
    {
        const float expectedDefault = name == StringView("emissiveFactor") ? 0.0F
                                      : name == StringView("alphaCutoff")  ? 0.5F
                                                                           : 1.0F;
        const bool maximumMatches =
            name == StringView("alphaCutoff") || maximum.components[component] == 1.0F;
        if (defaultValue.components[component] != expectedDefault ||
            minimum.components[component] != 0.0F || !maximumMatches)
        {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool ParseQueue(const Token& token, MaterialQueue& queue) noexcept
{
    if (token.quoted)
    {
        return false;
    }
    if (token.value == StringView("opaque"))
    {
        queue = MaterialQueue::Opaque;
    }
    else if (token.value == StringView("masked"))
    {
        queue = MaterialQueue::Masked;
    }
    else if (token.value == StringView("transparent"))
    {
        queue = MaterialQueue::Transparent;
    }
    else
    {
        return false;
    }
    return true;
}

[[nodiscard]] bool ParseKnownTexture(const Token& token) noexcept
{
    constexpr const char* kNames[] = {"baseColorTexture", "metallicRoughnessTexture",
                                      "normalTexture", "occlusionTexture", "emissiveTexture"};
    for (const char* name : kNames)
    {
        if (token.value == StringView(name))
        {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool IsKnownColorSpace(MaterialColorSpace colorSpace) noexcept
{
    return colorSpace == MaterialColorSpace::Linear || colorSpace == MaterialColorSpace::Srgb;
}

[[nodiscard]] bool TextureColorSpaceMatches(StringView name, MaterialColorSpace colorSpace) noexcept
{
    const bool colorTexture =
        name == StringView("baseColorTexture") || name == StringView("emissiveTexture");
    return colorTexture ? colorSpace == MaterialColorSpace::Srgb
                        : colorSpace == MaterialColorSpace::Linear;
}

[[nodiscard]] bool CopyText(std::array<char, kMaxMaterialNameBytes + 1>& output,
                            StringView value) noexcept
{
    if (!IsIdentifier(value))
    {
        return false;
    }
    std::memcpy(output.data(), value.Data(), value.Size());
    output[value.Size()] = '\0';
    return true;
}

[[nodiscard]] bool CopyUri(std::array<char, kMaxMaterialUriBytes + 1>& output, StringView value,
                           u32* outSize) noexcept
{
    if (!IsValidUri(value))
    {
        return false;
    }
    std::memcpy(output.data(), value.Data(), value.Size());
    output[value.Size()] = '\0';
    if (outSize != nullptr)
    {
        *outSize = static_cast<u32>(value.Size());
    }
    return true;
}

template <cyber::core::usize N>
[[nodiscard]] cyber::core::usize BoundedLength(const std::array<char, N>& value) noexcept
{
    for (cyber::core::usize index = 0; index < value.size(); ++index)
    {
        if (value[index] == '\0')
        {
            return index;
        }
    }
    return value.size();
}

template <cyber::core::usize N>
[[nodiscard]] StringView BoundedView(const std::array<char, N>& value) noexcept
{
    return StringView(value.data(), BoundedLength(value));
}

[[nodiscard]] StringView NameView(const std::array<char, kMaxMaterialNameBytes + 1>& value) noexcept
{
    return BoundedView(value);
}

[[nodiscard]] bool FileValueValid(const MaterialValue& value) noexcept
{
    const u32 count = ValueComponents(value.type);
    if (count == 0)
    {
        return false;
    }
    for (u32 component = 0; component < count; ++component)
    {
        if (!std::isfinite(value.components[component]))
        {
            return false;
        }
    }
    return true;
}

class TextBuilder
{
public:
    [[nodiscard]] bool Append(const char* text, u32 size) noexcept
    {
        if (size > m_data.size() - m_size)
        {
            return false;
        }
        std::memcpy(m_data.data() + m_size, text, size);
        m_size += size;
        return true;
    }

    [[nodiscard]] bool AppendLiteral(const char* text) noexcept
    {
        return Append(text, static_cast<u32>(std::strlen(text)));
    }

    [[nodiscard]] bool AppendQuoted(const char* text, u32 size) noexcept
    {
        if (!AppendLiteral("\""))
        {
            return false;
        }
        for (u32 index = 0; index < size; ++index)
        {
            if (text[index] == '"' || text[index] == '\\')
            {
                if (!AppendLiteral("\\"))
                {
                    return false;
                }
            }
            if (!Append(text + index, 1))
            {
                return false;
            }
        }
        return AppendLiteral("\"");
    }

    [[nodiscard]] bool AppendFloat(float value) noexcept
    {
        std::array<char, 64> buffer{};
        const auto converted =
            std::to_chars(buffer.data(), buffer.data() + buffer.size(), value,
                          std::chars_format::general, std::numeric_limits<float>::max_digits10);
        if (converted.ec != std::errc{})
        {
            return false;
        }
        return Append(buffer.data(), static_cast<u32>(converted.ptr - buffer.data()));
    }

    [[nodiscard]] const char* Data() const noexcept
    {
        return m_data.data();
    }

    [[nodiscard]] u32 Size() const noexcept
    {
        return m_size;
    }

private:
    std::array<char, kMaxMaterialFileBytes> m_data{};
    u32 m_size = 0;
};

[[nodiscard]] bool AppendState(TextBuilder& builder, const MaterialFile& file) noexcept
{
    const char* cull = file.renderState.cull == MaterialCullMode::None
                           ? "none"
                           : (file.renderState.cull == MaterialCullMode::Back ? "back" : "front");
    const char* blend = file.renderState.blend == MaterialBlendMode::Off ? "off" : "alpha";
    return builder.AppendLiteral("state cull ") && builder.AppendLiteral(cull) &&
           builder.AppendLiteral("\nstate blend ") && builder.AppendLiteral(blend) &&
           builder.AppendLiteral(file.renderState.depthTest ? "\nstate depth_test on"
                                                            : "\nstate depth_test off") &&
           builder.AppendLiteral(file.renderState.depthWrite ? "\nstate depth_write on\n"
                                                             : "\nstate depth_write off\n");
}

[[nodiscard]] bool AppendParameter(TextBuilder& builder,
                                   const MaterialFileParameter& parameter) noexcept
{
    if (!builder.AppendLiteral("parameter ") ||
        !builder.AppendQuoted(parameter.name.data(),
                              static_cast<u32>(BoundedLength(parameter.name))) ||
        !builder.AppendLiteral(" ") ||
        !builder.AppendLiteral(shaders::ValueTypeName(parameter.defaultValue.type)) ||
        !builder.AppendLiteral(" default"))
    {
        return false;
    }
    const u32 count = ValueComponents(parameter.defaultValue.type);
    for (u32 component = 0; component < count; ++component)
    {
        if (!builder.AppendLiteral(" ") ||
            !builder.AppendFloat(parameter.defaultValue.components[component]))
        {
            return false;
        }
    }
    if (!builder.AppendLiteral(" range"))
    {
        return false;
    }
    for (u32 component = 0; component < count; ++component)
    {
        if (!builder.AppendLiteral(" ") ||
            !builder.AppendFloat(parameter.minimum.components[component]))
        {
            return false;
        }
    }
    for (u32 component = 0; component < count; ++component)
    {
        if (!builder.AppendLiteral(" ") ||
            !builder.AppendFloat(parameter.maximum.components[component]))
        {
            return false;
        }
    }
    return builder.AppendLiteral("\n");
}

[[nodiscard]] bool AppendTexture(TextBuilder& builder, const MaterialFileTexture& texture) noexcept
{
    if (!builder.AppendLiteral("texture ") ||
        !builder.AppendQuoted(texture.name.data(), static_cast<u32>(BoundedLength(texture.name))) ||
        !builder.AppendLiteral(" default "))
    {
        return false;
    }
    if (texture.hasDefaultUri)
    {
        if (!builder.AppendQuoted(texture.defaultUri.data(), texture.defaultUriBytes))
        {
            return false;
        }
    }
    else if (!builder.AppendLiteral("none"))
    {
        return false;
    }
    return builder.AppendLiteral(texture.colorSpace == MaterialColorSpace::Srgb
                                     ? " color_space srgb\n"
                                     : " color_space linear\n");
}

}  // namespace

Result<MaterialFile, MaterialParseError> ParseMaterialFile(StringView text) noexcept
{
    if (text.Size() > kMaxMaterialFileBytes)
    {
        return Result<MaterialFile, MaterialParseError>::Err(
            ParseError(ErrorCode::OutOfRange, "material file: file exceeds 64 KiB", 1, 1));
    }
    if ((text.Data() == nullptr && !text.Empty()) || !IsValidUtf8(text) ||
        (text.Size() >= 3 && static_cast<u8>(text[0]) == 0xEFU &&
         static_cast<u8>(text[1]) == 0xBBU && static_cast<u8>(text[2]) == 0xBFU))
    {
        return Result<MaterialFile, MaterialParseError>::Err(ParseError(
            ErrorCode::InvalidArgument, "material file: invalid encoding or size", 1, 1));
    }

    MaterialFile file{};
    bool headerSeen = false;
    bool nameSeen = false;
    bool modelSeen = false;
    bool shaderSeen = false;
    bool queueSeen = false;
    bool cullSeen = false;
    bool blendSeen = false;
    bool depthTestSeen = false;
    bool depthWriteSeen = false;
    bool endSeen = false;
    u32 recordCount = 0;
    u32 lineNumber = 1;
    cyber::core::usize lineStart = 0;
    while (lineStart <= text.Size())
    {
        cyber::core::usize lineEnd = lineStart;
        while (lineEnd < text.Size() && text[lineEnd] != '\n')
        {
            ++lineEnd;
        }
        cyber::core::usize contentEnd = lineEnd;
        if (contentEnd > lineStart && text[contentEnd - 1] == '\r')
        {
            --contentEnd;
        }
        if (contentEnd - lineStart > kMaxMaterialFileRecordBytes)
        {
            return Result<MaterialFile, MaterialParseError>::Err(
                ParseError(ErrorCode::OutOfRange, "material file: record exceeds 1024 bytes",
                           lineNumber, kMaxMaterialFileRecordBytes + 1));
        }
        const StringView line(text.Data() == nullptr ? nullptr : text.Data() + lineStart,
                              contentEnd - lineStart);
        cyber::core::usize first = 0;
        while (first < line.Size() && IsSpace(line[first]))
        {
            ++first;
        }
        if (first < line.Size() && line[first] != '#')
        {
            if (endSeen)
            {
                return Result<MaterialFile, MaterialParseError>::Err(
                    ParseError(ErrorCode::InvalidArgument, "material file: data after end",
                               lineNumber, static_cast<u32>(first + 1)));
            }
            if (++recordCount > kMaxMaterialFileRecords)
            {
                return Result<MaterialFile, MaterialParseError>::Err(
                    ParseError(ErrorCode::OutOfRange, "material file: record count exceeds limit",
                               lineNumber, static_cast<u32>(first + 1)));
            }
            TokenList tokens{};
            MaterialParseError tokenError{};
            if (!Tokenize(line, lineNumber, tokens, tokenError))
            {
                return Result<MaterialFile, MaterialParseError>::Err(tokenError);
            }
            if (!headerSeen)
            {
                if (tokens.count != 2 || !Word(tokens, 0, "CYBERMAT") || !Word(tokens, 1, "1"))
                {
                    return Result<MaterialFile, MaterialParseError>::Err(ParseError(
                        ErrorCode::Unsupported, "material file: expected CYBERMAT 1 header",
                        lineNumber, static_cast<u32>(first + 1)));
                }
                headerSeen = true;
            }
            else if (Word(tokens, 0, "name"))
            {
                if (tokens.count != 2 || nameSeen || !tokens.values[1].quoted ||
                    !CopyText(file.name, tokens.values[1].value))
                {
                    return Result<MaterialFile, MaterialParseError>::Err(ParseError(
                        ErrorCode::InvalidArgument, "material file: invalid or duplicate name",
                        lineNumber, tokens.values[0].column));
                }
                nameSeen = true;
            }
            else if (Word(tokens, 0, "model"))
            {
                if (tokens.count != 2 || modelSeen || !Word(tokens, 1, "metallic_roughness"))
                {
                    return Result<MaterialFile, MaterialParseError>::Err(ParseError(
                        ErrorCode::InvalidArgument, "material file: invalid or duplicate model",
                        lineNumber, tokens.values[0].column));
                }
                file.model = MaterialModel::MetallicRoughness;
                modelSeen = true;
            }
            else if (Word(tokens, 0, "shader"))
            {
                if (tokens.count != 3 || shaderSeen || !tokens.values[1].quoted ||
                    !tokens.values[2].quoted ||
                    !CopyUri(file.vertexShaderUri, tokens.values[1].value, nullptr) ||
                    !CopyUri(file.pixelShaderUri, tokens.values[2].value, nullptr))
                {
                    return Result<MaterialFile, MaterialParseError>::Err(ParseError(
                        ErrorCode::InvalidArgument, "material file: invalid or duplicate shader",
                        lineNumber, tokens.values[0].column));
                }
                shaderSeen = true;
            }
            else if (Word(tokens, 0, "queue"))
            {
                if (tokens.count != 2 || queueSeen || !ParseQueue(tokens.values[1], file.queue))
                {
                    return Result<MaterialFile, MaterialParseError>::Err(ParseError(
                        ErrorCode::InvalidArgument, "material file: invalid or duplicate queue",
                        lineNumber, tokens.values[0].column));
                }
                queueSeen = true;
            }
            else if (Word(tokens, 0, "state"))
            {
                if (tokens.count != 3)
                {
                    return Result<MaterialFile, MaterialParseError>::Err(
                        ParseError(ErrorCode::InvalidArgument, "material file: malformed state",
                                   lineNumber, tokens.values[0].column));
                }
                if (Word(tokens, 1, "cull") && !cullSeen)
                {
                    if (Word(tokens, 2, "none"))
                    {
                        file.renderState.cull = MaterialCullMode::None;
                    }
                    else if (Word(tokens, 2, "back"))
                    {
                        file.renderState.cull = MaterialCullMode::Back;
                    }
                    else if (Word(tokens, 2, "front"))
                    {
                        file.renderState.cull = MaterialCullMode::Front;
                    }
                    else
                    {
                        return Result<MaterialFile, MaterialParseError>::Err(ParseError(
                            ErrorCode::InvalidArgument, "material file: invalid cull state",
                            lineNumber, tokens.values[2].column));
                    }
                    cullSeen = true;
                }
                else if (Word(tokens, 1, "blend") && !blendSeen)
                {
                    if (Word(tokens, 2, "off"))
                    {
                        file.renderState.blend = MaterialBlendMode::Off;
                    }
                    else if (Word(tokens, 2, "alpha"))
                    {
                        file.renderState.blend = MaterialBlendMode::Alpha;
                    }
                    else
                    {
                        return Result<MaterialFile, MaterialParseError>::Err(ParseError(
                            ErrorCode::InvalidArgument, "material file: invalid blend state",
                            lineNumber, tokens.values[2].column));
                    }
                    blendSeen = true;
                }
                else if (Word(tokens, 1, "depth_test") && !depthTestSeen)
                {
                    if (!Word(tokens, 2, "on") && !Word(tokens, 2, "off"))
                    {
                        return Result<MaterialFile, MaterialParseError>::Err(ParseError(
                            ErrorCode::InvalidArgument, "material file: invalid depth_test state",
                            lineNumber, tokens.values[2].column));
                    }
                    file.renderState.depthTest = Word(tokens, 2, "on");
                    depthTestSeen = true;
                }
                else if (Word(tokens, 1, "depth_write") && !depthWriteSeen)
                {
                    if (!Word(tokens, 2, "on") && !Word(tokens, 2, "off"))
                    {
                        return Result<MaterialFile, MaterialParseError>::Err(ParseError(
                            ErrorCode::InvalidArgument, "material file: invalid depth_write state",
                            lineNumber, tokens.values[2].column));
                    }
                    file.renderState.depthWrite = Word(tokens, 2, "on");
                    depthWriteSeen = true;
                }
                else
                {
                    return Result<MaterialFile, MaterialParseError>::Err(ParseError(
                        ErrorCode::InvalidArgument, "material file: unknown or duplicate state",
                        lineNumber, tokens.values[1].column));
                }
            }
            else if (Word(tokens, 0, "parameter"))
            {
                if (tokens.count < 8 || file.parameterCount == kMaxMaterialParameters ||
                    !tokens.values[1].quoted || !IsIdentifier(tokens.values[1].value))
                {
                    return Result<MaterialFile, MaterialParseError>::Err(
                        ParseError(ErrorCode::InvalidArgument, "material file: malformed parameter",
                                   lineNumber, tokens.values[0].column));
                }
                MaterialFileParameter parameter{};
                if (!CopyText(parameter.name, tokens.values[1].value))
                {
                    return Result<MaterialFile, MaterialParseError>::Err(ParseError(
                        ErrorCode::InvalidArgument, "material file: invalid parameter name",
                        lineNumber, tokens.values[1].column));
                }
                for (u32 prior = 0; prior < file.parameterCount; ++prior)
                {
                    if (NameView(file.parameters[prior].name) == tokens.values[1].value)
                    {
                        return Result<MaterialFile, MaterialParseError>::Err(ParseError(
                            ErrorCode::InvalidArgument, "material file: duplicate parameter",
                            lineNumber, tokens.values[1].column));
                    }
                }
                parameter.defaultValue.type = ParseValueType(tokens.values[2]);
                const u32 components = ValueComponents(parameter.defaultValue.type);
                if (components == 0 || !Word(tokens, 3, "default") ||
                    tokens.count != 5 + components * 3 || !Word(tokens, 4 + components, "range"))
                {
                    return Result<MaterialFile, MaterialParseError>::Err(
                        ParseError(ErrorCode::InvalidArgument,
                                   "material file: invalid parameter type or fields", lineNumber,
                                   tokens.values[2].column));
                }
                parameter.minimum.type = parameter.defaultValue.type;
                parameter.maximum.type = parameter.defaultValue.type;
                for (u32 component = 0; component < components; ++component)
                {
                    if (!ParseFloat(tokens.values[4 + component],
                                    parameter.defaultValue.components[component]) ||
                        !ParseFloat(tokens.values[5 + components + component],
                                    parameter.minimum.components[component]) ||
                        !ParseFloat(tokens.values[5 + components * 2 + component],
                                    parameter.maximum.components[component]) ||
                        parameter.minimum.components[component] >
                            parameter.maximum.components[component] ||
                        parameter.defaultValue.components[component] <
                            parameter.minimum.components[component] ||
                        parameter.defaultValue.components[component] >
                            parameter.maximum.components[component])
                    {
                        return Result<MaterialFile, MaterialParseError>::Err(ParseError(
                            ErrorCode::InvalidArgument, "material file: invalid parameter range",
                            lineNumber, tokens.values[4 + component].column));
                    }
                }
                if (!GltfParameterMatches(tokens.values[1].value, parameter.defaultValue,
                                          parameter.minimum, parameter.maximum))
                {
                    return Result<MaterialFile, MaterialParseError>::Err(ParseError(
                        ErrorCode::InvalidArgument,
                        "material file: glTF material factor has an invalid type or range",
                        lineNumber, tokens.values[1].column));
                }
                file.parameters[file.parameterCount++] = parameter;
            }
            else if (Word(tokens, 0, "texture"))
            {
                if (tokens.count != 6 || file.textureSlotCount == kMaxMaterialTextureSlots ||
                    !tokens.values[1].quoted || !IsIdentifier(tokens.values[1].value) ||
                    !ParseKnownTexture(tokens.values[1]) || !Word(tokens, 2, "default") ||
                    !Word(tokens, 4, "color_space"))
                {
                    return Result<MaterialFile, MaterialParseError>::Err(ParseError(
                        ErrorCode::InvalidArgument, "material file: malformed texture slot",
                        lineNumber, tokens.values[0].column));
                }
                MaterialFileTexture texture{};
                if (!CopyText(texture.name, tokens.values[1].value))
                {
                    return Result<MaterialFile, MaterialParseError>::Err(ParseError(
                        ErrorCode::InvalidArgument, "material file: invalid texture slot name",
                        lineNumber, tokens.values[1].column));
                }
                for (u32 prior = 0; prior < file.textureSlotCount; ++prior)
                {
                    if (NameView(file.textureSlots[prior].name) == tokens.values[1].value)
                    {
                        return Result<MaterialFile, MaterialParseError>::Err(ParseError(
                            ErrorCode::InvalidArgument, "material file: duplicate texture slot",
                            lineNumber, tokens.values[1].column));
                    }
                }
                if (tokens.values[3].quoted)
                {
                    texture.hasDefaultUri = CopyUri(texture.defaultUri, tokens.values[3].value,
                                                    &texture.defaultUriBytes);
                    if (!texture.hasDefaultUri)
                    {
                        return Result<MaterialFile, MaterialParseError>::Err(ParseError(
                            ErrorCode::InvalidArgument, "material file: invalid texture URI",
                            lineNumber, tokens.values[3].column));
                    }
                }
                else if (!Word(tokens, 3, "none"))
                {
                    return Result<MaterialFile, MaterialParseError>::Err(ParseError(
                        ErrorCode::InvalidArgument, "material file: expected URI or none",
                        lineNumber, tokens.values[3].column));
                }
                if (Word(tokens, 5, "srgb"))
                {
                    texture.colorSpace = MaterialColorSpace::Srgb;
                }
                else if (Word(tokens, 5, "linear"))
                {
                    texture.colorSpace = MaterialColorSpace::Linear;
                }
                else
                {
                    return Result<MaterialFile, MaterialParseError>::Err(ParseError(
                        ErrorCode::InvalidArgument, "material file: invalid texture color space",
                        lineNumber, tokens.values[5].column));
                }
                if (!TextureColorSpaceMatches(tokens.values[1].value, texture.colorSpace))
                {
                    return Result<MaterialFile, MaterialParseError>::Err(ParseError(
                        ErrorCode::InvalidArgument,
                        "material file: texture color space conflicts with its glTF role",
                        lineNumber, tokens.values[5].column));
                }
                file.textureSlots[file.textureSlotCount++] = texture;
            }
            else if (Word(tokens, 0, "end"))
            {
                if (tokens.count != 1)
                {
                    return Result<MaterialFile, MaterialParseError>::Err(ParseError(
                        ErrorCode::InvalidArgument, "material file: malformed end record",
                        lineNumber, tokens.values[0].column));
                }
                endSeen = true;
            }
            else
            {
                return Result<MaterialFile, MaterialParseError>::Err(
                    ParseError(ErrorCode::InvalidArgument, "material file: unknown record",
                               lineNumber, tokens.values[0].column));
            }
        }
        if (lineEnd == text.Size())
        {
            break;
        }
        lineStart = lineEnd + 1;
        ++lineNumber;
    }

    if (!headerSeen || !nameSeen || !modelSeen || !shaderSeen || !queueSeen || !endSeen)
    {
        return Result<MaterialFile, MaterialParseError>::Err(ParseError(
            ErrorCode::InvalidArgument, "material file: missing required record", lineNumber, 1));
    }
    return Result<MaterialFile, MaterialParseError>::Ok(file);
}

Result<u32, Error> WriteMaterialFile(const MaterialFile& file, Span<memory::Byte> output) noexcept
{
    const StringView name = BoundedView(file.name);
    const StringView vertex = BoundedView(file.vertexShaderUri);
    const StringView pixel = BoundedView(file.pixelShaderUri);
    if (!IsIdentifier(name) || !IsValidUri(vertex) || !IsValidUri(pixel) ||
        file.model != MaterialModel::MetallicRoughness ||
        (file.queue != MaterialQueue::Opaque && file.queue != MaterialQueue::Masked &&
         file.queue != MaterialQueue::Transparent) ||
        file.parameterCount > kMaxMaterialParameters ||
        file.textureSlotCount > kMaxMaterialTextureSlots ||
        (file.renderState.cull != MaterialCullMode::None &&
         file.renderState.cull != MaterialCullMode::Back &&
         file.renderState.cull != MaterialCullMode::Front) ||
        (file.renderState.blend != MaterialBlendMode::Off &&
         file.renderState.blend != MaterialBlendMode::Alpha))
    {
        return Result<u32, Error>::Err(
            Error{ErrorCode::InvalidArgument, "material file: invalid value"});
    }

    TextBuilder builder;
    if (!builder.AppendLiteral("CYBERMAT 1\nname ") ||
        !builder.AppendQuoted(name.Data(), static_cast<u32>(name.Size())) ||
        !builder.AppendLiteral("\nmodel metallic_roughness\nshader ") ||
        !builder.AppendQuoted(vertex.Data(), static_cast<u32>(vertex.Size())) ||
        !builder.AppendLiteral(" ") ||
        !builder.AppendQuoted(pixel.Data(), static_cast<u32>(pixel.Size())) ||
        !builder.AppendLiteral("\nqueue "))
    {
        return Result<u32, Error>::Err(
            Error{ErrorCode::OutOfRange, "material file: serialized text exceeds limit"});
    }
    const char* queue = file.queue == MaterialQueue::Opaque
                            ? "opaque"
                            : (file.queue == MaterialQueue::Masked ? "masked" : "transparent");
    if (!builder.AppendLiteral(queue) || !builder.AppendLiteral("\n") ||
        !AppendState(builder, file))
    {
        return Result<u32, Error>::Err(
            Error{ErrorCode::OutOfRange, "material file: serialized text exceeds limit"});
    }

    for (u32 index = 0; index < file.parameterCount; ++index)
    {
        const MaterialFileParameter& parameter = file.parameters[index];
        const StringView parameterName = NameView(parameter.name);
        if (!IsIdentifier(parameterName) || !FileValueValid(parameter.defaultValue) ||
            parameter.defaultValue.type != parameter.minimum.type ||
            parameter.defaultValue.type != parameter.maximum.type ||
            !GltfParameterMatches(parameterName, parameter.defaultValue, parameter.minimum,
                                  parameter.maximum))
        {
            return Result<u32, Error>::Err(
                Error{ErrorCode::InvalidArgument, "material file: invalid parameter"});
        }
        for (u32 prior = 0; prior < index; ++prior)
        {
            if (NameView(file.parameters[prior].name) == parameterName)
            {
                return Result<u32, Error>::Err(
                    Error{ErrorCode::InvalidArgument, "material file: duplicate parameter"});
            }
        }
        const u32 components = ValueComponents(parameter.defaultValue.type);
        for (u32 component = 0; component < components; ++component)
        {
            if (!std::isfinite(parameter.minimum.components[component]) ||
                !std::isfinite(parameter.maximum.components[component]) ||
                parameter.minimum.components[component] > parameter.maximum.components[component] ||
                parameter.defaultValue.components[component] <
                    parameter.minimum.components[component] ||
                parameter.defaultValue.components[component] >
                    parameter.maximum.components[component])
            {
                return Result<u32, Error>::Err(
                    Error{ErrorCode::InvalidArgument, "material file: invalid parameter range"});
            }
        }
        if (!AppendParameter(builder, parameter))
        {
            return Result<u32, Error>::Err(
                Error{ErrorCode::OutOfRange, "material file: serialized text exceeds limit"});
        }
    }

    for (u32 index = 0; index < file.textureSlotCount; ++index)
    {
        const MaterialFileTexture& texture = file.textureSlots[index];
        const StringView textureName = NameView(texture.name);
        const StringView textureUri(texture.defaultUri.data(), texture.defaultUriBytes);
        if (!IsIdentifier(textureName) || !ParseKnownTexture(Token{textureName}) ||
            !IsKnownColorSpace(texture.colorSpace) ||
            !TextureColorSpaceMatches(textureName, texture.colorSpace) ||
            texture.defaultUriBytes > kMaxMaterialUriBytes ||
            (texture.hasDefaultUri && !IsValidUri(textureUri)) ||
            (!texture.hasDefaultUri && texture.defaultUriBytes != 0))
        {
            return Result<u32, Error>::Err(
                Error{ErrorCode::InvalidArgument, "material file: invalid texture slot"});
        }
        for (u32 prior = 0; prior < index; ++prior)
        {
            if (NameView(file.textureSlots[prior].name) == textureName)
            {
                return Result<u32, Error>::Err(
                    Error{ErrorCode::InvalidArgument, "material file: duplicate texture slot"});
            }
        }
        if (!AppendTexture(builder, texture))
        {
            return Result<u32, Error>::Err(
                Error{ErrorCode::OutOfRange, "material file: serialized text exceeds limit"});
        }
    }
    if (!builder.AppendLiteral("end\n"))
    {
        return Result<u32, Error>::Err(
            Error{ErrorCode::OutOfRange, "material file: serialized text exceeds limit"});
    }
    if (output.Size() < builder.Size() || output.Data() == nullptr)
    {
        return Result<u32, Error>::Err(
            Error{ErrorCode::OutOfRange, "material file: output buffer is too small"});
    }
    std::memcpy(output.Data(), builder.Data(), builder.Size());
    return Result<u32, Error>::Ok(builder.Size());
}

}  // namespace cyber::renderer

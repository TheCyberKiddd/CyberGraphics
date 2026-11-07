#include <cyber/assets/json.h>

#include <cstring>

namespace cyber::assets
{
namespace
{

using cyber::core::ErrorCode;
using cyber::core::f64;
using cyber::core::i32;
using cyber::core::usize;

constexpr u32 kNoNode = 0xffffffffu;

/// Exactly rounded decimal powers used to scale parsed mantissas.
constexpr f64 kPowersOfTen[] = {1.0e0,  1.0e1,  1.0e2,  1.0e3,  1.0e4,  1.0e5,  1.0e6,  1.0e7,
                                1.0e8,  1.0e9,  1.0e10, 1.0e11, 1.0e12, 1.0e13, 1.0e14, 1.0e15,
                                1.0e16, 1.0e17, 1.0e18, 1.0e19, 1.0e20, 1.0e21, 1.0e22};

[[nodiscard]] bool IsSpace(char value) noexcept
{
    return value == ' ' || value == '\t' || value == '\n' || value == '\r';
}

[[nodiscard]] bool IsDigit(char value) noexcept
{
    return value >= '0' && value <= '9';
}

[[nodiscard]] int HexValue(char value) noexcept
{
    if (value >= '0' && value <= '9')
    {
        return value - '0';
    }
    if (value >= 'a' && value <= 'f')
    {
        return 10 + (value - 'a');
    }
    if (value >= 'A' && value <= 'F')
    {
        return 10 + (value - 'A');
    }
    return -1;
}

/// Rejects overlong encodings, surrogates, and code points beyond U+10FFFF.
[[nodiscard]] bool IsValidUtf8(const char* data, usize size) noexcept
{
    usize index = 0;
    while (index < size)
    {
        const auto lead = static_cast<unsigned char>(data[index]);
        if (lead < 0x80)
        {
            ++index;
            continue;
        }

        u32 codePoint = 0;
        usize extra = 0;
        if ((lead & 0xe0u) == 0xc0u)
        {
            codePoint = lead & 0x1fu;
            extra = 1;
        }
        else if ((lead & 0xf0u) == 0xe0u)
        {
            codePoint = lead & 0x0fu;
            extra = 2;
        }
        else if ((lead & 0xf8u) == 0xf0u)
        {
            codePoint = lead & 0x07u;
            extra = 3;
        }
        else
        {
            return false;
        }
        if (index + extra >= size)
        {
            return false;
        }
        for (usize offset = 1; offset <= extra; ++offset)
        {
            const auto continuation = static_cast<unsigned char>(data[index + offset]);
            if ((continuation & 0xc0u) != 0x80u)
            {
                return false;
            }
            codePoint = (codePoint << 6u) | (continuation & 0x3fu);
        }

        if (extra == 1 && codePoint < 0x80u)
        {
            return false;
        }
        if (extra == 2 && codePoint < 0x800u)
        {
            return false;
        }
        if (extra == 3 && codePoint < 0x10000u)
        {
            return false;
        }
        if (codePoint > 0x10ffffu || (codePoint >= 0xd800u && codePoint <= 0xdfffu))
        {
            return false;
        }
        index += extra + 1;
    }
    return true;
}

}  // namespace

// ---------------------------------------------------------------------------
// JsonValue
// ---------------------------------------------------------------------------

JsonType JsonValue::Type() const noexcept
{
    if (m_document == nullptr || !m_document->HasNode(m_index))
    {
        return JsonType::Null;
    }
    return m_document->NodeAt(m_index).type;
}

bool JsonValue::AsBool() const noexcept
{
    if (m_document == nullptr || !m_document->HasNode(m_index))
    {
        return false;
    }
    const JsonDocument::Node& node = m_document->NodeAt(m_index);
    return node.type == JsonType::Bool && node.boolean;
}

f64 JsonValue::AsNumber() const noexcept
{
    if (m_document == nullptr || !m_document->HasNode(m_index))
    {
        return 0.0;
    }
    const JsonDocument::Node& node = m_document->NodeAt(m_index);
    return node.type == JsonType::Number ? node.number : 0.0;
}

StringView JsonValue::AsString() const noexcept
{
    if (m_document == nullptr || !m_document->HasNode(m_index))
    {
        return {};
    }
    const JsonDocument::Node& node = m_document->NodeAt(m_index);
    if (node.type != JsonType::String)
    {
        return {};
    }
    return m_document->TextView(node.textOffset, node.textLength);
}

bool JsonValue::IsInteger() const noexcept
{
    if (m_document == nullptr || !m_document->HasNode(m_index))
    {
        return false;
    }
    const JsonDocument::Node& node = m_document->NodeAt(m_index);
    return node.type == JsonType::Number && node.isInteger;
}

usize JsonValue::Size() const noexcept
{
    if (m_document == nullptr || !m_document->HasNode(m_index))
    {
        return 0;
    }
    const JsonDocument::Node& node = m_document->NodeAt(m_index);
    return node.count;
}

JsonValue JsonValue::Element(usize index) const noexcept
{
    if (Type() != JsonType::Array || index >= Size())
    {
        return JsonValue();
    }
    const JsonDocument::Node& node = m_document->NodeAt(m_index);
    u32 current = node.first;
    for (usize skipped = 0; skipped < index; ++skipped)
    {
        current = m_document->NodeAt(current).next;
    }
    return JsonValue(m_document, current);
}

JsonValue JsonValue::Member(StringView name) const noexcept
{
    if (Type() != JsonType::Object)
    {
        return JsonValue();
    }
    const JsonDocument::Node& node = m_document->NodeAt(m_index);
    u32 current = node.first;
    while (current != kNoNode && m_document->HasNode(current))
    {
        const JsonDocument::Node& member = m_document->NodeAt(current);
        const StringView key = m_document->TextView(member.keyOffset, member.keyLength);
        if (key.Size() == name.Size() && std::memcmp(key.Data(), name.Data(), name.Size()) == 0)
        {
            return JsonValue(m_document, current);
        }
        current = member.next;
    }
    return JsonValue();
}

StringView JsonValue::MemberName(usize index) const noexcept
{
    const JsonValue member = MemberAt(index);
    if (!member.IsValid())
    {
        return {};
    }
    const JsonDocument::Node& node = m_document->NodeAt(member.m_index);
    return m_document->TextView(node.keyOffset, node.keyLength);
}

JsonValue JsonValue::MemberAt(usize index) const noexcept
{
    if (Type() != JsonType::Object || index >= Size())
    {
        return JsonValue();
    }
    const JsonDocument::Node& node = m_document->NodeAt(m_index);
    u32 current = node.first;
    for (usize skipped = 0; skipped < index; ++skipped)
    {
        current = m_document->NodeAt(current).next;
    }
    return JsonValue(m_document, current);
}

bool JsonValue::HasMember(StringView name) const noexcept
{
    return Member(name).IsValid();
}

bool JsonValue::TryGetBool(bool& value) const noexcept
{
    if (Type() != JsonType::Bool)
    {
        return false;
    }
    value = AsBool();
    return true;
}

bool JsonValue::TryGetNumber(f64& value) const noexcept
{
    if (Type() != JsonType::Number)
    {
        return false;
    }
    value = AsNumber();
    return true;
}

bool JsonValue::TryGetU32(u32& value) const noexcept
{
    if (Type() != JsonType::Number || !IsInteger())
    {
        return false;
    }
    const f64 number = AsNumber();
    if (number < 0.0 || number > 4294967295.0)
    {
        return false;
    }
    value = static_cast<u32>(number);
    return static_cast<f64>(value) == number;
}

bool JsonValue::TryGetString(StringView& value) const noexcept
{
    if (Type() != JsonType::String)
    {
        return false;
    }
    value = AsString();
    return true;
}

// ---------------------------------------------------------------------------
// JsonDocument
// ---------------------------------------------------------------------------

JsonDocument::JsonDocument(Allocator& allocator) noexcept
    : m_allocator(&allocator), m_nodes(allocator), m_text(allocator)
{
}

JsonDocument::JsonDocument(JsonDocument&& other) noexcept
    : m_allocator(other.m_allocator), m_nodes(std::move(other.m_nodes)),
      m_text(std::move(other.m_text))
{
    other.m_allocator = nullptr;
}

JsonDocument& JsonDocument::operator=(JsonDocument&& other) noexcept
{
    if (this != &other)
    {
        m_nodes = std::move(other.m_nodes);
        m_text = std::move(other.m_text);
        m_allocator = other.m_allocator;
        other.m_allocator = nullptr;
    }
    return *this;
}

StringView JsonDocument::TextView(u32 offset, u32 length) const noexcept
{
    if (offset > m_text.Size() || length > m_text.Size() - offset)
    {
        return {};
    }
    return StringView(m_text.Data() + offset, length);
}

JsonValue JsonDocument::Root() const noexcept
{
    if (m_nodes.Empty())
    {
        return JsonValue();
    }
    return JsonValue(this, 0);
}

usize JsonDocument::NodeCount() const noexcept
{
    return m_nodes.Size();
}

namespace detail
{

/// Bounded recursive-descent parser. Every buffer access is checked; on failure
/// exactly one diagnostic is recorded and parsing stops.
class JsonParser
{
public:
    JsonParser(cyber::memory::DynamicArray<JsonDocument::Node>& nodes,
               cyber::memory::DynamicArray<char>& text, StringView input,
               const JsonParseOptions& options) noexcept
        : m_nodes(nodes), m_text(text), m_input(input), m_options(options)
    {
    }

    [[nodiscard]] ImportError Error() const noexcept
    {
        return m_error;
    }

    [[nodiscard]] bool Failed() const noexcept
    {
        return m_failed;
    }

    [[nodiscard]] bool ParseRoot() noexcept
    {
        if (m_input.Size() >= 3 && static_cast<unsigned char>(m_input[0]) == 0xef &&
            static_cast<unsigned char>(m_input[1]) == 0xbb &&
            static_cast<unsigned char>(m_input[2]) == 0xbf)
        {
            m_cursor = 3;
        }

        // Skip leading whitespace to report a useful location for empty input.
        while (m_cursor < m_input.Size() && IsSpace(m_input[m_cursor]))
        {
            Advance();
        }
        if (AtEnd())
        {
            return Fail(ErrorCode::ParseError, "document is empty");
        }

        u32 root = 0;
        if (!ParseValue(root, 0))
        {
            return false;
        }
        SkipWhitespace();
        if (!AtEnd())
        {
            return Fail(ErrorCode::ParseError, "trailing content after the root value");
        }
        return true;
    }

    [[nodiscard]] usize NodeCount() const noexcept
    {
        return m_nodes.Size();
    }

private:
    [[nodiscard]] bool AtEnd() const noexcept
    {
        return m_cursor >= m_input.Size();
    }

    [[nodiscard]] char Peek() const noexcept
    {
        return m_input[m_cursor];
    }

    void Advance() noexcept
    {
        if (m_cursor < m_input.Size())
        {
            if (m_input[m_cursor] == '\n')
            {
                ++m_line;
                m_column = 1;
            }
            else
            {
                ++m_column;
            }
            ++m_cursor;
        }
    }

    void SkipWhitespace() noexcept
    {
        while (!AtEnd() && IsSpace(Peek()))
        {
            Advance();
        }
    }

    [[nodiscard]] bool Fail(ErrorCode code, const char* message) noexcept
    {
        if (!m_failed)
        {
            m_error = MakeTextError(code, message, m_line, m_column);
            m_failed = true;
        }
        return false;
    }

    [[nodiscard]] bool AppendNode(u32& index) noexcept
    {
        if (m_nodes.Size() >= m_options.maxNodes)
        {
            return Fail(ErrorCode::Overflow, "document exceeds the node limit");
        }
        if (m_nodes.Size() >= kNoNode)
        {
            return Fail(ErrorCode::Overflow, "document exceeds the node index range");
        }
        JsonDocument::Node node;
        node.next = kNoNode;
        index = static_cast<u32>(m_nodes.Size());
        if (!m_nodes.TryPushBack(node))
        {
            return Fail(ErrorCode::OutOfRange, "out of memory while growing the document");
        }
        return true;
    }

    // The trailing-node helper: append a text byte and return false on failure.
    [[nodiscard]] bool PushText(char value) noexcept
    {
        if (!m_text.TryPushBack(value))
        {
            return Fail(ErrorCode::OutOfRange, "out of memory while reading a string");
        }
        return true;
    }

    [[nodiscard]] bool ParseValue(u32& index, u32 depth) noexcept
    {
        if (depth > m_options.maxDepth)
        {
            return Fail(ErrorCode::ParseError, "nesting depth limit exceeded");
        }
        SkipWhitespace();
        if (AtEnd())
        {
            return Fail(ErrorCode::ParseError, "unexpected end of input");
        }

        switch (Peek())
        {
        case '{':
            return ParseObject(index, depth);
        case '[':
            return ParseArray(index, depth);
        case '"':
            return ParseStringNode(index);
        case 't':
            return ParseLiteral(index, "true", JsonType::Bool, true);
        case 'f':
            return ParseLiteral(index, "false", JsonType::Bool, false);
        case 'n':
            return ParseLiteral(index, "null", JsonType::Null, false);
        default:
            if (Peek() == '-' || IsDigit(Peek()))
            {
                return ParseNumber(index);
            }
            return Fail(ErrorCode::ParseError, "unexpected character");
        }
    }

    [[nodiscard]] bool ParseLiteral(u32& index, const char* literal, JsonType type,
                                    bool boolean) noexcept
    {
        const usize length = std::strlen(literal);
        if (m_cursor + length > m_input.Size() ||
            std::memcmp(m_input.Data() + m_cursor, literal, length) != 0)
        {
            return Fail(ErrorCode::ParseError, "invalid literal");
        }
        for (usize step = 0; step < length; ++step)
        {
            Advance();
        }
        if (!AppendNode(index))
        {
            return false;
        }
        m_nodes[index].type = type;
        m_nodes[index].boolean = boolean;
        return true;
    }

    [[nodiscard]] bool ParseNumber(u32& index) noexcept
    {
        const usize start = m_cursor;
        bool integer = true;
        if (Peek() == '-')
        {
            Advance();
            if (AtEnd())
            {
                return Fail(ErrorCode::ParseError, "truncated number");
            }
        }

        if (Peek() == '0')
        {
            Advance();
            if (!AtEnd() && IsDigit(Peek()))
            {
                return Fail(ErrorCode::ParseError, "number has a leading zero");
            }
        }
        else if (IsDigit(Peek()))
        {
            while (!AtEnd() && IsDigit(Peek()))
            {
                Advance();
            }
        }
        else
        {
            return Fail(ErrorCode::ParseError, "number has no digits");
        }

        if (!AtEnd() && Peek() == '.')
        {
            integer = false;
            Advance();
            if (AtEnd() || !IsDigit(Peek()))
            {
                return Fail(ErrorCode::ParseError, "fraction has no digits");
            }
            while (!AtEnd() && IsDigit(Peek()))
            {
                Advance();
            }
        }

        if (!AtEnd() && (Peek() == 'e' || Peek() == 'E'))
        {
            integer = false;
            Advance();
            if (!AtEnd() && (Peek() == '+' || Peek() == '-'))
            {
                Advance();
            }
            if (AtEnd() || !IsDigit(Peek()))
            {
                return Fail(ErrorCode::ParseError, "exponent has no digits");
            }
            while (!AtEnd() && IsDigit(Peek()))
            {
                Advance();
            }
        }

        const usize length = m_cursor - start;
        if (length > 64)
        {
            return Fail(ErrorCode::OutOfRange, "number is too long");
        }

        // Locale-independent decimal accumulation. The mantissa keeps at most 19
        // significant digits; further digits only move the exponent.
        char buffer[65] = {};
        std::memcpy(buffer, m_input.Data() + start, length);

        bool negative = false;
        usize position = 0;
        if (buffer[position] == '-')
        {
            negative = true;
            ++position;
        }
        f64 mantissa = 0.0;
        i32 exponent = 0;
        i32 significant = 0;
        while (position < length && IsDigit(buffer[position]))
        {
            if (significant < 19)
            {
                mantissa = mantissa * 10.0 + static_cast<f64>(buffer[position] - '0');
                if (mantissa != 0.0)
                {
                    ++significant;
                }
            }
            else
            {
                ++exponent;
            }
            ++position;
        }
        if (position < length && buffer[position] == '.')
        {
            ++position;
            while (position < length && IsDigit(buffer[position]))
            {
                if (significant < 19)
                {
                    mantissa = mantissa * 10.0 + static_cast<f64>(buffer[position] - '0');
                    if (mantissa != 0.0)
                    {
                        ++significant;
                    }
                    --exponent;
                }
                ++position;
            }
        }
        if (position < length && (buffer[position] == 'e' || buffer[position] == 'E'))
        {
            ++position;
            bool exponentNegative = false;
            if (buffer[position] == '+' || buffer[position] == '-')
            {
                exponentNegative = buffer[position] == '-';
                ++position;
            }
            i32 parsedExponent = 0;
            while (position < length && IsDigit(buffer[position]))
            {
                if (parsedExponent < 100000)
                {
                    parsedExponent = parsedExponent * 10 + (buffer[position] - '0');
                }
                ++position;
            }
            exponent += exponentNegative ? -parsedExponent : parsedExponent;
        }

        // Scale by exactly rounded powers of ten in bounded steps, so the result
        // never depends on the C library's locale-aware conversion.
        f64 value = mantissa;
        while (exponent > 0)
        {
            const i32 step = exponent > 22 ? 22 : exponent;
            value *= kPowersOfTen[step];
            exponent -= step;
            if (value > 1.0e308)
            {
                return Fail(ErrorCode::OutOfRange, "number is out of range");
            }
        }
        while (exponent < 0)
        {
            const i32 step = exponent < -22 ? 22 : -exponent;
            value /= kPowersOfTen[step];
            exponent += step;
            if (value != 0.0 && value < 1.0e-308)
            {
                value = 0.0;
            }
        }
        if (negative)
        {
            value = -value;
        }

        if (!AppendNode(index))
        {
            return false;
        }
        m_nodes[index].type = JsonType::Number;
        m_nodes[index].number = value;
        m_nodes[index].isInteger = integer;
        return true;
    }

    [[nodiscard]] bool ParseStringBody(StringView& text) noexcept
    {
        // Opening quote.
        Advance();
        const usize start = m_text.Size();
        while (!AtEnd())
        {
            const char value = Peek();
            if (value == '"')
            {
                Advance();
                text = StringView(m_text.Data() + start, m_text.Size() - start);
                return true;
            }
            if (static_cast<unsigned char>(value) < 0x20u)
            {
                return Fail(ErrorCode::ParseError, "raw control character in a string");
            }
            if (value != '\\')
            {
                if (!PushText(value))
                {
                    return false;
                }
                Advance();
                continue;
            }

            Advance();
            if (AtEnd())
            {
                return Fail(ErrorCode::ParseError, "unterminated escape sequence");
            }
            const char escape = Peek();
            Advance();
            switch (escape)
            {
            case '"':
            case '\\':
            case '/':
                if (!PushText(escape))
                {
                    return false;
                }
                break;
            case 'b':
                if (!PushText('\b'))
                {
                    return false;
                }
                break;
            case 'f':
                if (!PushText('\f'))
                {
                    return false;
                }
                break;
            case 'n':
                if (!PushText('\n'))
                {
                    return false;
                }
                break;
            case 'r':
                if (!PushText('\r'))
                {
                    return false;
                }
                break;
            case 't':
                if (!PushText('\t'))
                {
                    return false;
                }
                break;
            case 'u':
            {
                u32 codePoint = 0;
                if (!ReadHex4(codePoint))
                {
                    return false;
                }
                if (codePoint >= 0xd800u && codePoint <= 0xdbffu)
                {
                    if (m_cursor + 1 >= m_input.Size() || Peek() != '\\')
                    {
                        return Fail(ErrorCode::ParseError, "unpaired high surrogate");
                    }
                    Advance();
                    if (AtEnd() || Peek() != 'u')
                    {
                        return Fail(ErrorCode::ParseError, "unpaired high surrogate");
                    }
                    Advance();
                    u32 low = 0;
                    if (!ReadHex4(low))
                    {
                        return false;
                    }
                    if (low < 0xdc00u || low > 0xdfffu)
                    {
                        return Fail(ErrorCode::ParseError, "invalid low surrogate");
                    }
                    codePoint = 0x10000u + ((codePoint - 0xd800u) << 10u) + (low - 0xdc00u);
                }
                else if (codePoint >= 0xdc00u && codePoint <= 0xdfffu)
                {
                    return Fail(ErrorCode::ParseError, "unpaired low surrogate");
                }
                if (!AppendUtf8(codePoint))
                {
                    return false;
                }
                break;
            }
            default:
                return Fail(ErrorCode::ParseError, "unknown escape sequence");
            }
        }
        return Fail(ErrorCode::ParseError, "unterminated string");
    }

    [[nodiscard]] bool ReadHex4(u32& value) noexcept
    {
        value = 0;
        for (u32 digit = 0; digit < 4; ++digit)
        {
            if (AtEnd())
            {
                return Fail(ErrorCode::ParseError, "truncated unicode escape");
            }
            const int hex = HexValue(Peek());
            if (hex < 0)
            {
                return Fail(ErrorCode::ParseError, "invalid unicode escape");
            }
            value = (value << 4u) | static_cast<u32>(hex);
            Advance();
        }
        return true;
    }

    [[nodiscard]] bool AppendUtf8(u32 codePoint) noexcept
    {
        if (codePoint < 0x80u)
        {
            return PushText(static_cast<char>(codePoint));
        }
        if (codePoint < 0x800u)
        {
            return PushText(static_cast<char>(0xc0u | (codePoint >> 6u))) &&
                   PushText(static_cast<char>(0x80u | (codePoint & 0x3fu)));
        }
        if (codePoint < 0x10000u)
        {
            return PushText(static_cast<char>(0xe0u | (codePoint >> 12u))) &&
                   PushText(static_cast<char>(0x80u | ((codePoint >> 6u) & 0x3fu))) &&
                   PushText(static_cast<char>(0x80u | (codePoint & 0x3fu)));
        }
        return PushText(static_cast<char>(0xf0u | (codePoint >> 18u))) &&
               PushText(static_cast<char>(0x80u | ((codePoint >> 12u) & 0x3fu))) &&
               PushText(static_cast<char>(0x80u | ((codePoint >> 6u) & 0x3fu))) &&
               PushText(static_cast<char>(0x80u | (codePoint & 0x3fu)));
    }

    [[nodiscard]] bool ParseStringNode(u32& index) noexcept
    {
        const usize textStart = m_text.Size();
        StringView text;
        if (!ParseStringBody(text))
        {
            return false;
        }
        const usize length = m_text.Size() - textStart;
        if (!IsValidUtf8(m_text.Data() + textStart, length))
        {
            return Fail(ErrorCode::ParseError, "string is not valid UTF-8");
        }
        if (length > 0xffffffffull)
        {
            return Fail(ErrorCode::Overflow, "string is too long");
        }
        if (!AppendNode(index))
        {
            return false;
        }
        m_nodes[index].type = JsonType::String;
        m_nodes[index].textOffset = static_cast<u32>(textStart);
        m_nodes[index].textLength = static_cast<u32>(length);
        return true;
    }

    [[nodiscard]] bool ParseArray(u32& index, u32 depth) noexcept
    {
        if (!AppendNode(index))
        {
            return false;
        }
        const u32 nodeIndex = index;
        m_nodes[nodeIndex].type = JsonType::Array;
        Advance();  // '['
        SkipWhitespace();
        if (!AtEnd() && Peek() == ']')
        {
            Advance();
            return true;
        }

        u32 previous = kNoNode;
        u32 count = 0;
        while (true)
        {
            u32 child = 0;
            if (!ParseValue(child, depth + 1))
            {
                return false;
            }
            if (count == 0)
            {
                m_nodes[nodeIndex].first = child;
            }
            else
            {
                m_nodes[previous].next = child;
            }
            previous = child;
            ++count;

            SkipWhitespace();
            if (AtEnd())
            {
                return Fail(ErrorCode::ParseError, "unterminated array");
            }
            if (Peek() == ',')
            {
                Advance();
                SkipWhitespace();
                if (!AtEnd() && Peek() == ']')
                {
                    return Fail(ErrorCode::ParseError, "trailing comma in an array");
                }
                continue;
            }
            if (Peek() == ']')
            {
                Advance();
                m_nodes[nodeIndex].count = count;
                return true;
            }
            return Fail(ErrorCode::ParseError, "expected ',' or ']' in an array");
        }
    }

    [[nodiscard]] bool ParseObject(u32& index, u32 depth) noexcept
    {
        if (!AppendNode(index))
        {
            return false;
        }
        const u32 nodeIndex = index;
        m_nodes[nodeIndex].type = JsonType::Object;
        Advance();  // '{'
        SkipWhitespace();
        if (!AtEnd() && Peek() == '}')
        {
            Advance();
            return true;
        }

        u32 previous = kNoNode;
        u32 count = 0;
        while (true)
        {
            SkipWhitespace();
            if (AtEnd() || Peek() != '"')
            {
                return Fail(ErrorCode::ParseError, "expected a member name");
            }

            const usize keyStart = m_text.Size();
            StringView key;
            if (!ParseStringBody(key))
            {
                return false;
            }
            const usize keyLength = m_text.Size() - keyStart;
            if (keyLength > 0xffffffffull)
            {
                return Fail(ErrorCode::Overflow, "member name is too long");
            }

            SkipWhitespace();
            if (AtEnd() || Peek() != ':')
            {
                return Fail(ErrorCode::ParseError, "expected ':' after a member name");
            }
            Advance();

            u32 child = 0;
            if (!ParseValue(child, depth + 1))
            {
                return false;
            }
            m_nodes[child].keyOffset = static_cast<u32>(keyStart);
            m_nodes[child].keyLength = static_cast<u32>(keyLength);

            if (count == 0)
            {
                m_nodes[nodeIndex].first = child;
            }
            else
            {
                m_nodes[previous].next = child;
            }
            previous = child;
            ++count;

            SkipWhitespace();
            if (AtEnd())
            {
                return Fail(ErrorCode::ParseError, "unterminated object");
            }
            if (Peek() == ',')
            {
                Advance();
                SkipWhitespace();
                if (!AtEnd() && Peek() == '}')
                {
                    return Fail(ErrorCode::ParseError, "trailing comma in an object");
                }
                continue;
            }
            if (Peek() == '}')
            {
                Advance();
                m_nodes[nodeIndex].count = count;
                return true;
            }
            return Fail(ErrorCode::ParseError, "expected ',' or '}' in an object");
        }
    }

    cyber::memory::DynamicArray<JsonDocument::Node>& m_nodes;
    cyber::memory::DynamicArray<char>& m_text;
    StringView m_input;
    JsonParseOptions m_options;
    ImportError m_error;
    usize m_cursor = 0;
    u32 m_line = 1;
    u32 m_column = 1;
    bool m_failed = false;
};

}  // namespace detail

ImportResult<JsonDocument> JsonDocument::Parse(StringView text, Allocator& allocator,
                                               const JsonParseOptions& options) noexcept
{
    JsonDocument document(allocator);
    detail::JsonParser parser(document.m_nodes, document.m_text, text, options);
    if (!parser.ParseRoot())
    {
        return ImportResult<JsonDocument>::Err(parser.Error());
    }
    if (parser.NodeCount() == 0)
    {
        return ImportResult<JsonDocument>::Err(
            MakeError(ErrorCode::ParseError, "document has no root value"));
    }
    return ImportResult<JsonDocument>::Ok(std::move(document));
}

}  // namespace cyber::assets

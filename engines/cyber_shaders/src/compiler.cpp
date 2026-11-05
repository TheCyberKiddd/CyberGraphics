#include <cyber/memory/dynamic_array.h>
#include <cyber/shaders/compiler.h>

#include <array>
#include <limits>
#include <memory>
#include <utility>

namespace cyber::shaders
{
namespace
{

using cyber::core::Result;
using cyber::core::usize;
using cyber::memory::DynamicArray;
using cyber::memory::Span;
using cyber::memory::String;
using cyber::memory::StringView;

constexpr usize kMaximumSourceBytes = 1024U * 1024U;
constexpr usize kMaximumTokens = 16384;
constexpr usize kMaximumDeclarations = 32;
constexpr usize kMaximumExpressions = 256;
constexpr usize kMaximumStatements = 128;
constexpr usize kMaximumNesting = 32;
constexpr usize kMaximumWhenNesting = 8;
constexpr usize kMaximumShaderNameLength = 48;
constexpr usize kMaximumEnabledKeywords = 64;
// cs_5_x limits from the numthreads reference: X and Y up to 1024, Z up to 64,
// and at most 1024 threads in a group.
constexpr u32 kMaximumThreadsXY = 1024;
constexpr u32 kMaximumThreadsZ = 64;
constexpr u32 kMaximumThreadsPerGroup = 1024;
constexpr u32 kRegisterBytes = 16;

enum class TokenKind : u8
{
    Identifier,
    Number,
    LeftBrace,
    RightBrace,
    LeftParen,
    RightParen,
    Colon,
    Semicolon,
    Comma,
    Dot,
    At,
    Equal,
    End,
};

struct Token
{
    TokenKind kind = TokenKind::End;
    StringView text{};
    u32 line = 1;
    u32 column = 1;
};

enum class Type : u8
{
    Invalid = 0,
    Float,
    Float2,
    Float3,
    Float4,
    Float4x4,
    Uint3,
    Texture2D,
    Sampler,
    StorageTexture2D,
};

enum class DeclarationKind : u8
{
    Input,
    Output,
    Uniform,
    Texture,
    Sampler,
    StorageTexture,
};

struct Declaration
{
    DeclarationKind kind = DeclarationKind::Input;
    Type type = Type::Invalid;
    StringView name{};
    u32 location = 0;
    u32 registerIndex = 0;
    u32 registerSpace = 0;
    u32 line = 1;
    u32 column = 1;
    char registerClass = '\0';
    bool hasLocation = false;
    bool hasRegister = false;
    Builtin builtin = Builtin::None;
};

enum class ExpressionKind : u8
{
    Number,
    Identifier,
    Member,
    Call,
};

struct Expression
{
    ExpressionKind kind = ExpressionKind::Identifier;
    StringView name{};
    u32 left = 0;
    // Four covers the widest constructor, float4(x, y, z, w).
    std::array<u32, 4> arguments{};
    u32 argumentCount = 0;
    u32 line = 1;
    u32 column = 1;
};

enum class StatementKind : u8
{
    Assignment,
    Return,
    Store,
};

struct Statement
{
    StatementKind kind = StatementKind::Assignment;
    u32 target = 0;
    u32 expression = 0;
    u32 line = 1;
    u32 column = 1;
};

struct Keyword
{
    StringView name{};
    bool enabled = false;
};

struct Program
{
    Stage stage = Stage::Vertex;
    StringView name{};
    std::array<u32, 3> threads{};
    bool hasThreads = false;
    std::array<Keyword, kMaxKeywords> keywords{};
    usize keywordCount = 0;
    std::array<Declaration, kMaximumDeclarations> declarations{};
    std::array<Expression, kMaximumExpressions> expressions{};
    std::array<Statement, kMaximumStatements> statements{};
    usize declarationCount = 0;
    usize expressionCount = 0;
    usize statementCount = 0;
};

// Owns one Program in the caller's allocator. The program is several tens of
// kilobytes, which is too much for a worker thread's stack.
class ProgramStorage
{
public:
    explicit ProgramStorage(cyber::memory::Allocator& allocator) noexcept : m_allocator(allocator)
    {
        void* memory = allocator.Allocate(sizeof(Program), alignof(Program));
        if (memory != nullptr)
        {
            m_program = std::construct_at(static_cast<Program*>(memory));
        }
    }

    ~ProgramStorage()
    {
        if (m_program != nullptr)
        {
            std::destroy_at(m_program);
            m_allocator.Deallocate(m_program);
        }
    }

    ProgramStorage(const ProgramStorage&) = delete;
    ProgramStorage& operator=(const ProgramStorage&) = delete;

    [[nodiscard]] Program* Get() const noexcept
    {
        return m_program;
    }

private:
    cyber::memory::Allocator& m_allocator;
    Program* m_program = nullptr;
};

[[nodiscard]] Diagnostic MakeDiagnostic(DiagnosticKind kind, StringView sourceName, u32 line,
                                        u32 column, const char* message) noexcept
{
    return Diagnostic{kind, sourceName, line, column, message};
}

[[nodiscard]] Result<CompiledShader, Diagnostic>
Fail(DiagnosticKind kind, StringView sourceName, u32 line, u32 column, const char* message) noexcept
{
    return Result<CompiledShader, Diagnostic>::Err(
        MakeDiagnostic(kind, sourceName, line, column, message));
}

[[nodiscard]] bool IsIdentifierStart(char value) noexcept
{
    return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') || value == '_';
}

[[nodiscard]] bool IsIdentifierPart(char value) noexcept
{
    return IsIdentifierStart(value) || (value >= '0' && value <= '9');
}

[[nodiscard]] bool IsDigit(char value) noexcept
{
    return value >= '0' && value <= '9';
}

[[nodiscard]] bool IsWhitespace(char value) noexcept
{
    return value == ' ' || value == '\t' || value == '\r' || value == '\n';
}

[[nodiscard]] bool Lex(StringView source, StringView sourceName, DynamicArray<Token>& tokens,
                       Diagnostic& diagnostic) noexcept
{
    usize offset = 0;
    u32 line = 1;
    u32 column = 1;

    while (offset < source.Size())
    {
        const char current = source[offset];
        if (IsWhitespace(current))
        {
            ++offset;
            if (current == '\n')
            {
                ++line;
                column = 1;
            }
            else
            {
                ++column;
            }
            continue;
        }
        // Line comments run to the end of the line and are not tokens.
        if (current == '/' && offset + 1 < source.Size() && source[offset + 1] == '/')
        {
            while (offset < source.Size() && source[offset] != '\n')
            {
                ++offset;
                ++column;
            }
            continue;
        }

        const usize start = offset;
        const u32 startLine = line;
        const u32 startColumn = column;
        Token token{};
        token.line = line;
        token.column = column;

        if (IsIdentifierStart(current))
        {
            token.kind = TokenKind::Identifier;
            do
            {
                ++offset;
                ++column;
            } while (offset < source.Size() && IsIdentifierPart(source[offset]));
            if (offset - start > kMaxNameLength)
            {
                diagnostic = MakeDiagnostic(DiagnosticKind::Unsupported, sourceName, startLine,
                                            startColumn, "identifier is longer than 63 bytes");
                return false;
            }
        }
        else if (IsDigit(current))
        {
            token.kind = TokenKind::Number;
            do
            {
                ++offset;
                ++column;
            } while (offset < source.Size() && IsDigit(source[offset]));
            if (offset < source.Size() && source[offset] == '.')
            {
                ++offset;
                ++column;
                if (offset >= source.Size() || !IsDigit(source[offset]))
                {
                    diagnostic = MakeDiagnostic(DiagnosticKind::Lexical, sourceName, startLine,
                                                startColumn, "invalid numeric literal");
                    return false;
                }
                do
                {
                    ++offset;
                    ++column;
                } while (offset < source.Size() && IsDigit(source[offset]));
            }
        }
        else
        {
            switch (current)
            {
            case '{':
                token.kind = TokenKind::LeftBrace;
                break;
            case '}':
                token.kind = TokenKind::RightBrace;
                break;
            case '(':
                token.kind = TokenKind::LeftParen;
                break;
            case ')':
                token.kind = TokenKind::RightParen;
                break;
            case ':':
                token.kind = TokenKind::Colon;
                break;
            case ';':
                token.kind = TokenKind::Semicolon;
                break;
            case ',':
                token.kind = TokenKind::Comma;
                break;
            case '.':
                token.kind = TokenKind::Dot;
                break;
            case '@':
                token.kind = TokenKind::At;
                break;
            case '=':
                token.kind = TokenKind::Equal;
                break;
            default:
                diagnostic = MakeDiagnostic(DiagnosticKind::Lexical, sourceName, startLine,
                                            startColumn, "unsupported character");
                return false;
            }
            ++offset;
            ++column;
        }

        token.text = source.Substr(start, offset - start);
        if (tokens.Size() >= kMaximumTokens)
        {
            diagnostic = MakeDiagnostic(DiagnosticKind::Unsupported, sourceName, startLine,
                                        startColumn, "shader token limit exceeded");
            return false;
        }
        if (!tokens.TryPushBack(token))
        {
            diagnostic = MakeDiagnostic(DiagnosticKind::OutOfMemory, sourceName, startLine,
                                        startColumn, "unable to allocate compiler tokens");
            return false;
        }
    }

    Token end{};
    end.kind = TokenKind::End;
    end.line = line;
    end.column = column;
    if (!tokens.TryPushBack(end))
    {
        diagnostic =
            MakeDiagnostic(DiagnosticKind::OutOfMemory, sourceName, line, column, "out of memory");
        return false;
    }
    return true;
}

// HLSL keywords and reserved words from Microsoft's "Keywords" and "Reserved
// Words" appendices, plus the names the generator itself emits. A declared
// name from this list would make the generated HLSL fail to compile, so it is
// rejected at its source location instead.
constexpr const char* kReservedNames[] = {
    "AppendStructuredBuffer",
    "BlendState",
    "Buffer",
    "ByteAddressBuffer",
    "CompileShader",
    "ComputeShader",
    "ConsumeStructuredBuffer",
    "DepthStencilState",
    "DepthStencilView",
    "DomainShader",
    "GeometryShader",
    "Hullshader",
    "Input",
    "InputPatch",
    "LineStream",
    "NULL",
    "Output",
    "OutputPatch",
    "PixelShader",
    "PointStream",
    "RWBuffer",
    "RWByteAddressBuffer",
    "RWStructuredBuffer",
    "RWTexture1D",
    "RWTexture1DArray",
    "RWTexture2D",
    "RWTexture2DArray",
    "RWTexture3D",
    "RasterizerState",
    "RenderTargetView",
    "SamplerComparisonState",
    "SamplerState",
    "StructuredBuffer",
    "Texture1D",
    "Texture1DArray",
    "Texture2D",
    "Texture2DArray",
    "Texture2DMS",
    "Texture2DMSArray",
    "Texture3D",
    "TextureCube",
    "TextureCubeArray",
    "TriangleStream",
    "VertexShader",
    "asm",
    "asm_fragment",
    "auto",
    "break",
    "case",
    "catch",
    "cbuffer",
    "centroid",
    "char",
    "class",
    "column_major",
    "compile",
    "compile_fragment",
    "const",
    "const_cast",
    "continue",
    "default",
    "delete",
    "discard",
    "do",
    "dynamic_cast",
    "else",
    "enum",
    "explicit",
    "export",
    "extern",
    "false",
    "for",
    "friend",
    "fxgroup",
    "goto",
    "groupshared",
    "if",
    "in",
    "inline",
    "inout",
    "input",
    "interface",
    "line",
    "lineadj",
    "linear",
    "long",
    "main",
    "matrix",
    "mutable",
    "namespace",
    "new",
    "nointerpolation",
    "noperspective",
    "operator",
    "out",
    "output",
    "packoffset",
    "pass",
    "pixelfragment",
    "point",
    "precise",
    "private",
    "protected",
    "public",
    "register",
    "reinterpret_cast",
    "return",
    "row_major",
    "sample",
    "sampler",
    "shared",
    "short",
    "signed",
    "sizeof",
    "snorm",
    "stateblock",
    "stateblock_state",
    "static",
    "static_cast",
    "string",
    "struct",
    "switch",
    "tbuffer",
    "technique",
    "technique10",
    "technique11",
    "template",
    "texture",
    "this",
    "throw",
    "triangle",
    "triangleadj",
    "true",
    "try",
    "typedef",
    "typename",
    "uniform",
    "union",
    "unorm",
    "unsigned",
    "using",
    "vector",
    "vertexfragment",
    "virtual",
    "void",
    "volatile",
    "while",
};

// Scalar, vector, and matrix spellings of HLSL's numeric types: `float`,
// `float3`, `float4x4`, `min16int2`, and so on.
constexpr const char* kNumericTypeBases[] = {
    "bool",       "double",   "dword",      "float",    "half",      "int",
    "min10float", "min12int", "min16float", "min16int", "min16uint", "uint",
};

[[nodiscard]] bool IsDimension(char value) noexcept
{
    return value >= '1' && value <= '4';
}

[[nodiscard]] bool IsReservedName(StringView name) noexcept
{
    if (name.StartsWith("SV_") || name.StartsWith("sv_") || name.StartsWith("__"))
    {
        return true;
    }
    for (const char* reserved : kReservedNames)
    {
        if (name == StringView(reserved))
        {
            return true;
        }
    }
    for (const char* base : kNumericTypeBases)
    {
        const StringView prefix(base);
        if (!name.StartsWith(prefix))
        {
            continue;
        }
        const StringView suffix = name.Substr(prefix.Size());
        if (suffix.Empty() || (suffix.Size() == 1 && IsDimension(suffix[0])) ||
            (suffix.Size() == 3 && IsDimension(suffix[0]) && suffix[1] == 'x' &&
             IsDimension(suffix[2])))
        {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool ParseDigits(StringView text, u32& value) noexcept
{
    if (text.Empty())
    {
        return false;
    }
    u32 parsed = 0;
    for (usize index = 0; index < text.Size(); ++index)
    {
        const char digit = text[index];
        if (!IsDigit(digit) ||
            parsed > (std::numeric_limits<u32>::max() - static_cast<u32>(digit - '0')) / 10U)
        {
            return false;
        }
        parsed = parsed * 10U + static_cast<u32>(digit - '0');
    }
    value = parsed;
    return true;
}

class Parser
{
public:
    Parser(const DynamicArray<Token>& tokens, StringView sourceName,
           Span<const StringView> enabled) noexcept
        : m_tokens(tokens), m_sourceName(sourceName), m_enabled(enabled)
    {
    }

    [[nodiscard]] bool Parse(Program& program, Diagnostic& diagnostic) noexcept
    {
        if (!ExpectIdentifier("shader", "expected 'shader'"))
        {
            return SetDiagnostic(diagnostic);
        }
        if (MatchIdentifier("vertex"))
        {
            program.stage = Stage::Vertex;
        }
        else if (MatchIdentifier("pixel"))
        {
            program.stage = Stage::Pixel;
        }
        else if (MatchIdentifier("compute"))
        {
            program.stage = Stage::Compute;
        }
        else
        {
            SetError(DiagnosticKind::Syntax, Current(),
                     "expected 'vertex', 'pixel', or 'compute' stage");
            return SetDiagnostic(diagnostic);
        }

        if (!Check(TokenKind::Identifier))
        {
            SetError(DiagnosticKind::Syntax, Current(), "expected shader name");
            return SetDiagnostic(diagnostic);
        }
        const Token& name = Advance();
        if (name.text.Size() > kMaximumShaderNameLength)
        {
            SetError(DiagnosticKind::Unsupported, name, "shader name is longer than 48 bytes");
            return SetDiagnostic(diagnostic);
        }
        if (!CheckName(name))
        {
            return SetDiagnostic(diagnostic);
        }
        program.name = name.text;
        if (Check(TokenKind::At))
        {
            if (!ParseThreads(program))
            {
                return SetDiagnostic(diagnostic);
            }
        }
        if (!Expect(TokenKind::LeftBrace, "expected '{' after shader name"))
        {
            return SetDiagnostic(diagnostic);
        }

        if (!ParseBody(program, true, 0))
        {
            return SetDiagnostic(diagnostic);
        }

        if (!Expect(TokenKind::RightBrace, "expected '}' after shader body") ||
            !Check(TokenKind::End))
        {
            if (m_error.message == nullptr)
            {
                SetError(DiagnosticKind::Syntax, Current(), "unexpected text after shader body");
            }
            return SetDiagnostic(diagnostic);
        }
        return true;
    }

private:
    [[nodiscard]] const Token& Current() const noexcept
    {
        return m_tokens[m_position];
    }

    [[nodiscard]] const Token& Advance() noexcept
    {
        const Token& token = Current();
        if (token.kind != TokenKind::End)
        {
            ++m_position;
        }
        return token;
    }

    [[nodiscard]] bool Check(TokenKind kind) const noexcept
    {
        return Current().kind == kind;
    }

    [[nodiscard]] bool CheckNext(TokenKind kind) const noexcept
    {
        return m_position + 1 < m_tokens.Size() && m_tokens[m_position + 1].kind == kind;
    }

    [[nodiscard]] bool CheckIdentifier(StringView text) const noexcept
    {
        return Check(TokenKind::Identifier) && Current().text == text;
    }

    [[nodiscard]] bool MatchIdentifier(StringView text) noexcept
    {
        if (CheckIdentifier(text))
        {
            static_cast<void>(Advance());
            return true;
        }
        return false;
    }

    [[nodiscard]] bool ExpectIdentifier(StringView text, const char* message) noexcept
    {
        if (MatchIdentifier(text))
        {
            return true;
        }
        SetError(DiagnosticKind::Syntax, Current(), message);
        return false;
    }

    [[nodiscard]] bool Expect(TokenKind kind, const char* message) noexcept
    {
        if (Check(kind))
        {
            static_cast<void>(Advance());
            return true;
        }
        SetError(DiagnosticKind::Syntax, Current(), message);
        return false;
    }

    [[nodiscard]] bool Match(TokenKind kind) noexcept
    {
        if (Check(kind))
        {
            static_cast<void>(Advance());
            return true;
        }
        return false;
    }

    void SetError(DiagnosticKind kind, const Token& token, const char* message) noexcept
    {
        if (m_error.message == nullptr)
        {
            m_error = MakeDiagnostic(kind, m_sourceName, token.line, token.column, message);
        }
    }

    [[nodiscard]] bool SetDiagnostic(Diagnostic& diagnostic) const noexcept
    {
        diagnostic = m_error;
        return false;
    }

    // Rejects a declared name the generated HLSL could not use.
    [[nodiscard]] bool CheckName(const Token& token) noexcept
    {
        if (IsReservedName(token.text))
        {
            SetError(DiagnosticKind::Type, token, "name is reserved by HLSL or the generator");
            return false;
        }
        return true;
    }

    [[nodiscard]] bool IsEnabled(StringView name) const noexcept
    {
        for (usize index = 0; index < m_enabled.Size(); ++index)
        {
            if (m_enabled[index] == name)
            {
                return true;
            }
        }
        return false;
    }

    // `@threads(X, Y, Z)` after a compute shader's name.
    [[nodiscard]] bool ParseThreads(Program& program) noexcept
    {
        const Token at = Advance();
        if (!CheckIdentifier("threads"))
        {
            SetError(DiagnosticKind::Unsupported, Current(), "unsupported shader attribute");
            return false;
        }
        static_cast<void>(Advance());
        if (program.stage != Stage::Compute)
        {
            SetError(DiagnosticKind::Type, at, "only compute shaders declare @threads");
            return false;
        }
        if (!Expect(TokenKind::LeftParen, "expected '(' after threads"))
        {
            return false;
        }
        for (u32 axis = 0; axis < 3; ++axis)
        {
            if (axis != 0 && !Expect(TokenKind::Comma, "@threads needs three dimensions"))
            {
                return false;
            }
            if (!ParseUnsigned(program.threads[axis]))
            {
                return false;
            }
        }
        if (!Expect(TokenKind::RightParen, "expected ')' after thread dimensions"))
        {
            return false;
        }
        const u32 x = program.threads[0];
        const u32 y = program.threads[1];
        const u32 z = program.threads[2];
        const bool inRange = x >= 1 && y >= 1 && z >= 1 && x <= kMaximumThreadsXY &&
                             y <= kMaximumThreadsXY && z <= kMaximumThreadsZ &&
                             static_cast<u64>(x) * y * z <= kMaximumThreadsPerGroup;
        if (!inRange)
        {
            SetError(DiagnosticKind::Type, at,
                     "thread group exceeds cs_5_x limits (x, y <= 1024, z <= 64, total <= 1024)");
            return false;
        }
        program.hasThreads = true;
        return true;
    }

    // Items until the closing brace of the current block. The brace itself is
    // left for the caller. `active` is false inside a `when` branch the current
    // permutation does not select; such items are parsed and then discarded.
    [[nodiscard]] bool ParseBody(Program& program, bool active, usize whenDepth) noexcept
    {
        while (!Check(TokenKind::RightBrace) && !Check(TokenKind::End))
        {
            if (!ParseItem(program, active, whenDepth))
            {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] bool ParseItem(Program& program, bool active, usize whenDepth) noexcept
    {
        if (CheckIdentifier("keyword"))
        {
            if (whenDepth != 0)
            {
                SetError(DiagnosticKind::Syntax, Current(),
                         "keywords must be declared at shader scope");
                return false;
            }
            static_cast<void>(Advance());
            return ParseKeyword(program);
        }
        if (MatchIdentifier("when"))
        {
            return ParseWhen(program, active, whenDepth);
        }
        if (CheckIdentifier("output") && CheckNext(TokenKind::Dot))
        {
            return ParseAssignment(program, active);
        }
        if (CheckIdentifier("store") && CheckNext(TokenKind::LeftParen))
        {
            return ParseStore(program, active);
        }
        if (MatchIdentifier("input"))
        {
            return ParseTypedDeclaration(DeclarationKind::Input, program, active);
        }
        if (MatchIdentifier("output"))
        {
            return ParseTypedDeclaration(DeclarationKind::Output, program, active);
        }
        if (MatchIdentifier("uniform"))
        {
            return ParseTypedDeclaration(DeclarationKind::Uniform, program, active);
        }
        if (MatchIdentifier("texture2d"))
        {
            return ParseResourceDeclaration(DeclarationKind::Texture, program, active);
        }
        if (MatchIdentifier("sampler"))
        {
            return ParseResourceDeclaration(DeclarationKind::Sampler, program, active);
        }
        if (MatchIdentifier("rwtexture2d"))
        {
            return ParseResourceDeclaration(DeclarationKind::StorageTexture, program, active);
        }
        if (MatchIdentifier("return"))
        {
            return ParseReturn(program, active);
        }
        SetError(DiagnosticKind::Syntax, Current(),
                 "expected a declaration, keyword, when block, assignment, store, or return");
        return false;
    }

    [[nodiscard]] bool ParseKeyword(Program& program) noexcept
    {
        if (!Check(TokenKind::Identifier))
        {
            SetError(DiagnosticKind::Syntax, Current(), "expected keyword name");
            return false;
        }
        const Token name = Advance();
        if (!CheckName(name))
        {
            return false;
        }
        for (usize index = 0; index < program.keywordCount; ++index)
        {
            if (program.keywords[index].name == name.text)
            {
                SetError(DiagnosticKind::Type, name, "duplicate keyword");
                return false;
            }
        }
        if (program.keywordCount >= program.keywords.size())
        {
            SetError(DiagnosticKind::Unsupported, name, "keyword limit exceeded (8 per shader)");
            return false;
        }
        if (!Expect(TokenKind::Semicolon, "expected ';' after keyword"))
        {
            return false;
        }
        program.keywords[program.keywordCount++] = Keyword{name.text, IsEnabled(name.text)};
        return true;
    }

    // `when NAME { ... } else { ... }`. Both branches are always parsed.
    [[nodiscard]] bool ParseWhen(Program& program, bool active, usize whenDepth) noexcept
    {
        const Token& whenToken = m_tokens[m_position - 1];
        if (whenDepth + 1 > kMaximumWhenNesting)
        {
            SetError(DiagnosticKind::Unsupported, whenToken, "when nesting limit exceeded");
            return false;
        }
        if (!Check(TokenKind::Identifier))
        {
            SetError(DiagnosticKind::Syntax, Current(), "expected keyword after 'when'");
            return false;
        }
        const Token name = Advance();
        const Keyword* keyword = nullptr;
        for (usize index = 0; index < program.keywordCount; ++index)
        {
            if (program.keywords[index].name == name.text)
            {
                keyword = &program.keywords[index];
            }
        }
        if (keyword == nullptr)
        {
            SetError(DiagnosticKind::Type, name,
                     "unknown keyword; declare it with 'keyword' first");
            return false;
        }
        const bool selected = keyword->enabled;
        if (!Expect(TokenKind::LeftBrace, "expected '{' after when keyword") ||
            !ParseBody(program, active && selected, whenDepth + 1) ||
            !Expect(TokenKind::RightBrace, "expected '}' after when block"))
        {
            return false;
        }
        if (MatchIdentifier("else"))
        {
            if (!Expect(TokenKind::LeftBrace, "expected '{' after else") ||
                !ParseBody(program, active && !selected, whenDepth + 1) ||
                !Expect(TokenKind::RightBrace, "expected '}' after else block"))
            {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] bool ParseType(Type& type) noexcept
    {
        if (!Check(TokenKind::Identifier))
        {
            SetError(DiagnosticKind::Syntax, Current(), "expected a type name");
            return false;
        }

        const Token& token = Advance();
        if (token.text == "float")
        {
            type = Type::Float;
        }
        else if (token.text == "float2")
        {
            type = Type::Float2;
        }
        else if (token.text == "float3")
        {
            type = Type::Float3;
        }
        else if (token.text == "float4")
        {
            type = Type::Float4;
        }
        else if (token.text == "float4x4")
        {
            type = Type::Float4x4;
        }
        else if (token.text == "uint3")
        {
            type = Type::Uint3;
        }
        else
        {
            SetError(DiagnosticKind::Unsupported, token, "unsupported shader type");
            return false;
        }
        return true;
    }

    [[nodiscard]] bool ParseTypedDeclaration(DeclarationKind kind, Program& program,
                                             bool active) noexcept
    {
        Declaration declaration{};
        declaration.kind = kind;
        declaration.line = m_tokens[m_position - 1].line;
        declaration.column = m_tokens[m_position - 1].column;
        if (!ParseType(declaration.type) || !Check(TokenKind::Identifier))
        {
            if (m_error.message == nullptr)
            {
                SetError(DiagnosticKind::Syntax, Current(), "expected declaration name");
            }
            return false;
        }
        if (!CheckName(Current()))
        {
            return false;
        }
        declaration.name = Advance().text;
        if (!ParseAttribute(declaration) || !Expect(TokenKind::Semicolon, "expected ';'"))
        {
            return false;
        }
        return !active || AddDeclaration(program, declaration);
    }

    [[nodiscard]] bool ParseResourceDeclaration(DeclarationKind kind, Program& program,
                                                bool active) noexcept
    {
        Declaration declaration{};
        declaration.kind = kind;
        declaration.type =
            kind == DeclarationKind::Texture
                ? Type::Texture2D
                : (kind == DeclarationKind::Sampler ? Type::Sampler : Type::StorageTexture2D);
        declaration.line = m_tokens[m_position - 1].line;
        declaration.column = m_tokens[m_position - 1].column;
        if (!Check(TokenKind::Identifier))
        {
            SetError(DiagnosticKind::Syntax, Current(), "expected resource name");
            return false;
        }
        if (!CheckName(Current()))
        {
            return false;
        }
        declaration.name = Advance().text;
        if (!ParseAttribute(declaration) || !Expect(TokenKind::Semicolon, "expected ';'"))
        {
            return false;
        }
        return !active || AddDeclaration(program, declaration);
    }

    [[nodiscard]] bool ParseAttribute(Declaration& declaration) noexcept
    {
        if (!Expect(TokenKind::At, "expected an attribute"))
        {
            return false;
        }
        if (!Check(TokenKind::Identifier))
        {
            SetError(DiagnosticKind::Syntax, Current(), "expected attribute name");
            return false;
        }
        const Token attribute = Advance();
        if (!Expect(TokenKind::LeftParen, "expected '(' after attribute"))
        {
            return false;
        }

        if (attribute.text == "location")
        {
            if (!ParseUnsigned(declaration.location) ||
                !Expect(TokenKind::RightParen, "expected ')' after location"))
            {
                return false;
            }
            declaration.hasLocation = true;
            return true;
        }
        if (attribute.text == "builtin")
        {
            if (MatchIdentifier("position"))
            {
                declaration.builtin = Builtin::Position;
            }
            else if (MatchIdentifier("dispatch_thread_id"))
            {
                declaration.builtin = Builtin::DispatchThreadId;
            }
            else
            {
                SetError(DiagnosticKind::Syntax, Current(),
                         "expected builtin 'position' or 'dispatch_thread_id'");
                return false;
            }
            return Expect(TokenKind::RightParen, "expected ')' after builtin");
        }
        if (attribute.text == "register")
        {
            if (!Check(TokenKind::Identifier))
            {
                SetError(DiagnosticKind::Syntax, Current(), "expected register such as b0");
                return false;
            }
            const Token reg = Advance();
            if (reg.text.Size() < 2)
            {
                SetError(DiagnosticKind::Syntax, reg, "invalid register");
                return false;
            }
            declaration.registerClass = reg.text[0];
            if (!ParseDigits(reg.text.Substr(1), declaration.registerIndex))
            {
                SetError(DiagnosticKind::Syntax, reg, "invalid register index");
                return false;
            }
            if (Match(TokenKind::Comma))
            {
                if (!Check(TokenKind::Identifier) || Current().text.Size() <= 5 ||
                    !Current().text.StartsWith("space") ||
                    !ParseDigits(Current().text.Substr(5), declaration.registerSpace))
                {
                    SetError(DiagnosticKind::Syntax, Current(), "expected space index");
                    return false;
                }
                static_cast<void>(Advance());
            }
            declaration.hasRegister = true;
            return Expect(TokenKind::RightParen, "expected ')' after register");
        }

        SetError(DiagnosticKind::Unsupported, attribute, "unsupported shader attribute");
        return false;
    }

    [[nodiscard]] bool ParseUnsigned(u32& value) noexcept
    {
        if (!Check(TokenKind::Number))
        {
            SetError(DiagnosticKind::Syntax, Current(), "expected a non-negative integer");
            return false;
        }
        const Token token = Advance();
        if (!ParseDigits(token.text, value))
        {
            SetError(DiagnosticKind::Syntax, token, "integer is out of range");
            return false;
        }
        return true;
    }

    [[nodiscard]] bool AddDeclaration(Program& program, const Declaration& declaration) noexcept
    {
        if (program.declarationCount >= program.declarations.size())
        {
            SetError(DiagnosticKind::Unsupported, Current(), "declaration limit exceeded");
            return false;
        }
        program.declarations[program.declarationCount++] = declaration;
        return true;
    }

    // Records a statement whose expressions are already parsed. An inactive
    // statement releases the expression slots it used, so discarded branches do
    // not count against the expression limit.
    [[nodiscard]] bool CommitStatement(Program& program, const Statement& statement, bool active,
                                       usize expressionMark) noexcept
    {
        if (!active)
        {
            program.expressionCount = expressionMark;
            return true;
        }
        if (program.statementCount >= program.statements.size())
        {
            SetError(DiagnosticKind::Unsupported, Current(), "statement limit exceeded");
            return false;
        }
        program.statements[program.statementCount++] = statement;
        return true;
    }

    [[nodiscard]] bool ParseAssignment(Program& program, bool active) noexcept
    {
        const usize mark = program.expressionCount;
        const Token start = Current();
        u32 target = 0;
        if (!ParseExpression(program, target, 0) || !Expect(TokenKind::Equal, "expected '='"))
        {
            return false;
        }
        u32 value = 0;
        if (!ParseExpression(program, value, 0) ||
            !Expect(TokenKind::Semicolon, "expected ';' after assignment"))
        {
            return false;
        }
        return CommitStatement(
            program, Statement{StatementKind::Assignment, target, value, start.line, start.column},
            active, mark);
    }

    [[nodiscard]] bool ParseStore(Program& program, bool active) noexcept
    {
        const usize mark = program.expressionCount;
        const Token start = Current();
        u32 call = 0;
        if (!ParseExpression(program, call, 0) ||
            !Expect(TokenKind::Semicolon, "expected ';' after store"))
        {
            return false;
        }
        return CommitStatement(
            program, Statement{StatementKind::Store, call, call, start.line, start.column}, active,
            mark);
    }

    [[nodiscard]] bool ParseReturn(Program& program, bool active) noexcept
    {
        const usize mark = program.expressionCount;
        const Token start = m_tokens[m_position - 1];
        u32 value = 0;
        if (!ParseExpression(program, value, 0) ||
            !Expect(TokenKind::Semicolon, "expected ';' after return"))
        {
            return false;
        }
        return CommitStatement(program,
                               Statement{StatementKind::Return, 0, value, start.line, start.column},
                               active, mark);
    }

    [[nodiscard]] bool ParseExpression(Program& program, u32& expression, usize nesting) noexcept
    {
        if (nesting >= kMaximumNesting)
        {
            SetError(DiagnosticKind::Unsupported, Current(), "expression nesting limit exceeded");
            return false;
        }

        if (Check(TokenKind::Number))
        {
            const Token token = Advance();
            return AddExpression(
                program,
                Expression{ExpressionKind::Number, token.text, 0, {}, 0, token.line, token.column},
                expression);
        }
        if (!Check(TokenKind::Identifier))
        {
            SetError(DiagnosticKind::Syntax, Current(), "expected expression");
            return false;
        }

        const Token name = Advance();
        if (Match(TokenKind::LeftParen))
        {
            Expression call{};
            call.kind = ExpressionKind::Call;
            call.name = name.text;
            call.line = name.line;
            call.column = name.column;
            if (!Check(TokenKind::RightParen))
            {
                do
                {
                    if (call.argumentCount >= call.arguments.size() ||
                        !ParseExpression(program, call.arguments[call.argumentCount], nesting + 1))
                    {
                        if (m_error.message == nullptr)
                        {
                            SetError(DiagnosticKind::Unsupported, Current(),
                                     "function argument limit exceeded");
                        }
                        return false;
                    }
                    ++call.argumentCount;
                } while (Match(TokenKind::Comma));
            }
            if (!Expect(TokenKind::RightParen, "expected ')' after arguments"))
            {
                return false;
            }
            return AddExpression(program, call, expression);
        }

        if (Match(TokenKind::Dot))
        {
            if (!Check(TokenKind::Identifier))
            {
                SetError(DiagnosticKind::Syntax, Current(), "expected member name after '.'");
                return false;
            }
            const Token member = Advance();
            u32 base = 0;
            if (!AddExpression(
                    program,
                    Expression{
                        ExpressionKind::Identifier, name.text, 0, {}, 0, name.line, name.column},
                    base))
            {
                return false;
            }
            return AddExpression(
                program,
                Expression{
                    ExpressionKind::Member, member.text, base, {}, 0, member.line, member.column},
                expression);
        }

        return AddExpression(
            program,
            Expression{ExpressionKind::Identifier, name.text, 0, {}, 0, name.line, name.column},
            expression);
    }

    [[nodiscard]] bool AddExpression(Program& program, const Expression& expression,
                                     u32& index) noexcept
    {
        if (program.expressionCount >= program.expressions.size())
        {
            SetError(DiagnosticKind::Unsupported, Current(), "expression limit exceeded");
            return false;
        }
        index = static_cast<u32>(program.expressionCount);
        program.expressions[program.expressionCount++] = expression;
        return true;
    }

    const DynamicArray<Token>& m_tokens;
    StringView m_sourceName;
    Span<const StringView> m_enabled;
    usize m_position = 0;
    Diagnostic m_error{};
};

[[nodiscard]] const Declaration* FindDeclaration(const Program& program, DeclarationKind kind,
                                                 StringView name) noexcept
{
    for (usize index = 0; index < program.declarationCount; ++index)
    {
        const Declaration& declaration = program.declarations[index];
        if (declaration.kind == kind && declaration.name == name)
        {
            return &declaration;
        }
    }
    return nullptr;
}

[[nodiscard]] const Declaration* FindNameDeclaration(const Program& program,
                                                     StringView name) noexcept
{
    for (usize index = 0; index < program.declarationCount; ++index)
    {
        const Declaration& declaration = program.declarations[index];
        if ((declaration.kind == DeclarationKind::Uniform ||
             declaration.kind == DeclarationKind::Texture ||
             declaration.kind == DeclarationKind::Sampler ||
             declaration.kind == DeclarationKind::StorageTexture) &&
            declaration.name == name)
        {
            return &declaration;
        }
    }
    return nullptr;
}

[[nodiscard]] bool IsFloatValue(Type type) noexcept
{
    return type == Type::Float || type == Type::Float2 || type == Type::Float3 ||
           type == Type::Float4;
}

[[nodiscard]] Type ExpressionType(const Program& program, u32 index, StringView sourceName,
                                  Diagnostic& diagnostic) noexcept
{
    const Expression& expression = program.expressions[index];
    if (expression.kind == ExpressionKind::Number)
    {
        return Type::Float;
    }
    if (expression.kind == ExpressionKind::Identifier)
    {
        if (expression.name == "input" || expression.name == "output")
        {
            diagnostic =
                MakeDiagnostic(DiagnosticKind::Type, sourceName, expression.line, expression.column,
                               "use a declared member of input or output");
            return Type::Invalid;
        }
        const Declaration* declaration = FindNameDeclaration(program, expression.name);
        if (declaration == nullptr)
        {
            diagnostic = MakeDiagnostic(DiagnosticKind::Type, sourceName, expression.line,
                                        expression.column, "unknown shader name");
            return Type::Invalid;
        }
        return declaration->type;
    }
    if (expression.kind == ExpressionKind::Member)
    {
        const Expression& base = program.expressions[expression.left];
        if (base.kind != ExpressionKind::Identifier ||
            (base.name != "input" && base.name != "output"))
        {
            diagnostic = MakeDiagnostic(DiagnosticKind::Type, sourceName, expression.line,
                                        expression.column, "member access is limited to stage IO");
            return Type::Invalid;
        }
        const DeclarationKind kind =
            base.name == "input" ? DeclarationKind::Input : DeclarationKind::Output;
        const Declaration* declaration = FindDeclaration(program, kind, expression.name);
        if (declaration == nullptr || (base.name == "output" && program.stage != Stage::Vertex))
        {
            diagnostic = MakeDiagnostic(DiagnosticKind::Type, sourceName, expression.line,
                                        expression.column, "unknown stage IO member");
            return Type::Invalid;
        }
        return declaration->type;
    }

    std::array<Type, 4> arguments{};
    for (u32 argument = 0; argument < expression.argumentCount; ++argument)
    {
        arguments[argument] =
            ExpressionType(program, expression.arguments[argument], sourceName, diagnostic);
        if (arguments[argument] == Type::Invalid)
        {
            return Type::Invalid;
        }
    }
    if (expression.name == "float2" || expression.name == "float3" || expression.name == "float4")
    {
        const Type target = expression.name == "float2"
                                ? Type::Float2
                                : (expression.name == "float3" ? Type::Float3 : Type::Float4);
        u32 components = 0;
        for (u32 argument = 0; argument < expression.argumentCount; ++argument)
        {
            switch (arguments[argument])
            {
            case Type::Float:
                components += 1;
                break;
            case Type::Float2:
                components += 2;
                break;
            case Type::Float3:
                components += 3;
                break;
            case Type::Float4:
                components += 4;
                break;
            default:
                diagnostic =
                    MakeDiagnostic(DiagnosticKind::Type, sourceName, expression.line,
                                   expression.column, "invalid vector constructor argument");
                return Type::Invalid;
            }
        }
        const u32 expected = target == Type::Float2 ? 2U : (target == Type::Float3 ? 3U : 4U);
        if (components != expected)
        {
            diagnostic =
                MakeDiagnostic(DiagnosticKind::Type, sourceName, expression.line, expression.column,
                               "vector constructor component count mismatch");
            return Type::Invalid;
        }
        return target;
    }
    if (expression.name == "float")
    {
        if (expression.argumentCount == 1 && arguments[0] == Type::Float)
        {
            return Type::Float;
        }
    }
    else if (expression.name == "mul")
    {
        if (expression.argumentCount == 2 && arguments[0] == Type::Float4x4 &&
            arguments[1] == Type::Float4)
        {
            return Type::Float4;
        }
        // Component-wise product of two values of the same float type.
        if (expression.argumentCount == 2 && IsFloatValue(arguments[0]) &&
            arguments[0] == arguments[1])
        {
            return arguments[0];
        }
    }
    else if (expression.name == "sample")
    {
        if (expression.argumentCount == 3 && arguments[0] == Type::Texture2D &&
            arguments[1] == Type::Sampler && arguments[2] == Type::Float2)
        {
            return Type::Float4;
        }
    }

    diagnostic = MakeDiagnostic(DiagnosticKind::Type, sourceName, expression.line,
                                expression.column, "function or constructor has invalid arguments");
    return Type::Invalid;
}

[[nodiscard]] bool ValidateStore(const Program& program, const Statement& statement,
                                 StringView sourceName, Diagnostic& diagnostic) noexcept
{
    const Expression& call = program.expressions[statement.expression];
    if (program.stage != Stage::Compute || call.kind != ExpressionKind::Call ||
        call.name != "store")
    {
        diagnostic = MakeDiagnostic(DiagnosticKind::Type, sourceName, statement.line,
                                    statement.column, "store is only valid in a compute shader");
        return false;
    }
    if (call.argumentCount != 3)
    {
        diagnostic = MakeDiagnostic(DiagnosticKind::Type, sourceName, call.line, call.column,
                                    "store takes a storage texture, a uint3 id, and a float4");
        return false;
    }
    const Expression& target = program.expressions[call.arguments[0]];
    const Declaration* declaration =
        target.kind == ExpressionKind::Identifier
            ? FindDeclaration(program, DeclarationKind::StorageTexture, target.name)
            : nullptr;
    if (declaration == nullptr)
    {
        diagnostic = MakeDiagnostic(DiagnosticKind::Type, sourceName, target.line, target.column,
                                    "store target must be a declared rwtexture2d");
        return false;
    }
    if (ExpressionType(program, call.arguments[1], sourceName, diagnostic) != Type::Uint3)
    {
        if (diagnostic.message == nullptr)
        {
            diagnostic = MakeDiagnostic(DiagnosticKind::Type, sourceName, call.line, call.column,
                                        "store coordinate must be a uint3");
        }
        return false;
    }
    if (ExpressionType(program, call.arguments[2], sourceName, diagnostic) != Type::Float4)
    {
        if (diagnostic.message == nullptr)
        {
            diagnostic = MakeDiagnostic(DiagnosticKind::Type, sourceName, call.line, call.column,
                                        "store value must be a float4");
        }
        return false;
    }
    return true;
}

[[nodiscard]] bool ValidateDeclaration(const Program& program, const Declaration& declaration,
                                       StringView sourceName, Diagnostic& diagnostic) noexcept
{
    const auto fail = [&](const char* message) noexcept
    {
        diagnostic = MakeDiagnostic(DiagnosticKind::Type, sourceName, declaration.line,
                                    declaration.column, message);
        return false;
    };

    switch (declaration.kind)
    {
    case DeclarationKind::Input:
        if (declaration.hasRegister)
        {
            return fail("stage inputs do not take a register");
        }
        if (program.stage == Stage::Compute)
        {
            if (declaration.builtin != Builtin::DispatchThreadId ||
                declaration.type != Type::Uint3 || declaration.hasLocation)
            {
                return fail("compute inputs must be a uint3 @builtin(dispatch_thread_id)");
            }
            return true;
        }
        if (declaration.builtin == Builtin::Position)
        {
            if (program.stage != Stage::Pixel || declaration.type != Type::Float4 ||
                declaration.hasLocation)
            {
                return fail("only pixel inputs may read the float4 position builtin");
            }
            return true;
        }
        if (declaration.builtin != Builtin::None || !declaration.hasLocation ||
            !IsFloatValue(declaration.type))
        {
            return fail("stage inputs require a scalar/vector location");
        }
        return true;
    case DeclarationKind::Output:
        if (program.stage != Stage::Vertex || declaration.hasRegister ||
            !IsFloatValue(declaration.type) || declaration.builtin == Builtin::DispatchThreadId ||
            (declaration.builtin == Builtin::None && !declaration.hasLocation) ||
            (declaration.builtin == Builtin::Position &&
             (declaration.type != Type::Float4 || declaration.hasLocation)))
        {
            return fail("invalid vertex output declaration");
        }
        return true;
    case DeclarationKind::Uniform:
    case DeclarationKind::Texture:
    case DeclarationKind::Sampler:
    case DeclarationKind::StorageTexture:
        break;
    }

    char expectedClass = 'b';
    switch (declaration.kind)
    {
    case DeclarationKind::Texture:
        expectedClass = 't';
        break;
    case DeclarationKind::Sampler:
        expectedClass = 's';
        break;
    case DeclarationKind::StorageTexture:
        expectedClass = 'u';
        break;
    default:
        break;
    }
    if (!declaration.hasRegister || declaration.registerClass != expectedClass ||
        declaration.hasLocation || declaration.builtin != Builtin::None)
    {
        return fail("resource uses the wrong register class");
    }
    if (declaration.kind == DeclarationKind::Uniform &&
        !(IsFloatValue(declaration.type) || declaration.type == Type::Float4x4))
    {
        return fail("uniforms must be float, float2, float3, float4, or float4x4");
    }
    if (declaration.kind == DeclarationKind::StorageTexture && program.stage != Stage::Compute)
    {
        return fail("rwtexture2d is only available to compute shaders");
    }
    return true;
}

[[nodiscard]] bool ValidateProgram(const Program& program, StringView sourceName,
                                   Diagnostic& diagnostic) noexcept
{
    usize inputCount = 0;
    usize outputCount = 0;
    usize returnCount = 0;
    usize storeCount = 0;
    std::array<bool, kMaximumDeclarations> outputAssigned{};

    if (program.stage == Stage::Compute && !program.hasThreads)
    {
        diagnostic = MakeDiagnostic(DiagnosticKind::Type, sourceName, 1, 1,
                                    "compute shader requires @threads(x, y, z)");
        return false;
    }

    for (usize index = 0; index < program.declarationCount; ++index)
    {
        const Declaration& declaration = program.declarations[index];
        if (!ValidateDeclaration(program, declaration, sourceName, diagnostic))
        {
            return false;
        }
        inputCount += declaration.kind == DeclarationKind::Input ? 1U : 0U;
        outputCount += declaration.kind == DeclarationKind::Output ? 1U : 0U;

        for (usize prior = 0; prior < index; ++prior)
        {
            const Declaration& other = program.declarations[prior];
            const bool bothIo =
                (declaration.kind == DeclarationKind::Input ||
                 declaration.kind == DeclarationKind::Output) &&
                (other.kind == DeclarationKind::Input || other.kind == DeclarationKind::Output);
            if (declaration.name == other.name && (!bothIo || declaration.kind == other.kind))
            {
                diagnostic = MakeDiagnostic(DiagnosticKind::Type, sourceName, declaration.line,
                                            declaration.column, "duplicate shader resource name");
                return false;
            }
            if (declaration.kind == other.kind &&
                (declaration.kind == DeclarationKind::Input ||
                 declaration.kind == DeclarationKind::Output) &&
                ((declaration.hasLocation && other.hasLocation &&
                  declaration.location == other.location) ||
                 (declaration.builtin != Builtin::None && declaration.builtin == other.builtin)))
            {
                diagnostic = MakeDiagnostic(DiagnosticKind::Type, sourceName, declaration.line,
                                            declaration.column, "duplicate stage IO location");
                return false;
            }
            // Uniforms naming the same register are members of one constant
            // buffer; every other register may be used once.
            const bool sameConstantBuffer = declaration.kind == DeclarationKind::Uniform &&
                                            other.kind == DeclarationKind::Uniform;
            if (!sameConstantBuffer && declaration.hasRegister && other.hasRegister &&
                declaration.registerClass == other.registerClass &&
                declaration.registerIndex == other.registerIndex &&
                declaration.registerSpace == other.registerSpace)
            {
                diagnostic = MakeDiagnostic(DiagnosticKind::Type, sourceName, declaration.line,
                                            declaration.column, "duplicate resource register");
                return false;
            }
        }
    }

    if (inputCount == 0 || (program.stage == Stage::Vertex && outputCount == 0))
    {
        diagnostic = MakeDiagnostic(DiagnosticKind::Type, sourceName, 1, 1,
                                    "shader stage requires declared inputs and outputs");
        return false;
    }
    if (inputCount > kMaxStageVariables || outputCount > kMaxStageVariables)
    {
        diagnostic = MakeDiagnostic(DiagnosticKind::Unsupported, sourceName, 1, 1,
                                    "stage IO limit exceeded (16 inputs and 16 outputs)");
        return false;
    }

    for (usize statementIndex = 0; statementIndex < program.statementCount; ++statementIndex)
    {
        const Statement& statement = program.statements[statementIndex];
        if (statement.kind == StatementKind::Return)
        {
            ++returnCount;
            if (program.stage != Stage::Pixel || statementIndex + 1 != program.statementCount)
            {
                diagnostic = MakeDiagnostic(DiagnosticKind::Type, sourceName, statement.line,
                                            statement.column,
                                            "return is only valid at the end of a pixel shader");
                return false;
            }
            const Type result =
                ExpressionType(program, statement.expression, sourceName, diagnostic);
            if (result != Type::Float4)
            {
                if (diagnostic.message == nullptr)
                {
                    diagnostic =
                        MakeDiagnostic(DiagnosticKind::Type, sourceName, statement.line,
                                       statement.column, "pixel shader must return float4");
                }
                return false;
            }
        }
        else if (statement.kind == StatementKind::Store)
        {
            ++storeCount;
            if (!ValidateStore(program, statement, sourceName, diagnostic))
            {
                return false;
            }
        }
        else
        {
            const Expression& target = program.expressions[statement.target];
            if (target.kind != ExpressionKind::Member ||
                program.expressions[target.left].name != "output" || program.stage != Stage::Vertex)
            {
                diagnostic =
                    MakeDiagnostic(DiagnosticKind::Type, sourceName, statement.line,
                                   statement.column, "only vertex outputs may be assigned");
                return false;
            }
            const Declaration* declaration =
                FindDeclaration(program, DeclarationKind::Output, target.name);
            if (declaration == nullptr)
            {
                diagnostic = MakeDiagnostic(DiagnosticKind::Type, sourceName, target.line,
                                            target.column, "unknown vertex output");
                return false;
            }
            const Type value =
                ExpressionType(program, statement.expression, sourceName, diagnostic);
            if (value != declaration->type)
            {
                if (diagnostic.message == nullptr)
                {
                    diagnostic =
                        MakeDiagnostic(DiagnosticKind::Type, sourceName, statement.line,
                                       statement.column, "output assignment type mismatch");
                }
                return false;
            }
            const usize declarationIndex =
                static_cast<usize>(declaration - program.declarations.data());
            outputAssigned[declarationIndex] = true;
        }
    }

    if (program.stage == Stage::Pixel && returnCount != 1)
    {
        diagnostic = MakeDiagnostic(DiagnosticKind::Type, sourceName, 1, 1,
                                    "pixel shader requires one return");
        return false;
    }
    if (program.stage == Stage::Compute && storeCount == 0)
    {
        diagnostic = MakeDiagnostic(DiagnosticKind::Type, sourceName, 1, 1,
                                    "compute shader requires at least one store");
        return false;
    }
    if (program.stage == Stage::Vertex)
    {
        bool hasPosition = false;
        for (usize index = 0; index < program.declarationCount; ++index)
        {
            const Declaration& declaration = program.declarations[index];
            if (declaration.kind == DeclarationKind::Output)
            {
                if (!outputAssigned[index])
                {
                    diagnostic =
                        MakeDiagnostic(DiagnosticKind::Type, sourceName, declaration.line,
                                       declaration.column, "vertex output is not assigned");
                    return false;
                }
                hasPosition = hasPosition || declaration.builtin == Builtin::Position;
            }
        }
        if (!hasPosition)
        {
            diagnostic = MakeDiagnostic(DiagnosticKind::Type, sourceName, 1, 1,
                                        "vertex shader requires a position builtin output");
            return false;
        }
    }
    return true;
}

[[nodiscard]] ValueType ToValueType(Type type) noexcept
{
    switch (type)
    {
    case Type::Float2:
        return ValueType::Float2;
    case Type::Float3:
        return ValueType::Float3;
    case Type::Float4:
        return ValueType::Float4;
    case Type::Float4x4:
        return ValueType::Float4x4;
    case Type::Uint3:
        return ValueType::Uint3;
    default:
        return ValueType::Float;
    }
}

[[nodiscard]] ReflectedName CopyName(StringView text) noexcept
{
    ReflectedName name{};
    const usize size = text.Size() < kMaxNameLength ? text.Size() : kMaxNameLength;
    for (usize index = 0; index < size; ++index)
    {
        name.text[index] = text[index];
    }
    name.size = static_cast<u8>(size);
    return name;
}

// Byte offset of every uniform inside its constant buffer, following the HLSL
// packing rules: 4-byte alignment, no straddling of a 16-byte register, and a
// matrix starting a new register. Indexed by declaration.
struct ConstantLayout
{
    std::array<u32, kMaximumDeclarations> offsets{};
    // Index of the first uniform of the buffer this declaration belongs to.
    std::array<usize, kMaximumDeclarations> bufferOf{};
    // Bytes used so far by the buffer whose first uniform is at this index.
    std::array<u32, kMaximumDeclarations> bufferBytes{};
};

[[nodiscard]] u32 AlignUp(u32 value, u32 alignment) noexcept
{
    return (value + alignment - 1U) / alignment * alignment;
}

void ComputeConstantLayout(const Program& program, ConstantLayout& layout) noexcept
{
    for (usize index = 0; index < program.declarationCount; ++index)
    {
        const Declaration& declaration = program.declarations[index];
        if (declaration.kind != DeclarationKind::Uniform)
        {
            continue;
        }
        usize buffer = index;
        for (usize prior = 0; prior < index; ++prior)
        {
            const Declaration& other = program.declarations[prior];
            if (other.kind == DeclarationKind::Uniform &&
                other.registerIndex == declaration.registerIndex &&
                other.registerSpace == declaration.registerSpace)
            {
                buffer = layout.bufferOf[prior];
                break;
            }
        }
        layout.bufferOf[index] = buffer;
        const u32 size = ValueTypeSize(ToValueType(declaration.type));
        u32 cursor = layout.bufferBytes[buffer];
        if (declaration.type == Type::Float4x4 || (cursor % kRegisterBytes) + size > kRegisterBytes)
        {
            cursor = AlignUp(cursor, kRegisterBytes);
        }
        layout.offsets[index] = cursor;
        layout.bufferBytes[buffer] = cursor + size;
    }
}

[[nodiscard]] const char* TypeName(Type type) noexcept
{
    switch (type)
    {
    case Type::Float:
        return "float";
    case Type::Float2:
        return "float2";
    case Type::Float3:
        return "float3";
    case Type::Float4:
        return "float4";
    case Type::Float4x4:
        return "float4x4";
    case Type::Uint3:
        return "uint3";
    case Type::Texture2D:
        return "Texture2D<float4>";
    case Type::Sampler:
        return "SamplerState";
    case Type::StorageTexture2D:
        return "RWTexture2D<float4>";
    case Type::Invalid:
        break;
    }
    return "";
}

[[nodiscard]] bool Append(String& output, StringView text) noexcept
{
    return output.TryAppend(text);
}

[[nodiscard]] bool Append(String& output, const char* text) noexcept
{
    return output.TryAppend(StringView(text));
}

[[nodiscard]] bool Append(String& output, char value) noexcept
{
    return output.TryAppend(value);
}

[[nodiscard]] bool AppendNumber(String& output, u32 value) noexcept
{
    char buffer[10]{};
    usize size = 0;
    do
    {
        buffer[size++] = static_cast<char>('0' + value % 10U);
        value /= 10U;
    } while (value > 0);
    for (usize index = 0; index < size / 2; ++index)
    {
        const char temp = buffer[index];
        buffer[index] = buffer[size - index - 1];
        buffer[size - index - 1] = temp;
    }
    return output.TryAppend(StringView(buffer, size));
}

// Ordinal of the constant buffer whose first uniform is `first`.
[[nodiscard]] u32 ConstantBufferOrdinal(const Program& program, const ConstantLayout& layout,
                                        usize first) noexcept
{
    u32 ordinal = 0;
    for (usize index = 0; index < first; ++index)
    {
        if (program.declarations[index].kind == DeclarationKind::Uniform &&
            layout.bufferOf[index] == index)
        {
            ++ordinal;
        }
    }
    return ordinal;
}

[[nodiscard]] bool AppendConstantBufferName(const Program& program, u32 ordinal,
                                            String& output) noexcept
{
    return Append(output, program.name) && Append(output, "_Uniforms") &&
           AppendNumber(output, ordinal);
}

[[nodiscard]] bool EmitExpression(const Program& program, u32 index, String& output) noexcept
{
    const Expression& expression = program.expressions[index];
    switch (expression.kind)
    {
    case ExpressionKind::Number:
        return Append(output, expression.name);
    case ExpressionKind::Identifier:
        return Append(output, expression.name);
    case ExpressionKind::Member:
        return EmitExpression(program, expression.left, output) && Append(output, ".") &&
               Append(output, expression.name);
    case ExpressionKind::Call:
        if (expression.name == "sample")
        {
            return EmitExpression(program, expression.arguments[0], output) &&
                   Append(output, ".Sample(") &&
                   EmitExpression(program, expression.arguments[1], output) &&
                   Append(output, ", ") &&
                   EmitExpression(program, expression.arguments[2], output) && Append(output, ")");
        }
        if (expression.name == "mul" && expression.argumentCount == 2)
        {
            // A matrix product keeps HLSL's mul; a component-wise product is
            // emitted as an operator so its meaning does not depend on types.
            const Expression& lhs = program.expressions[expression.arguments[0]];
            const Declaration* matrix = lhs.kind == ExpressionKind::Identifier
                                            ? FindNameDeclaration(program, lhs.name)
                                            : nullptr;
            if (matrix == nullptr || matrix->type != Type::Float4x4)
            {
                return Append(output, "(") &&
                       EmitExpression(program, expression.arguments[0], output) &&
                       Append(output, " * ") &&
                       EmitExpression(program, expression.arguments[1], output) &&
                       Append(output, ")");
            }
        }
        if (!Append(output, expression.name) || !Append(output, "("))
        {
            return false;
        }
        for (u32 argument = 0; argument < expression.argumentCount; ++argument)
        {
            if (argument != 0 && !Append(output, ", "))
            {
                return false;
            }
            if (!EmitExpression(program, expression.arguments[argument], output))
            {
                return false;
            }
        }
        return Append(output, ")");
    }
    return false;
}

[[nodiscard]] bool EmitDeclarations(const Program& program, DeclarationKind kind,
                                    String& output) noexcept
{
    if (!Append(output,
                kind == DeclarationKind::Input ? "struct Input\n{\n" : "struct Output\n{\n"))
    {
        return false;
    }
    for (usize index = 0; index < program.declarationCount; ++index)
    {
        const Declaration& declaration = program.declarations[index];
        if (declaration.kind != kind)
        {
            continue;
        }
        if (!Append(output, "    ") || !Append(output, TypeName(declaration.type)) ||
            !Append(output, " ") || !Append(output, declaration.name) || !Append(output, " : "))
        {
            return false;
        }
        if (declaration.builtin == Builtin::Position)
        {
            if (!Append(output, "SV_Position"))
            {
                return false;
            }
        }
        else if (declaration.builtin == Builtin::DispatchThreadId)
        {
            if (!Append(output, "SV_DispatchThreadID"))
            {
                return false;
            }
        }
        else if (!Append(output, "TEXCOORD") || !AppendNumber(output, declaration.location))
        {
            return false;
        }
        if (!Append(output, ";\n"))
        {
            return false;
        }
    }
    return Append(output, "};\n");
}

[[nodiscard]] bool AppendRegister(String& output, char registerClass, u32 index, u32 space) noexcept
{
    if (!Append(output, " : register(") || !Append(output, registerClass) ||
        !AppendNumber(output, index))
    {
        return false;
    }
    if (space != 0 && (!Append(output, ", space") || !AppendNumber(output, space)))
    {
        return false;
    }
    return Append(output, ")");
}

[[nodiscard]] bool EmitConstantBuffer(const Program& program, const ConstantLayout& layout,
                                      usize first, String& output) noexcept
{
    const Declaration& head = program.declarations[first];
    if (!Append(output, "cbuffer ") ||
        !AppendConstantBufferName(program, ConstantBufferOrdinal(program, layout, first), output) ||
        !AppendRegister(output, 'b', head.registerIndex, head.registerSpace) ||
        !Append(output, "\n{\n"))
    {
        return false;
    }
    constexpr char kComponents[] = {'x', 'y', 'z', 'w'};
    for (usize index = first; index < program.declarationCount; ++index)
    {
        const Declaration& declaration = program.declarations[index];
        if (declaration.kind != DeclarationKind::Uniform || layout.bufferOf[index] != first)
        {
            continue;
        }
        const u32 offset = layout.offsets[index];
        const u32 component = (offset % kRegisterBytes) / 4U;
        if (!Append(output, "    ") || !Append(output, TypeName(declaration.type)) ||
            !Append(output, " ") || !Append(output, declaration.name) ||
            !Append(output, " : packoffset(c") || !AppendNumber(output, offset / kRegisterBytes))
        {
            return false;
        }
        if (component != 0 && (!Append(output, '.') || !Append(output, kComponents[component])))
        {
            return false;
        }
        if (!Append(output, ");\n"))
        {
            return false;
        }
    }
    return Append(output, "};\n");
}

[[nodiscard]] bool EmitBindings(const Program& program, const ConstantLayout& layout,
                                String& output) noexcept
{
    for (usize index = 0; index < program.declarationCount; ++index)
    {
        const Declaration& declaration = program.declarations[index];
        if (declaration.kind == DeclarationKind::Uniform)
        {
            if (layout.bufferOf[index] == index &&
                !EmitConstantBuffer(program, layout, index, output))
            {
                return false;
            }
        }
        else if (declaration.kind == DeclarationKind::Texture ||
                 declaration.kind == DeclarationKind::Sampler ||
                 declaration.kind == DeclarationKind::StorageTexture)
        {
            if (!Append(output, TypeName(declaration.type)) || !Append(output, " ") ||
                !Append(output, declaration.name) ||
                !AppendRegister(output, declaration.registerClass, declaration.registerIndex,
                                declaration.registerSpace) ||
                !Append(output, ";\n"))
            {
                return false;
            }
        }
    }
    return true;
}

[[nodiscard]] bool EmitHlsl(const Program& program, const ConstantLayout& layout,
                            String& output) noexcept
{
    if (program.stage == Stage::Vertex)
    {
        if (!EmitDeclarations(program, DeclarationKind::Input, output) ||
            !EmitDeclarations(program, DeclarationKind::Output, output) ||
            !EmitBindings(program, layout, output) ||
            !Append(output, "Output main(Input input)\n{\n    Output output;\n"))
        {
            return false;
        }
        for (usize index = 0; index < program.statementCount; ++index)
        {
            const Statement& statement = program.statements[index];
            if (!Append(output, "    ") || !EmitExpression(program, statement.target, output) ||
                !Append(output, " = ") || !EmitExpression(program, statement.expression, output) ||
                !Append(output, ";\n"))
            {
                return false;
            }
        }
        return Append(output, "    return output;\n}\n");
    }

    if (program.stage == Stage::Compute)
    {
        if (!EmitDeclarations(program, DeclarationKind::Input, output) ||
            !EmitBindings(program, layout, output) || !Append(output, "[numthreads(") ||
            !AppendNumber(output, program.threads[0]) || !Append(output, ", ") ||
            !AppendNumber(output, program.threads[1]) || !Append(output, ", ") ||
            !AppendNumber(output, program.threads[2]) ||
            !Append(output, ")]\nvoid main(Input input)\n{\n"))
        {
            return false;
        }
        for (usize index = 0; index < program.statementCount; ++index)
        {
            const Expression& call = program.expressions[program.statements[index].expression];
            if (!Append(output, "    ") || !EmitExpression(program, call.arguments[0], output) ||
                !Append(output, "[") || !EmitExpression(program, call.arguments[1], output) ||
                !Append(output, ".xy] = ") || !EmitExpression(program, call.arguments[2], output) ||
                !Append(output, ";\n"))
            {
                return false;
            }
        }
        return Append(output, "}\n");
    }

    if (!EmitDeclarations(program, DeclarationKind::Input, output) ||
        !EmitBindings(program, layout, output) ||
        !Append(output, "float4 main(Input input) : SV_Target\n{\n    return "))
    {
        return false;
    }
    for (usize index = 0; index < program.statementCount; ++index)
    {
        const Statement& statement = program.statements[index];
        if (statement.kind == StatementKind::Return)
        {
            return EmitExpression(program, statement.expression, output) &&
                   Append(output, ";\n}\n");
        }
    }
    return false;
}

void BuildReflection(const Program& program, const ConstantLayout& layout,
                     Reflection& reflection) noexcept
{
    reflection = Reflection{};
    reflection.stage = program.stage;
    if (program.stage == Stage::Compute)
    {
        reflection.threadGroupSize = program.threads;
    }
    for (usize index = 0; index < program.keywordCount; ++index)
    {
        reflection.keywords[reflection.keywordCount++] = CopyName(program.keywords[index].name);
    }

    for (usize index = 0; index < program.declarationCount; ++index)
    {
        const Declaration& declaration = program.declarations[index];
        switch (declaration.kind)
        {
        case DeclarationKind::Input:
        case DeclarationKind::Output:
        {
            StageVariable variable{};
            variable.name = CopyName(declaration.name);
            variable.type = ToValueType(declaration.type);
            variable.builtin = declaration.builtin;
            variable.location = declaration.hasLocation ? declaration.location : 0;
            if (declaration.kind == DeclarationKind::Input)
            {
                reflection.inputs[reflection.inputCount++] = variable;
            }
            else
            {
                reflection.outputs[reflection.outputCount++] = variable;
            }
            break;
        }
        case DeclarationKind::Uniform:
        {
            if (layout.bufferOf[index] != index)
            {
                break;
            }
            ResourceBinding binding{};
            // The generated cbuffer name always fits: shader names are capped at
            // 48 bytes and the suffix adds at most 9 + 2 digits.
            char nameBuffer[kMaxNameLength + 1]{};
            usize nameSize = 0;
            for (usize character = 0; character < program.name.Size(); ++character)
            {
                nameBuffer[nameSize++] = program.name[character];
            }
            constexpr char kSuffix[] = "_Uniforms";
            for (usize character = 0; character + 1 < sizeof(kSuffix); ++character)
            {
                nameBuffer[nameSize++] = kSuffix[character];
            }
            u32 ordinal = ConstantBufferOrdinal(program, layout, index);
            char digits[10]{};
            usize digitCount = 0;
            do
            {
                digits[digitCount++] = static_cast<char>('0' + ordinal % 10U);
                ordinal /= 10U;
            } while (ordinal > 0);
            while (digitCount > 0 && nameSize < kMaxNameLength)
            {
                nameBuffer[nameSize++] = digits[--digitCount];
            }
            binding.name = CopyName(StringView(nameBuffer, nameSize));
            binding.kind = ResourceKind::ConstantBuffer;
            binding.registerIndex = declaration.registerIndex;
            binding.registerSpace = declaration.registerSpace;
            binding.sizeBytes = AlignUp(layout.bufferBytes[index], kRegisterBytes);
            binding.firstMember = reflection.memberCount;
            for (usize member = index; member < program.declarationCount; ++member)
            {
                const Declaration& uniform = program.declarations[member];
                if (uniform.kind != DeclarationKind::Uniform || layout.bufferOf[member] != index ||
                    reflection.memberCount >= kMaxConstantMembers)
                {
                    continue;
                }
                ConstantMember& reflected = reflection.members[reflection.memberCount++];
                reflected.name = CopyName(uniform.name);
                reflected.type = ToValueType(uniform.type);
                reflected.offsetBytes = layout.offsets[member];
                reflected.sizeBytes = ValueTypeSize(reflected.type);
                ++binding.memberCount;
            }
            reflection.resources[reflection.resourceCount++] = binding;
            break;
        }
        case DeclarationKind::Texture:
        case DeclarationKind::Sampler:
        case DeclarationKind::StorageTexture:
        {
            ResourceBinding binding{};
            binding.name = CopyName(declaration.name);
            binding.kind =
                declaration.kind == DeclarationKind::Texture
                    ? ResourceKind::SampledTexture
                    : (declaration.kind == DeclarationKind::Sampler ? ResourceKind::Sampler
                                                                    : ResourceKind::StorageTexture);
            binding.registerIndex = declaration.registerIndex;
            binding.registerSpace = declaration.registerSpace;
            reflection.resources[reflection.resourceCount++] = binding;
            break;
        }
        }
    }
}

// Lex and parse into `program`. Shared by compilation and keyword scanning.
[[nodiscard]] bool Frontend(StringView sourceName, StringView source,
                            Span<const StringView> enabled, cyber::memory::Allocator& allocator,
                            Program& program, Diagnostic& diagnostic) noexcept
{
    if (source.Data() == nullptr || source.Empty())
    {
        diagnostic =
            MakeDiagnostic(DiagnosticKind::Syntax, sourceName, 1, 1, "shader source is empty");
        return false;
    }
    if (source.Size() > kMaximumSourceBytes)
    {
        diagnostic = MakeDiagnostic(DiagnosticKind::Unsupported, sourceName, 1, 1,
                                    "shader source size limit exceeded");
        return false;
    }
    DynamicArray<Token> tokens(allocator);
    if (!Lex(source, sourceName, tokens, diagnostic))
    {
        return false;
    }
    Parser parser(tokens, sourceName, enabled);
    return parser.Parse(program, diagnostic);
}

}  // namespace

const char* StageName(Stage stage) noexcept
{
    switch (stage)
    {
    case Stage::Vertex:
        return "vertex";
    case Stage::Pixel:
        return "pixel";
    case Stage::Compute:
        return "compute";
    }
    return "unknown";
}

u32 ValueTypeSize(ValueType type) noexcept
{
    switch (type)
    {
    case ValueType::Float:
        return 4;
    case ValueType::Float2:
        return 8;
    case ValueType::Float3:
    case ValueType::Uint3:
        return 12;
    case ValueType::Float4:
        return 16;
    case ValueType::Float4x4:
        return 64;
    }
    return 0;
}

const char* ValueTypeName(ValueType type) noexcept
{
    switch (type)
    {
    case ValueType::Float:
        return "float";
    case ValueType::Float2:
        return "float2";
    case ValueType::Float3:
        return "float3";
    case ValueType::Float4:
        return "float4";
    case ValueType::Float4x4:
        return "float4x4";
    case ValueType::Uint3:
        return "uint3";
    }
    return "unknown";
}

const char* ResourceKindName(ResourceKind kind) noexcept
{
    switch (kind)
    {
    case ResourceKind::ConstantBuffer:
        return "constant_buffer";
    case ResourceKind::SampledTexture:
        return "sampled_texture";
    case ResourceKind::Sampler:
        return "sampler";
    case ResourceKind::StorageTexture:
        return "storage_texture";
    }
    return "unknown";
}

Result<CompiledShader, Diagnostic> CompileHlsl(StringView sourceName, StringView source,
                                               cyber::memory::Allocator& allocator) noexcept
{
    return CompileHlsl(sourceName, source, CompileOptions{}, allocator);
}

Result<CompiledShader, Diagnostic> CompileHlsl(StringView sourceName, StringView source,
                                               const CompileOptions& options,
                                               cyber::memory::Allocator& allocator) noexcept
{
    if (options.keywords.Size() > kMaximumEnabledKeywords)
    {
        return Fail(DiagnosticKind::Permutation, sourceName, 1, 1,
                    "too many enabled keywords in one permutation");
    }
    ProgramStorage storage(allocator);
    Program* program = storage.Get();
    if (program == nullptr)
    {
        return Fail(DiagnosticKind::OutOfMemory, sourceName, 1, 1,
                    "unable to allocate the shader program");
    }

    Diagnostic diagnostic{};
    if (!Frontend(sourceName, source, options.keywords, allocator, *program, diagnostic))
    {
        return Result<CompiledShader, Diagnostic>::Err(diagnostic);
    }

    PermutationKey permutation{};
    for (usize index = 0; index < options.keywords.Size(); ++index)
    {
        bool declared = false;
        for (usize keyword = 0; keyword < program->keywordCount; ++keyword)
        {
            if (program->keywords[keyword].name == options.keywords[index])
            {
                declared = true;
                permutation.mask |= 1U << keyword;
            }
        }
        if (!declared)
        {
            return Fail(DiagnosticKind::Permutation, sourceName, 1, 1,
                        "enabled keyword is not declared by the shader");
        }
    }
    permutation.hash = HashKeywordSet(options.keywords);

    if (!ValidateProgram(*program, sourceName, diagnostic))
    {
        return Result<CompiledShader, Diagnostic>::Err(diagnostic);
    }

    ConstantLayout layout{};
    ComputeConstantLayout(*program, layout);

    CompiledShader compiled{program->stage, String(allocator), Reflection{}, permutation};
    if (!EmitHlsl(*program, layout, compiled.hlsl))
    {
        return Fail(DiagnosticKind::OutOfMemory, sourceName, 1, 1,
                    "unable to allocate generated HLSL");
    }
    BuildReflection(*program, layout, compiled.reflection);
    return Result<CompiledShader, Diagnostic>::Ok(std::move(compiled));
}

Result<KeywordList, Diagnostic> ScanKeywords(StringView sourceName, StringView source,
                                             cyber::memory::Allocator& allocator) noexcept
{
    ProgramStorage storage(allocator);
    Program* program = storage.Get();
    if (program == nullptr)
    {
        return Result<KeywordList, Diagnostic>::Err(
            MakeDiagnostic(DiagnosticKind::OutOfMemory, sourceName, 1, 1,
                           "unable to allocate the shader program"));
    }
    Diagnostic diagnostic{};
    if (!Frontend(sourceName, source, Span<const StringView>{}, allocator, *program, diagnostic))
    {
        return Result<KeywordList, Diagnostic>::Err(diagnostic);
    }
    KeywordList list{};
    list.stage = program->stage;
    for (usize index = 0; index < program->keywordCount; ++index)
    {
        list.names[list.count++] = CopyName(program->keywords[index].name);
    }
    return Result<KeywordList, Diagnostic>::Ok(list);
}

}  // namespace cyber::shaders

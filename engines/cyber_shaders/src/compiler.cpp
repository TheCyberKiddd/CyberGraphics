#include <cyber/memory/dynamic_array.h>
#include <cyber/shaders/compiler.h>

#include <array>
#include <limits>
#include <utility>

namespace cyber::shaders
{
namespace
{

using cyber::core::Result;
using cyber::core::usize;
using cyber::memory::DynamicArray;
using cyber::memory::String;
using cyber::memory::StringView;

constexpr usize kMaximumSourceBytes = 1024U * 1024U;
constexpr usize kMaximumTokens = 16384;
constexpr usize kMaximumDeclarations = 32;
constexpr usize kMaximumExpressions = 128;
constexpr usize kMaximumStatements = 128;
constexpr usize kMaximumNesting = 32;

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
    Texture2D,
    Sampler,
};

enum class DeclarationKind : u8
{
    Input,
    Output,
    Uniform,
    Texture,
    Sampler,
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
    bool isPosition = false;
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
    std::array<u32, 3> arguments{};
    u32 argumentCount = 0;
    u32 line = 1;
    u32 column = 1;
};

enum class StatementKind : u8
{
    Assignment,
    Return,
};

struct Statement
{
    StatementKind kind = StatementKind::Assignment;
    u32 target = 0;
    u32 expression = 0;
    u32 line = 1;
    u32 column = 1;
};

struct Program
{
    Stage stage = Stage::Vertex;
    StringView name{};
    std::array<Declaration, kMaximumDeclarations> declarations{};
    std::array<Expression, kMaximumExpressions> expressions{};
    std::array<Statement, kMaximumStatements> statements{};
    usize declarationCount = 0;
    usize expressionCount = 0;
    usize statementCount = 0;
};

[[nodiscard]] Result<CompiledShader, Diagnostic>
Fail(DiagnosticKind kind, StringView sourceName, u32 line, u32 column, const char* message) noexcept
{
    return Result<CompiledShader, Diagnostic>::Err(
        Diagnostic{kind, sourceName, line, column, message});
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
                    diagnostic = Diagnostic{DiagnosticKind::Lexical, sourceName, startLine,
                                            startColumn, "invalid numeric literal"};
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
                diagnostic = Diagnostic{DiagnosticKind::Lexical, sourceName, startLine, startColumn,
                                        "unsupported character"};
                return false;
            }
            ++offset;
            ++column;
        }

        token.text = source.Substr(start, offset - start);
        if (tokens.Size() >= kMaximumTokens)
        {
            diagnostic = Diagnostic{DiagnosticKind::Unsupported, sourceName, startLine, startColumn,
                                    "shader token limit exceeded"};
            return false;
        }
        if (!tokens.TryPushBack(token))
        {
            diagnostic = Diagnostic{DiagnosticKind::OutOfMemory, sourceName, startLine, startColumn,
                                    "unable to allocate compiler tokens"};
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
            Diagnostic{DiagnosticKind::OutOfMemory, sourceName, line, column, "out of memory"};
        return false;
    }
    return true;
}

class Parser
{
public:
    Parser(const DynamicArray<Token>& tokens, StringView sourceName) noexcept
        : m_tokens(tokens), m_sourceName(sourceName)
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
        else
        {
            SetError(DiagnosticKind::Syntax, Current(), "expected 'vertex' or 'pixel' stage");
            return SetDiagnostic(diagnostic);
        }

        if (!Check(TokenKind::Identifier))
        {
            SetError(DiagnosticKind::Syntax, Current(), "expected shader name");
            return SetDiagnostic(diagnostic);
        }
        program.name = Advance().text;
        if (!Expect(TokenKind::LeftBrace, "expected '{' after shader name"))
        {
            return SetDiagnostic(diagnostic);
        }

        while (!Check(TokenKind::RightBrace) && !Check(TokenKind::End))
        {
            if (CheckOutputAssignment())
            {
                if (!ParseAssignment(program))
                {
                    return SetDiagnostic(diagnostic);
                }
            }
            else if (MatchIdentifier("input"))
            {
                if (!ParseTypedDeclaration(DeclarationKind::Input, program))
                {
                    return SetDiagnostic(diagnostic);
                }
            }
            else if (MatchIdentifier("output"))
            {
                if (!ParseTypedDeclaration(DeclarationKind::Output, program))
                {
                    return SetDiagnostic(diagnostic);
                }
            }
            else if (MatchIdentifier("uniform"))
            {
                if (!ParseTypedDeclaration(DeclarationKind::Uniform, program))
                {
                    return SetDiagnostic(diagnostic);
                }
            }
            else if (MatchIdentifier("texture2d"))
            {
                if (!ParseResourceDeclaration(DeclarationKind::Texture, program))
                {
                    return SetDiagnostic(diagnostic);
                }
            }
            else if (MatchIdentifier("sampler"))
            {
                if (!ParseResourceDeclaration(DeclarationKind::Sampler, program))
                {
                    return SetDiagnostic(diagnostic);
                }
            }
            else if (MatchIdentifier("return"))
            {
                if (!ParseReturn(program))
                {
                    return SetDiagnostic(diagnostic);
                }
            }
            else
            {
                SetError(DiagnosticKind::Syntax, Current(),
                         "expected a declaration, output assignment, or return");
                return SetDiagnostic(diagnostic);
            }
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

    [[nodiscard]] bool MatchIdentifier(StringView text) noexcept
    {
        if (Check(TokenKind::Identifier) && Current().text == text)
        {
            static_cast<void>(Advance());
            return true;
        }
        return false;
    }

    [[nodiscard]] bool CheckOutputAssignment() const noexcept
    {
        return Check(TokenKind::Identifier) && Current().text == "output" &&
               m_position + 1 < m_tokens.Size() && m_tokens[m_position + 1].kind == TokenKind::Dot;
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

    void SetError(DiagnosticKind kind, const Token& token, const char* message) noexcept
    {
        if (m_error.message == nullptr)
        {
            m_error = Diagnostic{kind, m_sourceName, token.line, token.column, message};
        }
    }

    [[nodiscard]] bool SetDiagnostic(Diagnostic& diagnostic) const noexcept
    {
        diagnostic = m_error;
        return false;
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
        else
        {
            SetError(DiagnosticKind::Unsupported, token, "unsupported shader type");
            return false;
        }
        return true;
    }

    [[nodiscard]] bool ParseTypedDeclaration(DeclarationKind kind, Program& program) noexcept
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
        declaration.name = Advance().text;
        if (!ParseAttribute(declaration) || !Expect(TokenKind::Semicolon, "expected ';'"))
        {
            return false;
        }
        return AddDeclaration(program, declaration);
    }

    [[nodiscard]] bool ParseResourceDeclaration(DeclarationKind kind, Program& program) noexcept
    {
        Declaration declaration{};
        declaration.kind = kind;
        declaration.type = kind == DeclarationKind::Texture ? Type::Texture2D : Type::Sampler;
        declaration.line = m_tokens[m_position - 1].line;
        declaration.column = m_tokens[m_position - 1].column;
        if (!Check(TokenKind::Identifier))
        {
            SetError(DiagnosticKind::Syntax, Current(), "expected resource name");
            return false;
        }
        declaration.name = Advance().text;
        if (!ParseAttribute(declaration) || !Expect(TokenKind::Semicolon, "expected ';'"))
        {
            return false;
        }
        return AddDeclaration(program, declaration);
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
            if (!MatchIdentifier("position") ||
                !Expect(TokenKind::RightParen, "expected ')' after builtin"))
            {
                SetError(DiagnosticKind::Syntax, Current(),
                         "only the position builtin is supported");
                return false;
            }
            declaration.isPosition = true;
            return true;
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

    [[nodiscard]] bool Match(TokenKind kind) noexcept
    {
        if (Check(kind))
        {
            static_cast<void>(Advance());
            return true;
        }
        return false;
    }

    [[nodiscard]] bool ParseAssignment(Program& program) noexcept
    {
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
        if (program.statementCount >= program.statements.size())
        {
            SetError(DiagnosticKind::Unsupported, Current(), "statement limit exceeded");
            return false;
        }
        program.statements[program.statementCount++] =
            Statement{StatementKind::Assignment, target, value, start.line, start.column};
        return true;
    }

    [[nodiscard]] bool AddStatement(Program& program, Statement statement) noexcept
    {
        if (statement.kind == StatementKind::Return)
        {
            if (!ParseExpression(program, statement.expression, 0) ||
                !Expect(TokenKind::Semicolon, "expected ';' after return"))
            {
                return false;
            }
        }
        if (program.statementCount >= program.statements.size())
        {
            SetError(DiagnosticKind::Unsupported, Current(), "statement limit exceeded");
            return false;
        }
        program.statements[program.statementCount++] = statement;
        return true;
    }

    [[nodiscard]] bool ParseReturn(Program& program) noexcept
    {
        const Token start = m_tokens[m_position - 1];
        return AddStatement(program,
                            Statement{StatementKind::Return, 0, 0, start.line, start.column});
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
    usize m_position = 0;
    Diagnostic m_error{};
};

[[nodiscard]] bool IsSame(StringView lhs, StringView rhs) noexcept
{
    return lhs == rhs;
}

[[nodiscard]] const Declaration* FindDeclaration(const Program& program, DeclarationKind kind,
                                                 StringView name) noexcept
{
    for (usize index = 0; index < program.declarationCount; ++index)
    {
        const Declaration& declaration = program.declarations[index];
        if (declaration.kind == kind && IsSame(declaration.name, name))
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
             declaration.kind == DeclarationKind::Sampler) &&
            IsSame(declaration.name, name))
        {
            return &declaration;
        }
    }
    return nullptr;
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
            diagnostic = Diagnostic{DiagnosticKind::Type, sourceName, expression.line,
                                    expression.column, "use a declared member of input or output"};
            return Type::Invalid;
        }
        const Declaration* declaration = FindNameDeclaration(program, expression.name);
        if (declaration == nullptr)
        {
            diagnostic = Diagnostic{DiagnosticKind::Type, sourceName, expression.line,
                                    expression.column, "unknown shader name"};
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
            diagnostic = Diagnostic{DiagnosticKind::Type, sourceName, expression.line,
                                    expression.column, "member access is limited to stage IO"};
            return Type::Invalid;
        }
        const DeclarationKind kind =
            base.name == "input" ? DeclarationKind::Input : DeclarationKind::Output;
        const Declaration* declaration = FindDeclaration(program, kind, expression.name);
        if (declaration == nullptr || (base.name == "output" && program.stage != Stage::Vertex))
        {
            diagnostic = Diagnostic{DiagnosticKind::Type, sourceName, expression.line,
                                    expression.column, "unknown stage IO member"};
            return Type::Invalid;
        }
        return declaration->type;
    }

    std::array<Type, 3> arguments{};
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
                diagnostic = Diagnostic{DiagnosticKind::Type, sourceName, expression.line,
                                        expression.column, "invalid vector constructor argument"};
                return Type::Invalid;
            }
        }
        const u32 expected = target == Type::Float2 ? 2U : (target == Type::Float3 ? 3U : 4U);
        if (components != expected)
        {
            diagnostic =
                Diagnostic{DiagnosticKind::Type, sourceName, expression.line, expression.column,
                           "vector constructor component count mismatch"};
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
    }
    else if (expression.name == "sample")
    {
        if (expression.argumentCount == 3 && arguments[0] == Type::Texture2D &&
            arguments[1] == Type::Sampler && arguments[2] == Type::Float2)
        {
            return Type::Float4;
        }
    }

    diagnostic = Diagnostic{DiagnosticKind::Type, sourceName, expression.line, expression.column,
                            "function or constructor has invalid arguments"};
    return Type::Invalid;
}

[[nodiscard]] bool ValidateProgram(const Program& program, StringView sourceName,
                                   Diagnostic& diagnostic) noexcept
{
    usize inputCount = 0;
    usize outputCount = 0;
    usize returnCount = 0;
    std::array<bool, kMaximumDeclarations> outputAssigned{};

    for (usize index = 0; index < program.declarationCount; ++index)
    {
        const Declaration& declaration = program.declarations[index];
        if (declaration.kind == DeclarationKind::Input)
        {
            ++inputCount;
            const bool positionInput = program.stage == Stage::Pixel && declaration.isPosition &&
                                       declaration.type == Type::Float4;
            if ((!declaration.hasLocation && !positionInput) || declaration.hasRegister ||
                (declaration.isPosition && !positionInput) || declaration.type == Type::Float4x4)
            {
                diagnostic =
                    Diagnostic{DiagnosticKind::Type, sourceName, declaration.line,
                               declaration.column, "stage inputs require a scalar/vector location"};
                return false;
            }
        }
        else if (declaration.kind == DeclarationKind::Output)
        {
            ++outputCount;
            if (program.stage != Stage::Vertex ||
                (!declaration.hasLocation && !declaration.isPosition) || declaration.hasRegister ||
                (declaration.isPosition && declaration.type != Type::Float4))
            {
                diagnostic = Diagnostic{DiagnosticKind::Type, sourceName, declaration.line,
                                        declaration.column, "invalid vertex output declaration"};
                return false;
            }
        }
        else
        {
            const char expectedClass =
                declaration.kind == DeclarationKind::Uniform
                    ? 'b'
                    : (declaration.kind == DeclarationKind::Texture ? 't' : 's');
            if (!declaration.hasRegister || declaration.registerClass != expectedClass)
            {
                diagnostic =
                    Diagnostic{DiagnosticKind::Type, sourceName, declaration.line,
                               declaration.column, "resource uses the wrong register class"};
                return false;
            }
        }

        for (usize prior = 0; prior < index; ++prior)
        {
            const Declaration& other = program.declarations[prior];
            const bool bothIo =
                (declaration.kind == DeclarationKind::Input ||
                 declaration.kind == DeclarationKind::Output) &&
                (other.kind == DeclarationKind::Input || other.kind == DeclarationKind::Output);
            if (declaration.name == other.name && (!bothIo || declaration.kind == other.kind))
            {
                diagnostic = Diagnostic{DiagnosticKind::Type, sourceName, declaration.line,
                                        declaration.column, "duplicate shader resource name"};
                return false;
            }
            if (declaration.kind == other.kind &&
                (declaration.kind == DeclarationKind::Input ||
                 declaration.kind == DeclarationKind::Output) &&
                ((declaration.hasLocation && other.hasLocation &&
                  declaration.location == other.location) ||
                 (declaration.isPosition && other.isPosition)))
            {
                diagnostic = Diagnostic{DiagnosticKind::Type, sourceName, declaration.line,
                                        declaration.column, "duplicate stage IO location"};
                return false;
            }
            if (declaration.registerClass == other.registerClass &&
                declaration.registerIndex == other.registerIndex &&
                declaration.registerSpace == other.registerSpace && declaration.hasRegister &&
                other.hasRegister)
            {
                diagnostic = Diagnostic{DiagnosticKind::Type, sourceName, declaration.line,
                                        declaration.column, "duplicate resource register"};
                return false;
            }
        }
    }

    if (inputCount == 0 || (program.stage == Stage::Vertex && outputCount == 0))
    {
        diagnostic = Diagnostic{DiagnosticKind::Type, sourceName, 1, 1,
                                "shader stage requires declared inputs and outputs"};
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
                diagnostic =
                    Diagnostic{DiagnosticKind::Type, sourceName, statement.line, statement.column,
                               "return is only valid at the end of a pixel shader"};
                return false;
            }
            const Type result =
                ExpressionType(program, statement.expression, sourceName, diagnostic);
            if (result != Type::Float4)
            {
                if (diagnostic.message == nullptr)
                {
                    diagnostic = Diagnostic{DiagnosticKind::Type, sourceName, statement.line,
                                            statement.column, "pixel shader must return float4"};
                }
                return false;
            }
        }
        else
        {
            const Expression& target = program.expressions[statement.target];
            if (target.kind != ExpressionKind::Member ||
                program.expressions[target.left].name != "output" || program.stage != Stage::Vertex)
            {
                diagnostic = Diagnostic{DiagnosticKind::Type, sourceName, statement.line,
                                        statement.column, "only vertex outputs may be assigned"};
                return false;
            }
            const Declaration* declaration =
                FindDeclaration(program, DeclarationKind::Output, target.name);
            if (declaration == nullptr)
            {
                diagnostic = Diagnostic{DiagnosticKind::Type, sourceName, target.line,
                                        target.column, "unknown vertex output"};
                return false;
            }
            const Type value =
                ExpressionType(program, statement.expression, sourceName, diagnostic);
            if (value != declaration->type)
            {
                if (diagnostic.message == nullptr)
                {
                    diagnostic = Diagnostic{DiagnosticKind::Type, sourceName, statement.line,
                                            statement.column, "output assignment type mismatch"};
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
        diagnostic =
            Diagnostic{DiagnosticKind::Type, sourceName, 1, 1, "pixel shader requires one return"};
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
                    diagnostic = Diagnostic{DiagnosticKind::Type, sourceName, declaration.line,
                                            declaration.column, "vertex output is not assigned"};
                    return false;
                }
                hasPosition = hasPosition || declaration.isPosition;
            }
        }
        if (!hasPosition)
        {
            diagnostic = Diagnostic{DiagnosticKind::Type, sourceName, 1, 1,
                                    "vertex shader requires a position builtin output"};
            return false;
        }
    }
    return true;
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
    case Type::Texture2D:
        return "Texture2D<float4>";
    case Type::Sampler:
        return "SamplerState";
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
        if (declaration.isPosition)
        {
            if (!Append(output, "SV_Position"))
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

[[nodiscard]] bool EmitBindings(const Program& program, String& output) noexcept
{
    usize uniformIndex = 0;
    for (usize index = 0; index < program.declarationCount; ++index)
    {
        const Declaration& declaration = program.declarations[index];
        if (declaration.kind == DeclarationKind::Uniform)
        {
            if (!Append(output, "cbuffer ") || !Append(output, program.name) ||
                !Append(output, "_Uniforms") ||
                !AppendNumber(output, static_cast<u32>(uniformIndex)) ||
                !Append(output, " : register(b") ||
                !AppendNumber(output, declaration.registerIndex))
            {
                return false;
            }
            if (declaration.registerSpace != 0 &&
                (!Append(output, ", space") || !AppendNumber(output, declaration.registerSpace)))
            {
                return false;
            }
            if (!Append(output, ")\n{\n    ") || !Append(output, TypeName(declaration.type)) ||
                !Append(output, " ") || !Append(output, declaration.name) ||
                !Append(output, ";\n};\n"))
            {
                return false;
            }
            ++uniformIndex;
        }
        else if (declaration.kind == DeclarationKind::Texture ||
                 declaration.kind == DeclarationKind::Sampler)
        {
            if (!Append(output, TypeName(declaration.type)) || !Append(output, " ") ||
                !Append(output, declaration.name) || !Append(output, " : register(") ||
                !Append(output, declaration.registerClass) ||
                !AppendNumber(output, declaration.registerIndex))
            {
                return false;
            }
            if (declaration.registerSpace != 0 &&
                (!Append(output, ", space") || !AppendNumber(output, declaration.registerSpace)))
            {
                return false;
            }
            if (!Append(output, ");\n"))
            {
                return false;
            }
        }
    }
    return true;
}

[[nodiscard]] bool EmitHlsl(const Program& program, String& output) noexcept
{
    if (program.stage == Stage::Vertex)
    {
        if (!EmitDeclarations(program, DeclarationKind::Input, output) ||
            !EmitDeclarations(program, DeclarationKind::Output, output) ||
            !EmitBindings(program, output) ||
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

    if (!EmitDeclarations(program, DeclarationKind::Input, output) ||
        !EmitBindings(program, output) ||
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

}  // namespace

Result<CompiledShader, Diagnostic> CompileHlsl(StringView sourceName, StringView source,
                                               cyber::memory::Allocator& allocator) noexcept
{
    if (source.Data() == nullptr || source.Empty())
    {
        return Fail(DiagnosticKind::Syntax, sourceName, 1, 1, "shader source is empty");
    }
    if (source.Size() > kMaximumSourceBytes)
    {
        return Fail(DiagnosticKind::Unsupported, sourceName, 1, 1,
                    "shader source size limit exceeded");
    }

    DynamicArray<Token> tokens(allocator);
    Diagnostic diagnostic{};
    if (!Lex(source, sourceName, tokens, diagnostic))
    {
        return Result<CompiledShader, Diagnostic>::Err(diagnostic);
    }

    Program program{};
    Parser parser(tokens, sourceName);
    if (!parser.Parse(program, diagnostic))
    {
        return Result<CompiledShader, Diagnostic>::Err(diagnostic);
    }
    if (!ValidateProgram(program, sourceName, diagnostic))
    {
        return Result<CompiledShader, Diagnostic>::Err(diagnostic);
    }

    CompiledShader compiled{program.stage, String(allocator)};
    if (!EmitHlsl(program, compiled.hlsl))
    {
        return Fail(DiagnosticKind::OutOfMemory, sourceName, 1, 1,
                    "unable to allocate generated HLSL");
    }
    return Result<CompiledShader, Diagnostic>::Ok(std::move(compiled));
}

}  // namespace cyber::shaders

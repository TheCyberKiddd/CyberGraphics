#pragma once

#include <cyber/assets/asset.h>
#include <cyber/core/types.h>
#include <cyber/memory/allocator.h>
#include <cyber/memory/dynamic_array.h>
#include <cyber/memory/string_view.h>

#include <utility>

namespace cyber::assets
{

using cyber::core::f64;
using cyber::core::u32;
using cyber::core::u64;
using cyber::core::usize;
using cyber::memory::Allocator;
using cyber::memory::StringView;

namespace detail
{
class JsonParser;
}  // namespace detail

/// JSON value families. The document stores a flat node graph rather than a tree
/// of owning pointers, so a whole file is one allocation pair and no node owns
/// memory.
enum class JsonType : u8
{
    Null = 0,
    Bool,
    Number,
    String,
    Array,
    Object,
};

/// Parser limits. Both are hard stops, not suggestions: an input that exceeds
/// either is rejected instead of consuming unbounded memory or stack.
struct JsonParseOptions
{
    u32 maxDepth = 128;
    u32 maxNodes = 1'000'000;
};

class JsonDocument;

/// Non-owning handle to one JSON node. A handle is valid while its document is
/// alive; moving the document keeps handles valid because the node storage does
/// not move.
class JsonValue
{
public:
    JsonValue() noexcept = default;

    [[nodiscard]] bool IsValid() const noexcept
    {
        return m_document != nullptr;
    }

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return IsValid();
    }

    [[nodiscard]] JsonType Type() const noexcept;

    [[nodiscard]] bool IsNull() const noexcept
    {
        return Type() == JsonType::Null;
    }
    [[nodiscard]] bool IsBool() const noexcept
    {
        return Type() == JsonType::Bool;
    }
    [[nodiscard]] bool IsNumber() const noexcept
    {
        return Type() == JsonType::Number;
    }
    [[nodiscard]] bool IsString() const noexcept
    {
        return Type() == JsonType::String;
    }
    [[nodiscard]] bool IsArray() const noexcept
    {
        return Type() == JsonType::Array;
    }
    [[nodiscard]] bool IsObject() const noexcept
    {
        return Type() == JsonType::Object;
    }

    /// Returns the boolean value of a bool node, or false for other types.
    [[nodiscard]] bool AsBool() const noexcept;
    /// Returns the numeric value of a number node, or zero for other types.
    [[nodiscard]] f64 AsNumber() const noexcept;
    /// Returns the decoded text of a string node, or an empty view otherwise.
    [[nodiscard]] StringView AsString() const noexcept;
    /// True when the number node has no fraction and no exponent.
    [[nodiscard]] bool IsInteger() const noexcept;

    /// Element count for an array, member count for an object, zero otherwise.
    [[nodiscard]] usize Size() const noexcept;

    /// Array element by index. Returns an invalid handle when out of range.
    [[nodiscard]] JsonValue Element(usize index) const noexcept;

    /// Object member by name. Returns an invalid handle when absent.
    [[nodiscard]] JsonValue Member(StringView name) const noexcept;

    /// Member name of the object member at index, or an empty view.
    [[nodiscard]] StringView MemberName(usize index) const noexcept;

    /// Object member by index. Returns an invalid handle when out of range.
    [[nodiscard]] JsonValue MemberAt(usize index) const noexcept;

    [[nodiscard]] bool HasMember(StringView name) const noexcept;

    /// Convenience accessors that validate the type in one call.
    [[nodiscard]] bool TryGetBool(bool& value) const noexcept;
    [[nodiscard]] bool TryGetNumber(f64& value) const noexcept;
    [[nodiscard]] bool TryGetU32(u32& value) const noexcept;
    [[nodiscard]] bool TryGetString(StringView& value) const noexcept;

private:
    friend class JsonDocument;

    JsonValue(const JsonDocument* document, u32 index) noexcept
        : m_document(document), m_index(index)
    {
    }

    const JsonDocument* m_document = nullptr;
    u32 m_index = 0;
};

/// Parsed JSON document. One node array plus one text buffer own the whole file;
/// the allocator is borrowed and must outlive the document. Move construction
/// and assignment are supported, copy is not.
class JsonDocument
{
public:
    explicit JsonDocument(Allocator& allocator) noexcept;

    JsonDocument(const JsonDocument&) = delete;
    JsonDocument& operator=(const JsonDocument&) = delete;
    JsonDocument(JsonDocument&& other) noexcept;
    JsonDocument& operator=(JsonDocument&& other) noexcept;

    /// Parses `text`. The input may be empty only when it is also invalid; a
    /// document always contains exactly one root value.
    [[nodiscard]] static ImportResult<JsonDocument>
    Parse(StringView text, Allocator& allocator, const JsonParseOptions& options = {}) noexcept;

    [[nodiscard]] JsonValue Root() const noexcept;
    [[nodiscard]] usize NodeCount() const noexcept;
    [[nodiscard]] const Allocator* AllocatorRef() const noexcept
    {
        return m_allocator;
    }

private:
    friend class JsonValue;
    friend class detail::JsonParser;

    struct Node
    {
        JsonType type = JsonType::Null;
        bool boolean = false;
        bool isInteger = false;
        u32 first = 0;
        u32 next = 0;
        u32 count = 0;
        u32 keyOffset = 0;
        u32 keyLength = 0;
        u32 textOffset = 0;
        u32 textLength = 0;
        f64 number = 0.0;
    };

    [[nodiscard]] bool HasNode(u32 index) const noexcept
    {
        return index < m_nodes.Size();
    }

    [[nodiscard]] const Node& NodeAt(u32 index) const noexcept
    {
        return m_nodes[index];
    }

    [[nodiscard]] StringView TextView(u32 offset, u32 length) const noexcept;

    Allocator* m_allocator = nullptr;
    cyber::memory::DynamicArray<Node> m_nodes;
    cyber::memory::DynamicArray<char> m_text;
};

}  // namespace cyber::assets

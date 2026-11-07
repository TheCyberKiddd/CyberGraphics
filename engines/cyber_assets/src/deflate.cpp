#include "deflate.h"

#include <cstring>

namespace cyber::assets::detail
{
namespace
{

using cyber::core::ErrorCode;
using cyber::core::i32;
using cyber::core::u8;

constexpr u32 kMaxCodeBits = 15;
constexpr u32 kMaxSymbols = 288;
constexpr u32 kMaxDistances = 30;

constexpr u16 kLengthBase[29] = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                                 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
constexpr u8 kLengthExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
constexpr u16 kDistanceBase[30] = {1,    2,    3,    4,    5,    7,    9,    13,    17,    25,
                                   33,   49,   65,   97,   129,  193,  257,  385,   513,   769,
                                   1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
constexpr u8 kDistanceExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2,  3,  3,  4,  4,  5,  5,  6,
                                   6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
constexpr u8 kCodeLengthOrder[19] = {16, 17, 18, 0, 8,  7, 9,  6, 10, 5,
                                     11, 4,  12, 3, 13, 2, 14, 1, 15};

[[nodiscard]] constexpr u32 MakeCrcTableEntry(u32 index) noexcept
{
    u32 value = index;
    for (u32 bit = 0; bit < 8; ++bit)
    {
        value = (value & 1u) != 0 ? (0xedb88320u ^ (value >> 1u)) : (value >> 1u);
    }
    return value;
}

struct CrcTable
{
    u32 values[256] = {};

    constexpr CrcTable() noexcept
    {
        for (u32 index = 0; index < 256; ++index)
        {
            values[index] = MakeCrcTableEntry(index);
        }
    }
};

constexpr CrcTable kCrcTable{};

/// Big-endian, byte-oriented bit reader over a bounded span.
class BitReader
{
public:
    BitReader(Span<const Byte> input) noexcept : m_input(input) {}

    [[nodiscard]] bool Failed() const noexcept
    {
        return m_failed;
    }

    [[nodiscard]] usize BytePosition() const noexcept
    {
        return m_position;
    }

    [[nodiscard]] u32 Bits(u32 count) noexcept
    {
        while (m_bitCount < count)
        {
            if (m_position >= m_input.Size())
            {
                m_failed = true;
                return 0;
            }
            m_buffer |= static_cast<u32>(std::to_integer<u8>(m_input[m_position])) << m_bitCount;
            ++m_position;
            m_bitCount += 8;
        }
        const u32 value = count == 0 ? 0 : (m_buffer & ((1u << count) - 1u));
        m_buffer >>= count;
        m_bitCount -= count;
        return value;
    }

    /// Discards the remaining bits of the current byte.
    void AlignToByte() noexcept
    {
        const u32 discarded = m_bitCount % 8u;
        m_buffer >>= discarded;
        m_bitCount -= discarded;
    }

    [[nodiscard]] bool ReadAlignedBytes(Byte* destination, usize count) noexcept
    {
        AlignToByte();
        while (m_bitCount > 0 && count > 0)
        {
            *destination++ = static_cast<Byte>(m_buffer & 0xffu);
            m_buffer >>= 8u;
            m_bitCount -= 8u;
            --count;
        }
        const usize available = m_input.Size() - m_position;
        if (count > available)
        {
            m_failed = true;
            return false;
        }
        if (count > 0)
        {
            std::memcpy(destination, m_input.Data() + m_position, count);
            m_position += count;
        }
        return true;
    }

private:
    Span<const Byte> m_input;
    usize m_position = 0;
    u32 m_buffer = 0;
    u32 m_bitCount = 0;
    bool m_failed = false;
};

/// Canonical Huffman table in the "counts and symbols" form, which makes
/// over-subscribed and incomplete codes explicit instead of undefined.
struct Huffman
{
    u16 counts[kMaxCodeBits + 1] = {};
    u16 symbols[kMaxSymbols] = {};

    /// Returns 0 for a complete code, -1 for an over-subscribed one, and a
    /// positive value for an incomplete one.
    [[nodiscard]] i32 Build(const u8* lengths, u32 count) noexcept
    {
        for (u32 index = 0; index <= kMaxCodeBits; ++index)
        {
            counts[index] = 0;
        }
        for (u32 index = 0; index < count; ++index)
        {
            if (lengths[index] > kMaxCodeBits)
            {
                return -1;
            }
            ++counts[lengths[index]];
        }
        if (counts[0] == count)
        {
            return 0;  // No codes at all: legal but decoding any symbol fails.
        }

        i32 left = 1;
        for (u32 length = 1; length <= kMaxCodeBits; ++length)
        {
            left <<= 1;
            left -= counts[length];
            if (left < 0)
            {
                return -1;
            }
        }

        u16 offsets[kMaxCodeBits + 1] = {};
        for (u32 length = 1; length < kMaxCodeBits; ++length)
        {
            offsets[length + 1] = static_cast<u16>(offsets[length] + counts[length]);
        }
        for (u32 symbol = 0; symbol < count; ++symbol)
        {
            if (lengths[symbol] != 0)
            {
                symbols[offsets[lengths[symbol]]++] = static_cast<u16>(symbol);
            }
        }
        return left;
    }

    [[nodiscard]] i32 Decode(BitReader& reader) const noexcept
    {
        i32 code = 0;
        i32 first = 0;
        i32 index = 0;
        for (u32 length = 1; length <= kMaxCodeBits; ++length)
        {
            code |= static_cast<i32>(reader.Bits(1));
            if (reader.Failed())
            {
                return -1;
            }
            const i32 count = counts[length];
            if (code - first < count)
            {
                return symbols[index + (code - first)];
            }
            index += count;
            first += count;
            first <<= 1;
            code <<= 1;
        }
        return -1;
    }
};

[[nodiscard]] bool InflateBlock(BitReader& reader, DynamicArray<Byte>& output, usize maxOutput,
                                const Huffman& lengthTable, const Huffman& distanceTable,
                                ImportError& error) noexcept
{
    while (true)
    {
        const i32 symbol = lengthTable.Decode(reader);
        if (symbol < 0)
        {
            error = MakeError(ErrorCode::ParseError, "invalid DEFLATE length code");
            return false;
        }
        if (symbol < 256)
        {
            if (output.Size() >= maxOutput)
            {
                error = MakeError(ErrorCode::OutOfRange, "DEFLATE output exceeds the limit");
                return false;
            }
            if (!output.TryPushBack(static_cast<Byte>(symbol)))
            {
                error = MakeError(ErrorCode::OutOfRange, "out of memory while inflating");
                return false;
            }
            continue;
        }
        if (symbol == 256)
        {
            return true;
        }

        const u32 lengthIndex = static_cast<u32>(symbol - 257);
        if (lengthIndex >= 29)
        {
            error = MakeError(ErrorCode::ParseError, "invalid DEFLATE match length");
            return false;
        }
        const u32 length =
            static_cast<u32>(kLengthBase[lengthIndex]) + reader.Bits(kLengthExtra[lengthIndex]);

        const i32 distanceSymbol = distanceTable.Decode(reader);
        if (distanceSymbol < 0 || static_cast<u32>(distanceSymbol) >= kMaxDistances)
        {
            error = MakeError(ErrorCode::ParseError, "invalid DEFLATE distance code");
            return false;
        }
        const u32 distance = static_cast<u32>(kDistanceBase[distanceSymbol]) +
                             reader.Bits(kDistanceExtra[distanceSymbol]);
        if (reader.Failed())
        {
            error = MakeError(ErrorCode::ParseError, "truncated DEFLATE stream");
            return false;
        }
        if (distance == 0 || distance > output.Size())
        {
            error = MakeError(ErrorCode::ParseError, "DEFLATE distance is out of range");
            return false;
        }
        if (length > maxOutput - output.Size())
        {
            error = MakeError(ErrorCode::OutOfRange, "DEFLATE output exceeds the limit");
            return false;
        }

        usize source = output.Size() - distance;
        for (u32 copied = 0; copied < length; ++copied)
        {
            const Byte value = output[source + copied];
            if (!output.TryPushBack(value))
            {
                error = MakeError(ErrorCode::OutOfRange, "out of memory while inflating");
                return false;
            }
        }
    }
}

}  // namespace

u32 Crc32(u32 seed, const Byte* data, usize size) noexcept
{
    u32 value = ~seed;
    for (usize index = 0; index < size; ++index)
    {
        value =
            kCrcTable.values[(value ^ std::to_integer<u8>(data[index])) & 0xffu] ^ (value >> 8u);
    }
    return ~value;
}

ImportStatus InflateZlib(Span<const Byte> input, DynamicArray<Byte>& output, usize maxOutput,
                         ImportError& error) noexcept
{
    if (input.Size() < 6)
    {
        error = MakeError(ErrorCode::ParseError, "zlib stream is too short");
        return ImportStatus::Err(error);
    }

    const u32 cmf = std::to_integer<u8>(input[0]);
    const u32 flg = std::to_integer<u8>(input[1]);
    if ((cmf & 0x0fu) != 8u)
    {
        error = MakeError(ErrorCode::Unsupported, "zlib stream does not use DEFLATE");
        return ImportStatus::Err(error);
    }
    if (((cmf << 8u) | flg) % 31u != 0u)
    {
        error = MakeError(ErrorCode::ParseError, "zlib header check bits are invalid");
        return ImportStatus::Err(error);
    }
    if ((flg & 0x20u) != 0u)
    {
        error = MakeError(ErrorCode::Unsupported, "zlib preset dictionaries are not supported");
        return ImportStatus::Err(error);
    }

    BitReader reader(input.Subspan(2));
    Huffman fixedLength;
    Huffman fixedDistance;
    bool fixedReady = false;

    while (true)
    {
        const u32 finalBlock = reader.Bits(1);
        const u32 blockType = reader.Bits(2);
        if (reader.Failed())
        {
            error = MakeError(ErrorCode::ParseError, "truncated DEFLATE block header");
            return ImportStatus::Err(error);
        }

        if (blockType == 0)
        {
            reader.AlignToByte();
            Byte header[4] = {};
            if (!reader.ReadAlignedBytes(header, 4))
            {
                error = MakeError(ErrorCode::ParseError, "truncated stored block header");
                return ImportStatus::Err(error);
            }
            const u32 length = static_cast<u32>(std::to_integer<u8>(header[0])) |
                               (static_cast<u32>(std::to_integer<u8>(header[1])) << 8u);
            const u32 inverse = static_cast<u32>(std::to_integer<u8>(header[2])) |
                                (static_cast<u32>(std::to_integer<u8>(header[3])) << 8u);
            if ((length ^ 0xffffu) != inverse)
            {
                error = MakeError(ErrorCode::ParseError, "stored block length is inconsistent");
                return ImportStatus::Err(error);
            }
            if (length > maxOutput - output.Size())
            {
                error = MakeError(ErrorCode::OutOfRange, "DEFLATE output exceeds the limit");
                return ImportStatus::Err(error);
            }
            const usize previous = output.Size();
            if (!output.TryResize(previous + length))
            {
                error = MakeError(ErrorCode::OutOfRange, "out of memory while inflating");
                return ImportStatus::Err(error);
            }
            if (!reader.ReadAlignedBytes(output.Data() + previous, length))
            {
                error = MakeError(ErrorCode::ParseError, "truncated stored block");
                return ImportStatus::Err(error);
            }
        }
        else if (blockType == 1 || blockType == 2)
        {
            Huffman lengthTable;
            Huffman distanceTable;
            if (blockType == 1)
            {
                if (!fixedReady)
                {
                    u8 lengths[kMaxSymbols] = {};
                    for (u32 symbol = 0; symbol < 144; ++symbol)
                    {
                        lengths[symbol] = 8;
                    }
                    for (u32 symbol = 144; symbol < 256; ++symbol)
                    {
                        lengths[symbol] = 9;
                    }
                    for (u32 symbol = 256; symbol < 280; ++symbol)
                    {
                        lengths[symbol] = 7;
                    }
                    for (u32 symbol = 280; symbol < 288; ++symbol)
                    {
                        lengths[symbol] = 8;
                    }
                    static_cast<void>(fixedLength.Build(lengths, kMaxSymbols));
                    u8 distanceLengths[kMaxDistances] = {};
                    for (u32 symbol = 0; symbol < kMaxDistances; ++symbol)
                    {
                        distanceLengths[symbol] = 5;
                    }
                    static_cast<void>(fixedDistance.Build(distanceLengths, kMaxDistances));
                    fixedReady = true;
                }
                lengthTable = fixedLength;
                distanceTable = fixedDistance;
            }
            else
            {
                const u32 literalCount = reader.Bits(5) + 257u;
                const u32 distanceCount = reader.Bits(5) + 1u;
                const u32 codeLengthCount = reader.Bits(4) + 4u;
                if (reader.Failed() || literalCount > 286u || distanceCount > 30u)
                {
                    error = MakeError(ErrorCode::ParseError, "dynamic Huffman header is invalid");
                    return ImportStatus::Err(error);
                }

                u8 codeLengthLengths[19] = {};
                for (u32 index = 0; index < codeLengthCount; ++index)
                {
                    codeLengthLengths[kCodeLengthOrder[index]] = static_cast<u8>(reader.Bits(3));
                }
                Huffman codeLengthTable;
                static_cast<void>(codeLengthTable.Build(codeLengthLengths, 19));

                u8 lengths[kMaxSymbols + kMaxDistances] = {};
                const u32 total = literalCount + distanceCount;
                u32 index = 0;
                while (index < total)
                {
                    const i32 symbol = codeLengthTable.Decode(reader);
                    if (symbol < 0)
                    {
                        error = MakeError(ErrorCode::ParseError,
                                          "dynamic Huffman code lengths are invalid");
                        return ImportStatus::Err(error);
                    }
                    if (symbol < 16)
                    {
                        lengths[index++] = static_cast<u8>(symbol);
                        continue;
                    }
                    u32 repeat = 0;
                    u8 value = 0;
                    if (symbol == 16)
                    {
                        if (index == 0)
                        {
                            error = MakeError(ErrorCode::ParseError,
                                              "code length repeat has no previous value");
                            return ImportStatus::Err(error);
                        }
                        value = lengths[index - 1];
                        repeat = 3u + reader.Bits(2);
                    }
                    else if (symbol == 17)
                    {
                        repeat = 3u + reader.Bits(3);
                    }
                    else
                    {
                        repeat = 11u + reader.Bits(7);
                    }
                    if (reader.Failed() || repeat > total - index)
                    {
                        error = MakeError(ErrorCode::ParseError, "code length repeat overruns");
                        return ImportStatus::Err(error);
                    }
                    for (u32 repeated = 0; repeated < repeat; ++repeated)
                    {
                        lengths[index++] = value;
                    }
                }

                const i32 literalCode = lengthTable.Build(lengths, literalCount);
                const i32 distanceCode = distanceTable.Build(lengths + literalCount, distanceCount);
                // DEFLATE permits an incomplete distance table when it holds a single
                // one-bit code; every other incomplete code is invalid.
                const bool distanceAcceptable =
                    distanceCode == 0 || (distanceCode > 0 && distanceTable.counts[1] == 1);
                if (literalCode != 0 || !distanceAcceptable)
                {
                    error = MakeError(ErrorCode::ParseError, "dynamic Huffman table is invalid");
                    return ImportStatus::Err(error);
                }
                if (lengths[256] == 0)
                {
                    error = MakeError(ErrorCode::ParseError, "dynamic table has no end marker");
                    return ImportStatus::Err(error);
                }
            }

            if (!InflateBlock(reader, output, maxOutput, lengthTable, distanceTable, error))
            {
                return ImportStatus::Err(error);
            }
        }
        else
        {
            error = MakeError(ErrorCode::ParseError, "reserved DEFLATE block type");
            return ImportStatus::Err(error);
        }

        if (reader.Failed())
        {
            error = MakeError(ErrorCode::ParseError, "truncated DEFLATE stream");
            return ImportStatus::Err(error);
        }
        if (finalBlock != 0)
        {
            break;
        }
    }

    // Adler-32 trailer, big-endian, byte aligned after the final block.
    reader.AlignToByte();
    Byte trailer[4] = {};
    if (!reader.ReadAlignedBytes(trailer, 4))
    {
        error = MakeError(ErrorCode::ParseError, "zlib stream has no Adler-32 checksum");
        return ImportStatus::Err(error);
    }
    const u32 expected = ReadBigEndian32(trailer);
    const u32 modulus = 65521u;
    // Canonical two-accumulator Adler-32.
    u32 a = 1;
    u32 b = 0;
    for (usize index = 0; index < output.Size(); ++index)
    {
        a = (a + std::to_integer<u8>(output[index])) % modulus;
        b = (b + a) % modulus;
    }
    const u32 checksum = (b << 16u) | a;
    if (checksum != expected)
    {
        error = MakeError(ErrorCode::ParseError, "zlib Adler-32 checksum mismatch");
        return ImportStatus::Err(error);
    }
    return ImportStatus::Ok();
}

}  // namespace cyber::assets::detail

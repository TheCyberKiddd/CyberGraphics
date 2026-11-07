#include "deflate.h"

#include <cyber/assets/image.h>

#include <cmath>
#include <cstring>

namespace cyber::assets
{
namespace
{

using cyber::assets::detail::ReadBigEndian16;
using cyber::core::ErrorCode;
using cyber::core::i32;
using cyber::core::u8;
using cyber::core::usize;

constexpr u8 kMarkerSoi = 0xd8;
constexpr u8 kMarkerEoi = 0xd9;
constexpr u8 kMarkerSos = 0xda;
constexpr u8 kMarkerDqt = 0xdb;
constexpr u8 kMarkerDht = 0xc4;
constexpr u8 kMarkerDri = 0xdd;
constexpr u8 kMarkerApp14 = 0xee;
constexpr u8 kMarkerSof0 = 0xc0;
constexpr u8 kMarkerSof1 = 0xc1;
constexpr u8 kMarkerSof2 = 0xc2;
constexpr u8 kMarkerSof9 = 0xc9;
constexpr u8 kMarkerSof10 = 0xca;

constexpr u8 kZigZag[64] = {0,  1,  8,  16, 9,  2,  3,  10, 17, 24, 32, 25, 18, 11, 4,  5,
                            12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6,  7,  14, 21, 28,
                            35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51,
                            58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63};

constexpr u32 kMaxDimension = 32768;
constexpr usize kMaxPixels = 64u * 1024u * 1024u;

[[nodiscard]] bool InRange(usize size, usize offset, usize length) noexcept
{
    return offset <= size && length <= size - offset;
}

/// Canonical JPEG Huffman table (ITU T.81 Annex F). `maxCode[17]` is -1 for
/// lengths with no codes, which makes decoding fail cleanly instead of reading a
/// bogus symbol.
struct HuffmanTable
{
    u8 values[256] = {};
    i32 minCode[17] = {};
    i32 maxCode[18] = {};
    i32 valueIndex[17] = {};

    [[nodiscard]] bool Build(const u8* counts, const u8* symbols, u32 symbolCount) noexcept
    {
        i32 code = 0;
        i32 index = 0;
        for (u32 length = 1; length <= 16; ++length)
        {
            const i32 count = counts[length];
            if (count > 0)
            {
                minCode[length] = code;
                valueIndex[length] = index;
                code += count;
                index += count;
                maxCode[length] = code - 1;
            }
            else
            {
                maxCode[length] = -1;
            }
            code <<= 1;
        }
        maxCode[17] = 0x7fffffff;
        if (static_cast<u32>(index) > symbolCount)
        {
            return false;
        }
        for (i32 symbol = 0; symbol < index; ++symbol)
        {
            values[symbol] = symbols[symbol];
        }
        return true;
    }
};

/// Entropy-coded segment reader. Handles 0xFF00 byte stuffing and stops at a
/// marker so a restart interval can be honored.
class EntropyReader
{
public:
    EntropyReader(const Byte* data, usize size, usize offset) noexcept
        : m_data(data), m_size(size), m_position(offset)
    {
    }

    [[nodiscard]] bool Failed() const noexcept
    {
        return m_failed;
    }

    [[nodiscard]] usize Position() const noexcept
    {
        return m_position;
    }

    [[nodiscard]] u8 Marker() const noexcept
    {
        return m_marker;
    }

    [[nodiscard]] bool MarkerPending() const noexcept
    {
        return m_markerPending;
    }

    [[nodiscard]] bool NextBit(u32& bit) noexcept
    {
        if (m_bitCount == 0)
        {
            if (!Refill())
            {
                return false;
            }
        }
        bit = (m_buffer >> 7u) & 1u;
        m_buffer = static_cast<u8>(m_buffer << 1u);
        --m_bitCount;
        return true;
    }

    [[nodiscard]] bool Bits(u32 count, u32& value) noexcept
    {
        value = 0;
        for (u32 index = 0; index < count; ++index)
        {
            u32 bit = 0;
            if (!NextBit(bit))
            {
                return false;
            }
            value = (value << 1u) | bit;
        }
        return true;
    }

    void AlignToByte() noexcept
    {
        m_buffer = 0;
        m_bitCount = 0;
    }

    /// Discards the partial byte and consumes one RSTn marker.
    [[nodiscard]] bool ConsumeRestart() noexcept
    {
        AlignToByte();
        if (!m_markerPending)
        {
            // The marker is one byte ahead; pull it in.
            if (m_position >= m_size)
            {
                return false;
            }
            if (std::to_integer<u8>(m_data[m_position]) != 0xffu)
            {
                return false;
            }
            ++m_position;
            while (m_position < m_size && std::to_integer<u8>(m_data[m_position]) == 0xffu)
            {
                ++m_position;
            }
            if (m_position >= m_size)
            {
                return false;
            }
            m_marker = std::to_integer<u8>(m_data[m_position]);
            ++m_position;
            m_markerPending = true;
        }
        if (m_marker < 0xd0u || m_marker > 0xd7u)
        {
            return false;
        }
        m_markerPending = false;
        return true;
    }

    [[nodiscard]] i32 DecodeSymbol(const HuffmanTable& table) noexcept
    {
        i32 code = 0;
        for (u32 length = 1; length <= 16; ++length)
        {
            u32 bit = 0;
            if (!NextBit(bit))
            {
                return -1;
            }
            code = (code << 1) | static_cast<i32>(bit);
            if (table.maxCode[length] >= 0 && code <= table.maxCode[length])
            {
                const i32 index = table.valueIndex[length] + code - table.minCode[length];
                if (index < 0 || index >= 256)
                {
                    return -1;
                }
                return table.values[index];
            }
        }
        return -1;
    }

private:
    [[nodiscard]] bool Refill() noexcept
    {
        if (m_markerPending || m_position >= m_size)
        {
            m_failed = m_failed || m_position >= m_size;
            return false;
        }
        u8 value = std::to_integer<u8>(m_data[m_position]);
        ++m_position;
        if (value == 0xffu)
        {
            if (m_position >= m_size)
            {
                m_failed = true;
                return false;
            }
            const u8 next = std::to_integer<u8>(m_data[m_position]);
            ++m_position;
            if (next == 0x00u)
            {
                value = 0xffu;
            }
            else
            {
                m_marker = next;
                m_markerPending = true;
                return false;
            }
        }
        m_buffer = value;
        m_bitCount = 8;
        return true;
    }

    const Byte* m_data = nullptr;
    usize m_size = 0;
    usize m_position = 0;
    u8 m_buffer = 0;
    u32 m_bitCount = 0;
    u8 m_marker = 0;
    bool m_markerPending = false;
    bool m_failed = false;
};

struct Component
{
    u8 id = 0;
    u8 horizontal = 1;
    u8 vertical = 1;
    u8 quantTable = 0;
    u8 dcTable = 0;
    u8 acTable = 0;
    u32 stride = 0;
    u32 height = 0;
    i32 dcPrediction = 0;
    Byte* plane = nullptr;
};

[[nodiscard]] i32 Extend(i32 value, u32 bits) noexcept
{
    if (bits == 0)
    {
        return 0;
    }
    if (value < (1 << (bits - 1)))
    {
        value += (-(1 << static_cast<i32>(bits))) + 1;
    }
    return value;
}

constexpr f32 kPi = 3.14159265358979323846f;

/// Builds the eight cosine basis rows once per image: cosine[x][u] is
/// cos((2x + 1) * u * pi / 16).
void BuildCosineTable(f32 table[8][8]) noexcept
{
    for (u32 x = 0; x < 8; ++x)
    {
        for (u32 u = 0; u < 8; ++u)
        {
            table[x][u] =
                static_cast<f32>(std::cos(static_cast<f64>((2 * x + 1) * u) * kPi / 16.0));
        }
    }
}

/// Separable inverse DCT. The 1/4 scale factor and the DC bias are folded into
/// the two passes. Coefficients are in natural (raster) order.
void InverseDct(const i32* coefficients, const f32 cosine[8][8], Byte* output,
                u32 outputStride) noexcept
{
    const f32 inverseSqrt2 = 0.70710678f;
    f32 intermediate[64] = {};

    for (u32 row = 0; row < 8; ++row)
    {
        for (u32 x = 0; x < 8; ++x)
        {
            f32 sum = 0.0f;
            for (u32 u = 0; u < 8; ++u)
            {
                const f32 scale = u == 0 ? inverseSqrt2 : 1.0f;
                sum += scale * static_cast<f32>(coefficients[row * 8 + u]) * cosine[x][u];
            }
            intermediate[row * 8 + x] = sum * 0.5f;
        }
    }

    for (u32 column = 0; column < 8; ++column)
    {
        for (u32 y = 0; y < 8; ++y)
        {
            f32 sum = 0.0f;
            for (u32 v = 0; v < 8; ++v)
            {
                const f32 scale = v == 0 ? inverseSqrt2 : 1.0f;
                sum += scale * intermediate[v * 8 + column] * cosine[y][v];
            }
            const f32 value = sum * 0.5f + 128.0f;
            const i32 rounded = static_cast<i32>(std::floor(value + 0.5f));
            output[y * outputStride + column] =
                static_cast<Byte>(rounded < 0 ? 0 : (rounded > 255 ? 255 : rounded));
        }
    }
}

[[nodiscard]] u8 ClampToByte(f32 value) noexcept
{
    const i32 rounded = static_cast<i32>(std::floor(value + 0.5f));
    return static_cast<u8>(rounded < 0 ? 0 : (rounded > 255 ? 255 : rounded));
}

}  // namespace

ImportResult<Image> DecodeJpeg(Span<const Byte> bytes, Allocator& allocator) noexcept
{
    if (bytes.Size() < 4 || std::to_integer<u8>(bytes[0]) != 0xffu ||
        std::to_integer<u8>(bytes[1]) != kMarkerSoi)
    {
        return ImportResult<Image>::Err(
            MakeOffsetError(ErrorCode::ParseError, "missing JPEG start-of-image marker", 0));
    }

    u16 quantTables[4][64] = {};
    bool quantPresent[4] = {};
    HuffmanTable dcTables[4];
    HuffmanTable acTables[4];
    bool dcPresent[4] = {};
    bool acPresent[4] = {};

    u32 width = 0;
    u32 height = 0;
    u32 restartInterval = 0;
    i32 adobeTransform = -1;
    Component components[4];
    u32 componentCount = 0;
    u32 horizontalMax = 1;
    u32 verticalMax = 1;
    bool sawFrame = false;
    f32 cosine[8][8] = {};
    BuildCosineTable(cosine);

    usize position = 2;
    while (position + 1 < bytes.Size())
    {
        if (std::to_integer<u8>(bytes[position]) != 0xffu)
        {
            return ImportResult<Image>::Err(MakeOffsetError(
                ErrorCode::ParseError, "JPEG has stray data between segments", position));
        }
        ++position;
        while (position < bytes.Size() && std::to_integer<u8>(bytes[position]) == 0xffu)
        {
            ++position;
        }
        if (position >= bytes.Size())
        {
            break;
        }
        const u8 marker = std::to_integer<u8>(bytes[position]);
        ++position;

        if (marker == kMarkerEoi)
        {
            break;
        }
        if (marker == 0x01u || (marker >= 0xd0u && marker <= 0xd7u))
        {
            continue;  // Standalone markers carry no payload.
        }

        if (!InRange(bytes.Size(), position, 2))
        {
            return ImportResult<Image>::Err(
                MakeOffsetError(ErrorCode::ParseError, "truncated JPEG segment length", position));
        }
        const u32 segmentLength = ReadBigEndian16(bytes.Data() + position);
        if (segmentLength < 2)
        {
            return ImportResult<Image>::Err(
                MakeOffsetError(ErrorCode::ParseError, "JPEG segment length is invalid", position));
        }
        position += 2;
        const usize segmentSize = segmentLength - 2u;
        if (!InRange(bytes.Size(), position, segmentSize))
        {
            return ImportResult<Image>::Err(
                MakeOffsetError(ErrorCode::ParseError, "JPEG segment overruns the file", position));
        }
        const Byte* segment = bytes.Data() + position;

        switch (marker)
        {
        case kMarkerDqt:
        {
            usize cursor = 0;
            while (cursor < segmentSize)
            {
                const u8 header = std::to_integer<u8>(segment[cursor]);
                const u8 precision = static_cast<u8>(header >> 4u);
                const u8 table = static_cast<u8>(header & 0x0fu);
                if (precision > 1u || table > 3u)
                {
                    return ImportResult<Image>::Err(MakeOffsetError(
                        ErrorCode::ParseError, "JPEG quantization table is invalid", position));
                }
                const usize entrySize = precision == 0 ? 1u : 2u;
                ++cursor;
                if (cursor + entrySize * 64u > segmentSize)
                {
                    return ImportResult<Image>::Err(MakeOffsetError(
                        ErrorCode::ParseError, "JPEG quantization table is truncated", position));
                }
                for (u32 index = 0; index < 64; ++index)
                {
                    const usize entry = cursor + index * entrySize;
                    quantTables[table][index] =
                        precision == 0
                            ? static_cast<u16>(std::to_integer<u8>(segment[entry]))
                            : static_cast<u16>(
                                  (static_cast<u32>(std::to_integer<u8>(segment[entry])) << 8u) |
                                  static_cast<u32>(std::to_integer<u8>(segment[entry + 1])));
                }
                quantPresent[table] = true;
                cursor += entrySize * 64u;
            }
            break;
        }
        case kMarkerDht:
        {
            usize cursor = 0;
            while (cursor < segmentSize)
            {
                if (cursor + 17 > segmentSize)
                {
                    return ImportResult<Image>::Err(MakeOffsetError(
                        ErrorCode::ParseError, "JPEG Huffman table is truncated", position));
                }
                const u8 header = std::to_integer<u8>(segment[cursor]);
                const u8 tableClass = static_cast<u8>(header >> 4u);
                const u8 tableId = static_cast<u8>(header & 0x0fu);
                if (tableClass > 1u || tableId > 3u)
                {
                    return ImportResult<Image>::Err(
                        MakeOffsetError(ErrorCode::ParseError,
                                        "JPEG Huffman table identifier is invalid", position));
                }
                u8 counts[17] = {};
                u32 symbolCount = 0;
                for (u32 length = 1; length <= 16; ++length)
                {
                    counts[length] = std::to_integer<u8>(segment[cursor + length]);
                    symbolCount += counts[length];
                }
                cursor += 17;
                if (symbolCount > 256 || cursor + symbolCount > segmentSize)
                {
                    return ImportResult<Image>::Err(MakeOffsetError(
                        ErrorCode::ParseError, "JPEG Huffman table is truncated", position));
                }
                u8 symbols[256] = {};
                for (u32 index = 0; index < symbolCount; ++index)
                {
                    symbols[index] = std::to_integer<u8>(segment[cursor + index]);
                }
                HuffmanTable& target = tableClass == 0 ? dcTables[tableId] : acTables[tableId];
                if (!target.Build(counts, symbols, symbolCount))
                {
                    return ImportResult<Image>::Err(MakeOffsetError(
                        ErrorCode::ParseError, "JPEG Huffman table is malformed", position));
                }
                (tableClass == 0 ? dcPresent[tableId] : acPresent[tableId]) = true;
                cursor += symbolCount;
            }
            break;
        }
        case kMarkerDri:
        {
            if (segmentSize != 2)
            {
                return ImportResult<Image>::Err(MakeOffsetError(
                    ErrorCode::ParseError, "JPEG restart interval segment is invalid", position));
            }
            restartInterval = ReadBigEndian16(segment);
            break;
        }
        case kMarkerApp14:
        {
            if (segmentSize >= 12 && std::memcmp(segment, "Adobe", 5) == 0)
            {
                adobeTransform = static_cast<i32>(std::to_integer<u8>(segment[11]));
            }
            break;
        }
        case kMarkerSof0:
        case kMarkerSof1:
        {
            if (sawFrame)
            {
                return ImportResult<Image>::Err(MakeOffsetError(
                    ErrorCode::ParseError, "JPEG has more than one frame header", position));
            }
            if (segmentSize < 6)
            {
                return ImportResult<Image>::Err(MakeOffsetError(
                    ErrorCode::ParseError, "JPEG frame header is truncated", position));
            }
            const u8 precision = std::to_integer<u8>(segment[0]);
            height = ReadBigEndian16(segment + 1);
            width = ReadBigEndian16(segment + 3);
            componentCount = std::to_integer<u8>(segment[5]);
            if (precision != 8u)
            {
                return ImportResult<Image>::Err(MakeOffsetError(
                    ErrorCode::Unsupported, "JPEG precision is not 8 bits", position));
            }
            if (width == 0 || height == 0 || width > kMaxDimension || height > kMaxDimension)
            {
                return ImportResult<Image>::Err(MakeOffsetError(
                    ErrorCode::OutOfRange, "JPEG dimensions are unsupported", position));
            }
            if (static_cast<usize>(width) * height > kMaxPixels)
            {
                return ImportResult<Image>::Err(MakeOffsetError(
                    ErrorCode::OutOfRange, "JPEG pixel count is unsupported", position));
            }
            if (componentCount != 1 && componentCount != 3)
            {
                return ImportResult<Image>::Err(MakeOffsetError(
                    ErrorCode::Unsupported, "JPEG component count is unsupported", position));
            }
            if (segmentSize < 6 + static_cast<usize>(componentCount) * 3)
            {
                return ImportResult<Image>::Err(MakeOffsetError(
                    ErrorCode::ParseError, "JPEG frame header is truncated", position));
            }
            for (u32 index = 0; index < componentCount; ++index)
            {
                const Byte* entry = segment + 6 + index * 3;
                Component& component = components[index];
                component.id = std::to_integer<u8>(entry[0]);
                component.horizontal = static_cast<u8>(std::to_integer<u8>(entry[1]) >> 4u);
                component.vertical = static_cast<u8>(std::to_integer<u8>(entry[1]) & 0x0fu);
                component.quantTable = std::to_integer<u8>(entry[2]);
                if (component.horizontal < 1 || component.horizontal > 4 ||
                    component.vertical < 1 || component.vertical > 4)
                {
                    return ImportResult<Image>::Err(MakeOffsetError(
                        ErrorCode::ParseError, "JPEG sampling factor is invalid", position));
                }
                if (component.quantTable > 3)
                {
                    return ImportResult<Image>::Err(MakeOffsetError(
                        ErrorCode::ParseError, "JPEG quantization table id is invalid", position));
                }
                horizontalMax =
                    horizontalMax > component.horizontal ? horizontalMax : component.horizontal;
                verticalMax = verticalMax > component.vertical ? verticalMax : component.vertical;
            }
            sawFrame = true;
            break;
        }
        case kMarkerSof2:
        case kMarkerSof10:
            return ImportResult<Image>::Err(MakeOffsetError(
                ErrorCode::Unsupported, "progressive JPEG is not supported", position));
        case kMarkerSof9:
            return ImportResult<Image>::Err(MakeOffsetError(
                ErrorCode::Unsupported, "arithmetic-coded JPEG is not supported", position));
        case kMarkerSos:
        {
            if (!sawFrame)
            {
                return ImportResult<Image>::Err(MakeOffsetError(
                    ErrorCode::ParseError, "JPEG scan starts before the frame header", position));
            }
            if (segmentSize < 4)
            {
                return ImportResult<Image>::Err(MakeOffsetError(
                    ErrorCode::ParseError, "JPEG scan header is truncated", position));
            }
            const u8 scanComponents = std::to_integer<u8>(segment[0]);
            if (scanComponents != componentCount ||
                segmentSize < 1 + static_cast<usize>(scanComponents) * 2 + 3)
            {
                return ImportResult<Image>::Err(MakeOffsetError(
                    ErrorCode::ParseError, "JPEG scan header is inconsistent", position));
            }
            if (std::to_integer<u8>(segment[1 + scanComponents * 2]) != 0u ||
                std::to_integer<u8>(segment[2 + scanComponents * 2]) != 63u ||
                std::to_integer<u8>(segment[3 + scanComponents * 2]) != 0u)
            {
                return ImportResult<Image>::Err(MakeOffsetError(
                    ErrorCode::Unsupported, "JPEG scan parameters are not baseline", position));
            }
            for (u32 index = 0; index < scanComponents; ++index)
            {
                const u8 id = std::to_integer<u8>(segment[1 + index * 2]);
                const u8 tables = std::to_integer<u8>(segment[2 + index * 2]);
                const u8 dc = static_cast<u8>(tables >> 4u);
                const u8 ac = static_cast<u8>(tables & 0x0fu);
                Component* match = nullptr;
                for (u32 candidate = 0; candidate < componentCount; ++candidate)
                {
                    if (components[candidate].id == id)
                    {
                        match = &components[candidate];
                        break;
                    }
                }
                if (match == nullptr || dc > 3 || ac > 3 || !dcPresent[dc] || !acPresent[ac])
                {
                    return ImportResult<Image>::Err(MakeOffsetError(
                        ErrorCode::ParseError, "JPEG scan references a missing table", position));
                }
                match->dcTable = dc;
                match->acTable = ac;
            }
            for (u32 index = 0; index < componentCount; ++index)
            {
                if (!quantPresent[components[index].quantTable])
                {
                    return ImportResult<Image>::Err(
                        MakeOffsetError(ErrorCode::ParseError,
                                        "JPEG component has no quantization table", position));
                }
            }

            const u32 mcusX = (width + 8u * horizontalMax - 1u) / (8u * horizontalMax);
            const u32 mcusY = (height + 8u * verticalMax - 1u) / (8u * verticalMax);

            // Each component plane is padded to whole blocks so every block write
            // inside an MCU stays in range without a per-block check.
            DynamicArray<Byte> planes[4] = {
                DynamicArray<Byte>(allocator), DynamicArray<Byte>(allocator),
                DynamicArray<Byte>(allocator), DynamicArray<Byte>(allocator)};
            for (u32 index = 0; index < componentCount; ++index)
            {
                Component& component = components[index];
                const u32 blocksX =
                    (width * component.horizontal + 8u * horizontalMax - 1u) / (8u * horizontalMax);
                const u32 blocksY =
                    (height * component.vertical + 8u * verticalMax - 1u) / (8u * verticalMax);
                const usize planeSize = static_cast<usize>(blocksX) * 8u * blocksY * 8u;
                if (!planes[index].TryResize(planeSize))
                {
                    return ImportResult<Image>::Err(
                        MakeError(ErrorCode::OutOfRange, "out of memory for JPEG components"));
                }
                component.stride = blocksX * 8u;
                component.height = blocksY * 8u;
                component.plane = planes[index].Data();
            }

            const usize entropyStart = position + segmentSize;
            EntropyReader reader(bytes.Data(), bytes.Size(), entropyStart);

            i32 coefficients[64] = {};
            u32 mcuIndex = 0;
            bool decodeFailed = false;

            for (u32 mcuY = 0; mcuY < mcusY && !decodeFailed; ++mcuY)
            {
                for (u32 mcuX = 0; mcuX < mcusX && !decodeFailed; ++mcuX)
                {
                    if (restartInterval != 0 && mcuIndex != 0 && mcuIndex % restartInterval == 0)
                    {
                        if (!reader.ConsumeRestart())
                        {
                            decodeFailed = true;
                            break;
                        }
                        for (u32 index = 0; index < componentCount; ++index)
                        {
                            components[index].dcPrediction = 0;
                        }
                    }
                    ++mcuIndex;

                    for (u32 index = 0; index < componentCount && !decodeFailed; ++index)
                    {
                        Component& component = components[index];
                        for (u32 blockY = 0; blockY < component.vertical && !decodeFailed; ++blockY)
                        {
                            for (u32 blockX = 0; blockX < component.horizontal; ++blockX)
                            {
                                memset(coefficients, 0, sizeof(coefficients));
                                const HuffmanTable& dcTable = dcTables[component.dcTable];
                                const HuffmanTable& acTable = acTables[component.acTable];

                                const i32 dcLength = reader.DecodeSymbol(dcTable);
                                u32 dcBits = 0;
                                if (dcLength < 0 || dcLength > 11 ||
                                    !reader.Bits(static_cast<u32>(dcLength), dcBits))
                                {
                                    decodeFailed = true;
                                    break;
                                }
                                component.dcPrediction +=
                                    Extend(static_cast<i32>(dcBits), static_cast<u32>(dcLength));
                                coefficients[0] =
                                    component.dcPrediction *
                                    static_cast<i32>(quantTables[component.quantTable][0]);

                                u32 zigzag = 1;
                                while (zigzag < 64)
                                {
                                    const i32 symbol = reader.DecodeSymbol(acTable);
                                    if (symbol < 0)
                                    {
                                        decodeFailed = true;
                                        break;
                                    }
                                    const u32 run = static_cast<u32>(symbol) >> 4u;
                                    const u32 size = static_cast<u32>(symbol) & 0x0fu;
                                    if (size == 0)
                                    {
                                        if (run != 15u)
                                        {
                                            break;  // End of block.
                                        }
                                        zigzag += 16;
                                        continue;
                                    }
                                    zigzag += run;
                                    if (zigzag >= 64)
                                    {
                                        decodeFailed = true;
                                        break;
                                    }
                                    u32 acBits = 0;
                                    if (!reader.Bits(size, acBits))
                                    {
                                        decodeFailed = true;
                                        break;
                                    }
                                    coefficients[kZigZag[zigzag]] =
                                        Extend(static_cast<i32>(acBits), size) *
                                        static_cast<i32>(quantTables[component.quantTable][zigzag]);
                                    ++zigzag;
                                }

                                if (!decodeFailed)
                                {
                                    const u32 destinationX =
                                        (mcuX * component.horizontal + blockX) * 8u;
                                    const u32 destinationY =
                                        (mcuY * component.vertical + blockY) * 8u;
                                    InverseDct(coefficients, cosine,
                                               component.plane + destinationY * component.stride +
                                                   destinationX,
                                               component.stride);
                                }
                            }
                        }
                    }
                }
            }

            if (decodeFailed)
            {
                return ImportResult<Image>::Err(
                    MakeOffsetError(ErrorCode::ParseError, "JPEG entropy-coded data is invalid",
                                    reader.Position()));
            }

            // A complete scan must be followed by EOI. Padding and trailing
            // metadata between the scan and EOI are tolerated.
            bool sawEndOfImage = false;
            for (usize index = entropyStart; index + 1 < bytes.Size(); ++index)
            {
                if (std::to_integer<u8>(bytes[index]) == 0xffu &&
                    std::to_integer<u8>(bytes[index + 1]) == kMarkerEoi)
                {
                    sawEndOfImage = true;
                    break;
                }
            }
            if (!sawEndOfImage)
            {
                return ImportResult<Image>::Err(
                    MakeOffsetError(ErrorCode::ParseError,
                                    "JPEG is missing its end-of-image marker", reader.Position()));
            }

            Image image(allocator);
            auto resized = image.Resize(width, height);
            if (resized.IsError())
            {
                return ImportResult<Image>::Err(resized.Error());
            }

            const bool ycbcr = componentCount == 3 && adobeTransform != 0;
            for (u32 y = 0; y < height; ++y)
            {
                for (u32 x = 0; x < width; ++x)
                {
                    auto sample = [&](u32 componentIndex) noexcept
                    {
                        const Component& component = components[componentIndex];
                        const u32 sampleX = (x * component.horizontal) / horizontalMax;
                        const u32 sampleY = (y * component.vertical) / verticalMax;
                        return std::to_integer<u8>(
                            component.plane[sampleY * component.stride + sampleX]);
                    };

                    if (componentCount == 1)
                    {
                        const u8 luma = sample(0);
                        image.SetPixel(x, y, static_cast<Byte>(luma), static_cast<Byte>(luma),
                                       static_cast<Byte>(luma), Byte{255});
                        continue;
                    }

                    if (!ycbcr)
                    {
                        image.SetPixel(x, y, static_cast<Byte>(sample(0)),
                                       static_cast<Byte>(sample(1)), static_cast<Byte>(sample(2)),
                                       Byte{255});
                        continue;
                    }

                    const f32 luma = static_cast<f32>(sample(0));
                    const f32 cb = static_cast<f32>(sample(1)) - 128.0f;
                    const f32 cr = static_cast<f32>(sample(2)) - 128.0f;
                    image.SetPixel(
                        x, y, static_cast<Byte>(ClampToByte(luma + 1.402f * cr)),
                        static_cast<Byte>(ClampToByte(luma - 0.344136f * cb - 0.714136f * cr)),
                        static_cast<Byte>(ClampToByte(luma + 1.772f * cb)), Byte{255});
                }
            }
            return ImportResult<Image>::Ok(std::move(image));
        }
        default:
            break;
        }

        position += segmentSize;
    }

    return ImportResult<Image>::Err(MakeError(ErrorCode::ParseError, "JPEG has no decodable scan"));
}

}  // namespace cyber::assets

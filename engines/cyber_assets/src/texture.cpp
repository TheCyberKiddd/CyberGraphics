#include <cyber/assets/texture.h>

#include <cstring>

namespace cyber::assets
{
namespace
{

constexpr u32 kMaxTextureDimension = 32768;
constexpr u64 kMaxTextureBytes = 512ull * 1024ull * 1024ull;

}  // namespace

ImportResult<u64> TextureLevelSize(TextureFormat format, u32 width, u32 height,
                                   u32& rowPitch) noexcept
{
    const u32 unit = TextureBlockBytes(format);
    if (unit == 0 || width == 0 || height == 0)
    {
        return ImportResult<u64>::Err(
            MakeError(ErrorCode::InvalidArgument, "texture format or size is invalid"));
    }
    if (IsBlockCompressed(format))
    {
        const u64 blocksX = TextureBlocksAcross(width);
        const u64 blocksY = TextureBlocksAcross(height);
        rowPitch = static_cast<u32>(blocksX * unit);
        return ImportResult<u64>::Ok(blocksX * blocksY * unit);
    }
    rowPitch = static_cast<u32>(static_cast<u64>(width) * unit);
    return ImportResult<u64>::Ok(static_cast<u64>(width) * height * unit);
}

ImportStatus TextureAsset::Validate() const noexcept
{
    if (format == TextureFormat::Unknown)
    {
        return ImportStatus::Err(
            MakeError(ErrorCode::InvalidArgument, "texture format is unknown"));
    }
    if (width == 0 || height == 0 || width > kMaxTextureDimension ||
        height > kMaxTextureDimension || depth == 0)
    {
        return ImportStatus::Err(
            MakeError(ErrorCode::InvalidArgument, "texture dimensions are invalid"));
    }
    if (mipCount == 0 || mipCount > 32 || layerCount == 0 || layerCount > 4096)
    {
        return ImportStatus::Err(
            MakeError(ErrorCode::InvalidArgument, "texture level or layer count is invalid"));
    }
    if (levels.Size() != mipCount)
    {
        return ImportStatus::Err(
            MakeError(ErrorCode::InvalidArgument, "texture level table does not match mip count"));
    }
    if (data.Size() > kMaxTextureBytes)
    {
        return ImportStatus::Err(MakeError(ErrorCode::OutOfRange, "texture payload is too large"));
    }

    u32 expectedWidth = width;
    u32 expectedHeight = height;
    for (u32 level = 0; level < mipCount; ++level)
    {
        const TextureLevel& record = levels[level];
        if (record.width != expectedWidth || record.height != expectedHeight)
        {
            return ImportStatus::Err(
                MakeError(ErrorCode::InvalidArgument, "texture level dimensions are inconsistent"));
        }
        u32 rowPitch = 0;
        auto levelSize = TextureLevelSize(format, record.width, record.height, rowPitch);
        if (levelSize.IsError())
        {
            return ImportStatus::Err(levelSize.Error());
        }
        const u64 expectedSize = levelSize.Value() * layerCount;
        if (record.rowPitch != rowPitch)
        {
            return ImportStatus::Err(
                MakeError(ErrorCode::InvalidArgument, "texture level row pitch is wrong"));
        }
        if (record.size != expectedSize || record.offset + record.size > data.Size())
        {
            return ImportStatus::Err(
                MakeError(ErrorCode::InvalidArgument, "texture level range is wrong"));
        }
        // Levels are tightly packed after the 16-byte-aligned payload start; the
        // cooked writer is responsible for aligning that start, not each mip.
        if (level == 0 && record.offset != 0)
        {
            return ImportStatus::Err(
                MakeError(ErrorCode::InvalidArgument, "texture payload does not start at zero"));
        }
        expectedWidth = expectedWidth > 1 ? expectedWidth / 2 : 1;
        expectedHeight = expectedHeight > 1 ? expectedHeight / 2 : 1;
    }
    return ImportStatus::Ok();
}

ImportResult<TextureAsset> TextureFromImage(const Image& image, StringView name,
                                            Allocator& allocator) noexcept
{
    if (image.Width() == 0 || image.Height() == 0)
    {
        return ImportResult<TextureAsset>::Err(
            MakeError(ErrorCode::InvalidArgument, "image has no pixels"));
    }

    TextureAsset texture(allocator);
    texture.format = TextureFormat::Rgba8Unorm;
    texture.width = image.Width();
    texture.height = image.Height();
    texture.mipCount = 1;
    texture.layerCount = 1;
    if (!texture.name.TryAssign(name))
    {
        return ImportResult<TextureAsset>::Err(
            MakeError(ErrorCode::OutOfRange, "out of memory for a texture name"));
    }
    if (!texture.data.TryResize(image.SizeBytes()) || !texture.levels.TryResize(1))
    {
        return ImportResult<TextureAsset>::Err(
            MakeError(ErrorCode::OutOfRange, "out of memory for a texture payload"));
    }
    if (!image.Pixels().Empty())
    {
        std::memcpy(texture.data.Data(), image.Pixels().Data(), image.Pixels().Size());
    }

    u32 rowPitch = 0;
    auto levelSize =
        TextureLevelSize(TextureFormat::Rgba8Unorm, texture.width, texture.height, rowPitch);
    if (levelSize.IsError())
    {
        return ImportResult<TextureAsset>::Err(levelSize.Error());
    }
    texture.levels[0] = TextureLevel{0, levelSize.Value(), texture.width, texture.height, rowPitch};

    const ImportStatus validated = texture.Validate();
    if (validated.IsError())
    {
        return ImportResult<TextureAsset>::Err(validated.Error());
    }
    return ImportResult<TextureAsset>::Ok(std::move(texture));
}

}  // namespace cyber::assets

#include "Main.h"
#include "ArchivePreview.h"

#include <cstdint>
#include <vector>

namespace slade_mobile
{
// Flat: raw indexed pixels, row-major, no header at all -- just a wall of
// palette-index bytes. androidDetectEntryType() only classifies exactly
// 4096 (classic 64x64) or 4160 (Heretic/Hexen animated liquid flats, 64x64
// plus 64 trailing bytes of engine-specific data that isn't part of the
// pixel grid) as "Flat", so 64x64 read from the front covers both cases.
std::vector<int32_t> decodeFlat(const uint8_t* data, uint32_t size, const uint8_t* pal)
{
    constexpr int kDim = 64;
    constexpr uint32_t kPixels = kDim * kDim;
    if (!data || !pal || size < kPixels)
        return {};
    std::vector<int32_t> pixels(static_cast<size_t>(kDim) * kDim);
    for (int i = 0; i < kDim * kDim; ++i)
    {
        const uint8_t idx = data[i];
        const uint8_t r   = pal[idx * 3 + 0];
        const uint8_t g   = pal[idx * 3 + 1];
        const uint8_t b   = pal[idx * 3 + 2];
        pixels[i] = static_cast<int32_t>(0xFF000000u | (static_cast<uint32_t>(r) << 16)
                                       | (static_cast<uint32_t>(g) << 8) | static_cast<uint32_t>(b));
    }
    return pixels;
}

// Doom picture (patch) format: 4x int16 header (width, height, left/top
// offset -- offsets are for sprite placement and don't affect the pixel
// grid itself, so they're read but unused here), followed by `width`
// little-endian uint32 column offsets relative to the start of the entry.
// Each column is a run-length-encoded list of opaque "posts":
//   u8 topdelta (0xFF marks end-of-column)
//   u8 length
//   u8 unused (padding byte -- DOOM.EXE reads a byte here it never uses)
//   u8[length] pixel data (palette indices)
//   u8 unused (trailing padding byte)
// Anything not covered by a post stays fully transparent (alpha 0) --
// that's the whole point of the format; sprites/HUD graphics are
// non-rectangular cutouts, not solid rectangles. This is the classic
// decode, not the "tall patch" (DeepSea) variant some newer source ports
// support for patches taller than 254px -- fine for the vanilla-range
// IWADs/PWADs this app has been validated against so far.
std::vector<int32_t> decodeDoomGraphic(const uint8_t* data, uint32_t size, const uint8_t* pal, int* outW, int* outH)
{
    if (!outW || !outH)
        return {};
    *outW = 0;
    *outH = 0;
    if (!data || !pal || size < 8)
        return {};

    const uint16_t width  = static_cast<uint16_t>(data[0] | (data[1] << 8));
    const uint16_t height = static_cast<uint16_t>(data[2] | (data[3] << 8));

    const uint64_t columnTableEnd = 8ull + static_cast<uint64_t>(width) * 4ull;
    if (columnTableEnd > size)
        return {};

    std::vector<int32_t> pixels(static_cast<size_t>(width) * height, 0); // transparent by default

    for (uint16_t col = 0; col < width; ++col)
    {
        const uint32_t colOfsPos = 8u + static_cast<uint32_t>(col) * 4u;
        const uint32_t colOfs    = static_cast<uint32_t>(data[colOfsPos]) | (static_cast<uint32_t>(data[colOfsPos + 1]) << 8)
                                 | (static_cast<uint32_t>(data[colOfsPos + 2]) << 16)
                                 | (static_cast<uint32_t>(data[colOfsPos + 3]) << 24);
        if (colOfs >= size)
            continue; // corrupt offset -- skip this column, keep decoding the rest

        uint32_t pos = colOfs;
        while (pos < size)
        {
            const uint8_t topdelta = data[pos];
            if (topdelta == 0xFF)
                break;
            if (pos + 1 >= size)
                break;

            const uint8_t  length    = data[pos + 1];
            const uint32_t dataStart = pos + 3; // skip topdelta, length, unused padding byte
            if (static_cast<uint64_t>(dataStart) + length > size)
                break; // corrupt -- bail on this column rather than read out of bounds

            for (uint8_t y = 0; y < length; ++y)
            {
                const int row = static_cast<int>(topdelta) + y;
                if (row < 0 || row >= height)
                    continue;
                const uint8_t idx = data[dataStart + y];
                const uint8_t r   = pal[idx * 3 + 0];
                const uint8_t g   = pal[idx * 3 + 1];
                const uint8_t b   = pal[idx * 3 + 2];
                pixels[static_cast<size_t>(row) * width + col] = static_cast<int32_t>(
                        0xFF000000u | (static_cast<uint32_t>(r) << 16) | (static_cast<uint32_t>(g) << 8)
                        | static_cast<uint32_t>(b));
            }

            pos = dataStart + length + 1; // + trailing unused padding byte
        }
    }

    *outW = width;
    *outH = height;
    return pixels;
}


} // namespace slade_mobile

// Copyright 2026 N-GINN LLC. All rights reserved.
// Use of this source code is governed by a BSD-3 Clause license that can be found in the LICENSE file.
#include "nau/text/screen_text.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <tuple>

#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>

namespace nau::text
{
    class Font
    {
    public:
        std::vector<std::byte> bytes;
        stbtt_fontinfo info{};
    };

    namespace
    {
        Result<std::vector<int>> decode(std::string_view input)
        {
            std::vector<int> result;
            for (size_t i = 0; i < input.size();)
            {
                const auto first = static_cast<unsigned char>(input[i++]);
                int code = first, extra = 0, minimum = 0;
                if (first >= 0xC2 && first <= 0xDF)
                    code = first & 31, extra = 1, minimum = 0x80;
                else if (first >= 0xE0 && first <= 0xEF)
                    code = first & 15, extra = 2, minimum = 0x800;
                else if (first >= 0xF0 && first <= 0xF4)
                    code = first & 7, extra = 3, minimum = 0x10000;
                else if (first >= 0x80)
                    return NauMakeError("Text contains invalid UTF-8");
                for (int n = 0; n < extra; ++n)
                {
                    if (i >= input.size() || (static_cast<unsigned char>(input[i]) & 0xC0) != 0x80)
                        return NauMakeError("Text contains truncated or invalid UTF-8");
                    code = (code << 6) | (static_cast<unsigned char>(input[i++]) & 63);
                }
                if (code < minimum || code > 0x10FFFF || (code >= 0xD800 && code <= 0xDFFF))
                    return NauMakeError("Text contains invalid Unicode codepoint");
                if (code < 32 || code == 127 || code == 0x85 || code == 0x2028 || code == 0x2029)
                    return NauMakeError("Screen text supports a single line without control characters");
                if (result.size() == MaxCodepoints)
                    return NauMakeError("Text exceeds {} codepoints", MaxCodepoints);
                result.push_back(code);
            }
            return result;
        }

        bool validEnvelope(std::span<const std::byte> bytes)
        {
            const auto u16 = [&](size_t n)
            {
                return (unsigned(bytes[n]) << 8) | unsigned(bytes[n + 1]);
            };
            const auto u32 = [&](size_t n)
            {
                return (uint32_t(u16(n)) << 16) | u16(n + 2);
            };
            if (bytes.size() < 12 || u32(0) != 0x00010000)
                return false;
            const unsigned count = u16(4);
            if (count == 0 || count > 128 || 12 + count * 16 > bytes.size())
                return false;
            std::map<uint32_t, unsigned> tables;
            for (unsigned i = 0; i < count; ++i)
            {
                const size_t entry = 12 + i * 16;
                const uint32_t offset = u32(entry + 8), length = u32(entry + 12);
                if (offset > bytes.size() || length > bytes.size() - offset)
                    return false;
                tables[u32(entry)] = length;
            }
            // stb expects these TrueType tables. Internal glyph structures are
            // still trusted; table bounds alone do not validate hostile fonts.
            return tables[0x636D6170] >= 4 && tables[0x68656164] >= 54 &&
                   tables[0x68686561] >= 36 && tables[0x686D7478] >= 4 &&
                   tables[0x6D617870] >= 6 && tables[0x6C6F6361] >= 4 &&
                   tables[0x676C7966] > 0;
        }

        struct Glyph
        {
            int index, x0, y0, x1, y1;
            float pen;
        };

        Result<BitmapHandle> makeBitmap(const Font& font, std::span<const int> codes, float size, float density, float& advance)
        {
            const float scale = stbtt_ScaleForPixelHeight(&font.info, size * density);
            float pen = 0;
            int previous = -1, minX = 0, minY = 0, maxX = 0, maxY = 0;
            bool hasPixels = false;
            std::vector<Glyph> glyphs;
            for (const int code : codes)
            {
                const int index = stbtt_FindGlyphIndex(&font.info, code);
                if (previous >= 0)
                    pen += stbtt_GetGlyphKernAdvance(&font.info, previous, index) * scale;
                int x0, y0, x1, y1;
                const float shift = pen - std::floor(pen);
                stbtt_GetGlyphBitmapBoxSubpixel(&font.info, index, scale, scale, shift, 0, &x0, &y0, &x1, &y1);
                if (x1 > x0 && y1 > y0)
                {
                    const int x = static_cast<int>(std::floor(pen));
                    if (!hasPixels)
                        minX = x + x0, minY = y0, maxX = x + x1, maxY = y1;
                    else
                        minX = std::min(minX, x + x0), minY = std::min(minY, y0),
                        maxX = std::max(maxX, x + x1), maxY = std::max(maxY, y1);
                    hasPixels = true;
                    glyphs.push_back({index, x0, y0, x1, y1, pen});
                }
                int width, bearing;
                stbtt_GetGlyphHMetrics(&font.info, index, &width, &bearing);
                pen += width * scale;
                if (!std::isfinite(pen) || std::abs(pen) > MaxBitmapDimension)
                    return NauMakeError("Text advance exceeds bitmap limits");
                previous = index;
            }
            advance = pen / density;
            auto bitmap = std::make_shared<TextBitmap>();
            bitmap->scale = density;
            if (!hasPixels)
                return BitmapHandle(bitmap);
            bitmap->width = maxX - minX;
            bitmap->height = maxY - minY;
            bitmap->left = minX;
            int ascent, descent, gap;
            stbtt_GetFontVMetrics(&font.info, &ascent, &descent, &gap);
            bitmap->top = static_cast<int>(std::ceil(ascent * scale)) + minY;
            if (bitmap->width > MaxBitmapDimension || bitmap->height > MaxBitmapDimension ||
                size_t(bitmap->width) * bitmap->height > MaxBitmapBytes)
                return NauMakeError("Text coverage exceeds bitmap limits");
            bitmap->coverage.resize(size_t(bitmap->width) * bitmap->height);
            for (const auto& glyph : glyphs)
            {
                const int w = glyph.x1 - glyph.x0, h = glyph.y1 - glyph.y0;
                std::vector<unsigned char> pixels(size_t(w) * h);
                stbtt_MakeGlyphBitmapSubpixel(&font.info, pixels.data(), w, h, w, scale, scale,
                                              glyph.pen - std::floor(glyph.pen), 0, glyph.index);
                const int left = int(std::floor(glyph.pen)) + glyph.x0 - minX;
                const int top = glyph.y0 - minY;
                for (int y = 0; y < h; ++y)
                    for (int x = 0; x < w; ++x)
                    {
                        auto& dst = bitmap->coverage[size_t(top + y) * bitmap->width + left + x];
                        const unsigned src = pixels[size_t(y) * w + x];
                        dst = static_cast<unsigned char>(src + (dst * (255 - src) + 127) / 255);
                    }
            }
            return BitmapHandle(bitmap);
        }
    }  // namespace

    struct ScreenText::State
    {
        mutable std::mutex mutex;
        bool stopped = false;
        std::map<std::tuple<const Font*, std::string, float>, std::weak_ptr<const TextLine>> lines;
        struct CachedImage
        {
            std::weak_ptr<const TextLine> owner;
            std::weak_ptr<const TextBitmap> bitmap;
        };
        std::map<std::pair<const TextLine*, float>, CachedImage> images;
        std::shared_ptr<const TextFrame> frame = std::make_shared<TextFrame>();
        async::TaskSource<> presented;
        bool presentationReported = false;
    };

    ScreenText::ScreenText() :
        m_state(std::make_unique<State>())
    {
    }
    ScreenText::~ScreenText() = default;

    Result<FontHandle> ScreenText::loadFont(io::IVirtualFileSystem& filesystem, std::string_view path)
    {
        std::lock_guard lock(m_state->mutex);
        if (m_state->stopped)
            return NauMakeError("Font load after text shutdown: {}", path);
        auto file = filesystem.openFile(io::FsPath(path), io::AccessMode::Read, io::OpenFileMode::OpenExisting);
        if (!file || !file->getSize() || file->getSize() > MaxFontBytes)
            return NauMakeError("Missing, empty or oversized TTF asset: {}", path);
        auto font = std::make_shared<Font>();
        font->bytes.resize(file->getSize());
        auto base = file->createStream(io::AccessMode::Read);
        auto* reader = base ? base->as<io::IStreamReader*>() : nullptr;
        size_t offset = 0;
        while (reader && offset < font->bytes.size())
        {
            auto read = reader->read(font->bytes.data() + offset, font->bytes.size() - offset);
            if (!read || !*read || *read > font->bytes.size() - offset)
                return NauMakeError("Could not read TTF asset: {}", path);
            offset += *read;
        }
        if (offset != font->bytes.size() || !validEnvelope(font->bytes) ||
            !stbtt_InitFont(&font->info, reinterpret_cast<const unsigned char*>(font->bytes.data()), 0))
            return NauMakeError("Unsupported or invalid TTF asset: {}", path);
        int ascent, descent, gap;
        stbtt_GetFontVMetrics(&font->info, &ascent, &descent, &gap);
        if (ascent <= descent)
            return NauMakeError("Invalid TTF metrics: {}", path);
        return FontHandle(font);
    }

    Result<TextHandle> ScreenText::createText(FontHandle font, std::string_view utf8, float size)
    {
        std::lock_guard lock(m_state->mutex);
        if (m_state->stopped || !font || !std::isfinite(size) || size <= 0 || size > 512)
            return NauMakeError("Invalid text font/size or service stopped");
        auto codes = decode(utf8);
        if (!codes)
            return codes.getError();
        std::erase_if(m_state->lines, [](const auto& entry)
        {
            return entry.second.expired();
        });
        const auto key = std::make_tuple(font.get(), std::string(utf8), size);
        if (auto existing = m_state->lines[key].lock())
            return existing;
        if (m_state->lines.size() > 128)
            return NauMakeError("Too many live prepared text lines");
        auto line = std::make_shared<TextLine>();
        line->font = std::move(font);
        line->utf8 = utf8;
        line->size = size;
        auto bitmap = makeBitmap(*line->font, *codes, size, 1, line->advance);
        if (!bitmap)
            return bitmap.getError();
        line->bitmap = *bitmap;
        int ascent, descent, gap;
        stbtt_GetFontVMetrics(&line->font->info, &ascent, &descent, &gap);
        const float scale = stbtt_ScaleForPixelHeight(&line->font->info, size);
        line->ascent = ascent * scale;
        line->descent = descent * scale;
        m_state->lines[key] = line;
        return TextHandle(line);
    }

    Result<BitmapHandle> ScreenText::rasterize(TextHandle line, float density)
    {
        std::lock_guard lock(m_state->mutex);
        if (m_state->stopped || !line || !std::isfinite(density) || density <= 0 || density > 8)
            return NauMakeError("Invalid text raster scale or service stopped");
        if (density == 1)
            return line->bitmap;
        std::erase_if(m_state->images, [](const auto& entry)
        {
            return entry.second.owner.expired() || entry.second.bitmap.expired();
        });
        const auto key = std::make_pair(line.get(), density);
        if (auto existing = m_state->images[key].bitmap.lock())
            return existing;
        if (m_state->images.size() > 128)
            return NauMakeError("Too many live text raster images");
        auto codes = decode(line->utf8);
        if (!codes)
            return codes.getError();
        float advance;
        auto bitmap = makeBitmap(*line->font, *codes, line->size, density, advance);
        if (bitmap)
            m_state->images[key] = {line, *bitmap};
        return bitmap;
    }

    Result<> ScreenText::publish(TextFrame commands)
    {
        std::lock_guard lock(m_state->mutex);
        if (m_state->stopped || commands.size() > 128)
            return NauMakeError("Text submission after Stop or beyond frame limit");
        for (const auto& command : commands)
        {
            if (!command.text)
                return NauMakeError("Text command has no prepared line");
            for (const float value : command.position)
                if (!std::isfinite(value) || std::abs(value) > 1e6f)
                    return NauMakeError("Invalid text position");
            for (const float value : command.color)
                if (!std::isfinite(value) || value < 0 || value > 1)
                    return NauMakeError("Invalid text color");
        }
        m_state->frame = std::make_shared<const TextFrame>(std::move(commands));
        return {};
    }

    Result<> ScreenText::draw(TextHandle line, std::array<float, 2> position, std::array<float, 4> color)
    {
        return publish({
            TextDraw{std::move(line), position, color}
        });
    }

    std::shared_ptr<const TextFrame> ScreenText::snapshot() const
    {
        std::lock_guard lock(m_state->mutex);
        return m_state->frame;
    }

    void ScreenText::stop()
    {
        completePresentation(NauMakeError("Screen text stopped before first presentation"));
        std::lock_guard lock(m_state->mutex);
        m_state->stopped = true;
        m_state->frame = std::make_shared<const TextFrame>();
        m_state->lines.clear();
        m_state->images.clear();
    }

    async::Task<> ScreenText::whenPresented()
    {
        std::lock_guard lock(m_state->mutex);
        return m_state->presented.getTask();
    }

    void ScreenText::completePresentation(Result<> result)
    {
        {
            std::lock_guard lock(m_state->mutex);
            if (m_state->presentationReported)
                return;
            m_state->presentationReported = true;
        }
        if (result)
            m_state->presented.resolve();
        else
            m_state->presented.reject(result.getError());
    }

    async::Task<> ScreenText::shutdownService()
    {
        stop();
        return async::Task<>::makeResolved();
    }
}  // namespace nau::text

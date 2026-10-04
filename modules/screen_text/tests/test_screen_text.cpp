// Copyright 2026 N-GINN LLC. All rights reserved.
// Use of this source code is governed by a BSD-3 Clause license that can be found in the LICENSE file.
#include <gtest/gtest.h>

#include <algorithm>
#include <fstream>
#include <limits>

#include "nau/assets/cpu_mesh.h"
#include "nau/text/screen_text.h"

namespace nau::test
{
    class ScreenTextTests : public testing::Test
    {
    protected:
        text::ScreenText text;
        io::IVirtualFileSystem::Ptr vfs;
        text::FontHandle font;

        void SetUp() override
        {
            std::ifstream input(NAU_TEXT_FIXTURE_DIR "/Abel-Regular.ttf", std::ios::binary | std::ios::ate);
            ASSERT_TRUE(input);
            std::vector<std::byte> bytes(size_t(input.tellg()));
            input.seekg(0);
            ASSERT_TRUE(input.read(reinterpret_cast<char*>(bytes.data()), bytes.size()));
            auto filesystem = createCpuAssetFileSystem({
                {"Abel-Regular.ttf",           std::move(bytes)},
                {       "empty.ttf",                         {}},
                {     "invalid.ttf", std::vector<std::byte>(12)}
            });
            ASSERT_TRUE(filesystem);
            vfs = io::createVirtualFileSystem();
            ASSERT_TRUE(vfs->mount("/fonts", *filesystem));
            auto loaded = text.loadFont(*vfs, "/fonts/Abel-Regular.ttf");
            ASSERT_TRUE(loaded);
            font = *loaded;
        }
    };

    TEST_F(ScreenTextTests, FontIsSpecifiedAndOwnsBytes)
    {
        EXPECT_FALSE(text.loadFont(*vfs, "/fonts/missing.ttf"));
        EXPECT_FALSE(text.loadFont(*vfs, "/fonts/empty.ttf"));
        EXPECT_FALSE(text.loadFont(*vfs, "/fonts/invalid.ttf"));
        vfs = nullptr;
        auto line = text.createText(font, "Nau Demo", 24);
        ASSERT_TRUE(line);
        EXPECT_GT((*line)->bitmap->width, 50);
        EXPECT_EQ((*line)->ascent - (*line)->descent, 24);
        EXPECT_GT(std::count_if((*line)->bitmap->coverage.begin(), (*line)->bitmap->coverage.end(),
                                [](unsigned char value)
        {
            return value > 0;
        }),
                  100);
    }

    TEST_F(ScreenTextTests, RejectsOversizedFontBeforeParsing)
    {
        auto filesystem = createCpuAssetFileSystem({
            {"large.ttf", std::vector<std::byte>(text::MaxFontBytes + 1)}
        });
        ASSERT_TRUE(filesystem);
        ASSERT_TRUE(vfs->mount("/large", *filesystem));
        EXPECT_FALSE(text.loadFont(*vfs, "/large/large.ttf"));
    }

    TEST_F(ScreenTextTests, LineLayoutIncludesSpacingAndDescenders)
    {
        auto a = text.createText(font, "A", 24);
        auto v = text.createText(font, "V", 24);
        auto pair = text.createText(font, "AV", 24);
        auto spaced = text.createText(font, " A ", 24);
        auto descender = text.createText(font, "jgy", 24);
        ASSERT_TRUE(a && v && pair && spaced && descender);
        EXPECT_LE((*pair)->advance, (*a)->advance + (*v)->advance);
        EXPECT_GT((*spaced)->advance, (*a)->advance);
        EXPECT_GT((*spaced)->bitmap->left, (*a)->bitmap->left);
        EXPECT_GT((*descender)->bitmap->top + (*descender)->bitmap->height, (*descender)->ascent);
    }

    TEST_F(ScreenTextTests, EmptyAndMissingGlyphHaveDefinedBehavior)
    {
        auto empty = text.createText(font, "", 24);
        auto spaces = text.createText(font, "   ", 24);
        auto missing = text.createText(font, "\xf4\x8f\xbf\xbf", 24);
        ASSERT_TRUE(empty && spaces && missing);
        EXPECT_TRUE((*empty)->bitmap->coverage.empty());
        EXPECT_TRUE((*spaces)->bitmap->coverage.empty());
        EXPECT_GT((*spaces)->advance, 0);
        EXPECT_GT((*missing)->advance, 0);
    }

    TEST_F(ScreenTextTests, RejectsInvalidTextSizeAndAllocation)
    {
        for (std::string_view invalid : {"\xff", "\xc0\x80", "\xed\xa0\x80", "\xf4\x90\x80\x80", "\xe2", "a\nb", "\t"})
            EXPECT_FALSE(text.createText(font, invalid, 24));
        for (float size : {0.f, -1.f, 513.f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()})
            EXPECT_FALSE(text.createText(font, "hello", size));
        EXPECT_FALSE(text.createText(font, std::string(text::MaxCodepoints + 1, 'A'), 24));
        EXPECT_FALSE(text.createText(font, std::string(200, 'W'), 512));
    }

    TEST_F(ScreenTextTests, PreparationAndRasterCachesFollowHandles)
    {
        auto first = text.createText(font, "Nau Demo", 24);
        auto same = text.createText(font, "Nau Demo", 24);
        auto changed = text.createText(font, "Nau Demo!", 24);
        auto resized = text.createText(font, "Nau Demo", 30);
        ASSERT_TRUE(first && same && changed && resized);
        EXPECT_EQ(first->get(), same->get());
        EXPECT_NE(first->get(), changed->get());
        EXPECT_NE(first->get(), resized->get());
        auto highDpi = text.rasterize(*first, 2);
        auto sameDpi = text.rasterize(*first, 2);
        ASSERT_TRUE(highDpi && sameDpi);
        EXPECT_EQ(highDpi->get(), sameDpi->get());
        EXPECT_NEAR((*highDpi)->width, (*first)->bitmap->width * 2, 2);
        EXPECT_FALSE(text.rasterize(*first, 0));
        std::weak_ptr<const text::TextBitmap> weak = *highDpi;
        highDpi = text::BitmapHandle{};
        sameDpi = text::BitmapHandle{};
        EXPECT_TRUE(weak.expired());
    }

    TEST_F(ScreenTextTests, SnapshotsReplaceFramesRetainHandlesAndStop)
    {
        auto line = text.createText(font, "Nau Demo", 24);
        ASSERT_TRUE(line);
        auto handle = *line;
        EXPECT_TRUE(text.draw(handle, {24, 24}));
        auto retained = text.snapshot();
        EXPECT_TRUE(text.draw(handle, {30, 30}, {1, 0, 0, 0.5f}));
        EXPECT_EQ(text.snapshot()->size(), 1);
        EXPECT_EQ(retained->front().position[0], 24);
        EXPECT_EQ(text.snapshot()->front().text.get(), handle.get());
        std::weak_ptr<const text::TextLine> weak = handle;
        handle.reset();
        line = text::TextHandle{};
        text.stop();
        text.stop();
        EXPECT_FALSE(weak.expired());
        retained.reset();
        EXPECT_TRUE(weak.expired());
        EXPECT_TRUE(text.snapshot()->empty());
        EXPECT_FALSE(text.publish({}));
        EXPECT_FALSE(text.createText(font, "After Stop", 24));
    }

    TEST_F(ScreenTextTests, FontDerivedRenderReferences)
    {
        std::ifstream input(NAU_TEXT_FIXTURE_DIR "/Abel-Regular.ttf", std::ios::binary | std::ios::ate);
        std::vector<std::byte> changed(size_t(input.tellg()));
        input.seekg(0);
        ASSERT_TRUE(input.read(reinterpret_cast<char*>(changed.data()), changed.size()));
        const auto u16 = [&](size_t p)
        {
            return (unsigned(changed[p]) << 8) | unsigned(changed[p + 1]);
        };
        const auto u32 = [&](size_t p)
        {
            return (uint32_t(u16(p)) << 16) | u16(p + 2);
        };
        size_t metrics = 0, header = 0;
        for (unsigned i = 0; i < u16(4); ++i)
        {
            const size_t entry = 12 + i * 16;
            if (u32(entry) == 0x686D7478)
                metrics = u32(entry + 8);
            if (u32(entry) == 0x68686561)
                header = u32(entry + 8);
        }
        ASSERT_GT(metrics, 0);
        ASSERT_GT(header, 0);
        for (unsigned i = 0; i < u16(header + 34); ++i)
        {
            const size_t p = metrics + i * 4;
            const unsigned advance = u16(p) * 3 / 2;
            changed[p] = std::byte(advance >> 8);
            changed[p + 1] = std::byte(advance & 255);
        }
        std::ofstream changedFile(NAU_TEXT_FIXTURE_DIR "/Abel-Changed.ttf", std::ios::binary);
        ASSERT_TRUE(changedFile.write(reinterpret_cast<const char*>(changed.data()), changed.size()));
        auto filesystem = createCpuAssetFileSystem({
            {"Abel-Changed.ttf", std::move(changed)}
        });
        ASSERT_TRUE(filesystem);
        ASSERT_TRUE(vfs->mount("/changed", *filesystem));
        auto alternate = text.loadFont(*vfs, "/changed/Abel-Changed.ttf");
        ASSERT_TRUE(alternate);
        for (const int variant : {0, 1, 2})
        {
            auto line = text.createText(variant == 2 ? *alternate : font, variant == 1 ? "Glyph AVjgy" : "Nau Demo", variant == 1 ? 32 : 24);
            ASSERT_TRUE(line);
            for (const float scale : {1.0f, 2.0f})
            {
                auto raster = text.rasterize(*line, scale);
                ASSERT_TRUE(raster);
                const auto& bitmap = **raster;
                const std::string path = std::string(NAU_TEXT_FIXTURE_DIR) + "/text-reference-" + std::to_string(int(scale)) + (variant ? "-variant" + std::to_string(variant) : "");
                std::ofstream metadata(path + ".json");
                metadata << "{\"width\":" << bitmap.width << ",\"height\":" << bitmap.height
                         << ",\"left\":" << bitmap.left << ",\"top\":" << bitmap.top << "}";
                std::ofstream image(path + ".raw", std::ios::binary);
                ASSERT_TRUE(image.write(reinterpret_cast<const char*>(bitmap.coverage.data()), bitmap.coverage.size()));
            }
        }
    }

    TEST_F(ScreenTextTests, PresentationSignalSettlesOnce)
    {
        auto ready = text.whenPresented();
        EXPECT_FALSE(ready.isReady());
        text.completePresentation({});
        EXPECT_TRUE(ready.isReady());
        EXPECT_FALSE(ready.isRejected());
        text.stop();
        EXPECT_FALSE(ready.isRejected());
    }
}  // namespace nau::test

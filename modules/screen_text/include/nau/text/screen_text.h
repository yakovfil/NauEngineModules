// Copyright 2026 N-GINN LLC. All rights reserved.
// Use of this source code is governed by a BSD-3 Clause license that can be found in the LICENSE file.
#pragma once

#include <array>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <vector>

#include "nau/io/virtual_file_system.h"
#include "nau/rtti/rtti_impl.h"
#include "nau/service/service.h"
#include "nau/utils/result.h"

namespace nau::text
{
    // Fonts are project-controlled TTF assets, not arbitrary external inputs.
    inline constexpr size_t MaxFontBytes = 16 * 1024 * 1024;
    inline constexpr size_t MaxCodepoints = 4096;
    inline constexpr int MaxBitmapDimension = 4096;
    inline constexpr size_t MaxBitmapBytes = 16 * 1024 * 1024;

    class Font;
    using FontHandle = std::shared_ptr<const Font>;

    struct TextBitmap
    {
        int width = 0, height = 0;
        int left = 0, top = 0;
        float scale = 1;
        std::vector<unsigned char> coverage;
    };
    using BitmapHandle = std::shared_ptr<const TextBitmap>;

    struct TextLine
    {
        FontHandle font;
        std::string utf8;
        float size = 0;
        float advance = 0, ascent = 0, descent = 0;
        BitmapHandle bitmap;
    };
    using TextHandle = std::shared_ptr<const TextLine>;

    struct TextDraw
    {
        TextHandle text;
        std::array<float, 2> position = {0, 0};
        std::array<float, 4> color = {1, 1, 1, 1};
    };
    using TextFrame = std::vector<TextDraw>;

    // CPU-only service. publish() replaces an immutable frame snapshot; graphics
    // backends own uploads and release GPU resources on their owner threads.
    class ScreenText final : public IServiceShutdown
    {
        NAU_RTTI_CLASS(nau::text::ScreenText, IServiceShutdown)
    public:
        ScreenText();
        ~ScreenText() override;
        Result<FontHandle> loadFont(io::IVirtualFileSystem&, std::string_view path);
        Result<TextHandle> createText(FontHandle, std::string_view utf8, float size);
        Result<BitmapHandle> rasterize(TextHandle, float scale = 1);
        Result<> publish(TextFrame);
        Result<> draw(TextHandle, std::array<float, 2> position, std::array<float, 4> color = {1, 1, 1, 1});
        std::shared_ptr<const TextFrame> snapshot() const;
        async::Task<> whenPresented();
        void completePresentation(Result<>);
        void stop();
        async::Task<> shutdownService() override;

    private:
        struct State;
        std::unique_ptr<State> m_state;
    };
}  // namespace nau::text

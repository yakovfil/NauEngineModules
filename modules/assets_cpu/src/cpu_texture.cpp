// Copyright 2026 N-GINN LLC. All rights reserved.
// Use of this source code is governed by a BSD-3 Clause license that can be found in the LICENSE file.
#include "nau/assets/cpu_texture.h"

#include <array>
#include <cstring>

#include "nau/io/stream_utils.h"

namespace nau
{
    namespace
    {
        constexpr size_t HeaderSize = 24;
        uint32_t word(std::span<const std::byte> bytes, size_t offset)
        {
            uint32_t value = 0;
            for (unsigned i = 0; i < 4; ++i)
                value |= uint32_t(std::to_integer<unsigned char>(bytes[offset + i])) << (i * 8);
            return value;
        }
        Result<size_t> payloadSize(std::span<const std::byte> header)
        {
            if (header.size() < HeaderSize || std::memcmp(header.data(), "NTEX", 4) || word(header, 4) != 1 || word(header, 16) != 1)
                return NauMakeError("CPU texture: invalid header, version or RGBA8 format");
            const uint64_t width = word(header, 8), height = word(header, 12);
            if (!width || !height || width > CpuTextureView::MaxDimension || height > CpuTextureView::MaxDimension)
                return NauMakeError("CPU texture: invalid or oversized dimensions");
            const uint64_t length = width * height * 4;
            if (word(header, 20) != length)
                return NauMakeError("CPU texture: inconsistent payload length");
            return size_t(length);
        }
        class TextureContainer final : public IAssetContainer
        {
            NAU_CLASS_(TextureContainer, IAssetContainer)
        public:
            explicit TextureContainer(nau::Ptr<CpuTextureView> view) :
                m_view(std::move(view))
            {
            }
            nau::Ptr<> getAsset(std::string_view path) override
            {
                return path.empty() ? m_view : nullptr;
            }
            std::vector<std::string> getContent() const override
            {
                return {""};
            }

        private:
            nau::Ptr<CpuTextureView> m_view;
        };
    }  // namespace
    Result<nau::Ptr<CpuTextureView>> parseCpuTexture(std::span<const std::byte> bytes)
    {
        auto length = payloadSize(bytes);
        if (!length)
            return length.getError();
        if (bytes.size() != HeaderSize + *length)
            return NauMakeError("CPU texture: truncated or trailing payload");
        auto view = rtti::createInstance<CpuTextureView>();
        view->width = word(bytes, 8);
        view->height = word(bytes, 12);
        view->pixels.assign(bytes.begin() + HeaderSize, bytes.end());
        return view;
    }
    std::vector<std::string_view> CpuTextureContainerLoader::getSupportedAssetKind() const
    {
        return {"ntex"};
    }
    async::Task<IAssetContainer::Ptr> CpuTextureContainerLoader::loadFromStream(io::IStreamReader::Ptr stream, AssetContentInfo info)
    {
        const auto reject = [&](std::string_view reason)
        {
            auto error = NauMakeError("CPU texture {}: {}", info.path.getString(), reason);
            NAU_LOG_ERROR("{}", error->getMessage());
            return error;
        };
        if (!stream)
            co_return reject("missing stream");
        const auto begin = stream->getPosition();
        std::array<std::byte, HeaderSize> header{};
        auto copied = io::copyFromStream(header.data(), header.size(), *stream);
        if (!copied || *copied != header.size())
            co_return reject("truncated header");
        auto length = payloadSize(header);
        if (!length)
            co_return reject(length.getError()->getMessage());
        const auto end = stream->setPosition(io::OffsetOrigin::End, 0);
        if (end < begin || end - begin != HeaderSize + *length || stream->setPosition(io::OffsetOrigin::Begin, begin + HeaderSize) != begin + HeaderSize)
            co_return reject("truncated, trailing or unseekable payload");
        std::vector<std::byte> bytes(HeaderSize + *length);
        std::copy(header.begin(), header.end(), bytes.begin());
        copied = io::copyFromStream(bytes.data() + HeaderSize, *length, *stream);
        if (!copied || *copied != *length)
            co_return reject("truncated pixels");
        std::byte extra{};
        auto trailing = stream->read(&extra, 1);
        if (!trailing || *trailing)
            co_return reject("trailing payload or read failure");
        auto view = parseCpuTexture(bytes);
        if (!view)
            co_return reject(view.getError()->getMessage());
        co_return rtti::createInstance<TextureContainer>(*view);
    }
    std::vector<const rtti::TypeInfo*> CpuTextureViewFactory::getAssetViewTypes() const
    {
        return {&rtti::getTypeInfo<CpuTextureView>()};
    }
    async::Task<IAssetView::Ptr> CpuTextureViewFactory::createAssetView(nau::Ptr<> asset, const rtti::TypeInfo& type)
    {
        auto view = asset ? asset->as<CpuTextureView*>() : nullptr;
        if (!view || type != rtti::getTypeInfo<CpuTextureView>())
            co_return NauMakeError("CPU texture: invalid view request");
        co_return nau::Ptr<CpuTextureView>{view};
    }
}  // namespace nau

// Copyright 2026 N-GINN LLC. All rights reserved.
// Use of this source code is governed by a BSD-3 Clause license that can be found in the LICENSE file.
#pragma once

#include <span>

#include "nau/assets/cpu_mesh.h"

namespace nau
{
    class CpuTextureView final : public IAssetView
    {
        NAU_CLASS_(nau::CpuTextureView, IAssetView)
    public:
        static constexpr uint32_t MaxDimension = 4096;
        uint32_t width = 0, height = 0;
        // Top-row-first sRGB RGB and linear alpha, tightly packed RGBA8.
        std::vector<std::byte> pixels;
    };
    Result<nau::Ptr<CpuTextureView>> parseCpuTexture(std::span<const std::byte>);
    class CpuTextureContainerLoader final : public IAssetContainerLoader
    {
        NAU_INTERFACE(nau::CpuTextureContainerLoader, IAssetContainerLoader)
    public:
        std::vector<std::string_view> getSupportedAssetKind() const override;
        async::Task<IAssetContainer::Ptr> loadFromStream(io::IStreamReader::Ptr, AssetContentInfo) override;
        RuntimeReadonlyDictionary::Ptr getDefaultImportSettings() const override
        {
            return nullptr;
        }
    };
    class CpuTextureViewFactory final : public IAssetViewFactory,
                                        public IRefCounted
    {
        NAU_CLASS_(nau::CpuTextureViewFactory, IAssetViewFactory, IRefCounted)
    public:
        std::vector<const rtti::TypeInfo*> getAssetViewTypes() const override;
        async::Task<IAssetView::Ptr> createAssetView(nau::Ptr<>, const rtti::TypeInfo&) override;
    };
}  // namespace nau

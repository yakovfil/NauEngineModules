// Copyright 2026 N-GINN LLC. All rights reserved.
// Use of this source code is governed by a BSD-3 Clause license that can be found in the LICENSE file.
#include <cstdio>
#include <limits>

#include "nau/io/memory_stream.h"
#include "nau/render/scene_backend.h"

int main()
{
    using namespace nau;
    unsigned checks = 0;
    const auto check = [&](bool okay, const char* reason)
    {
        ++checks;
        if (!okay)
            std::fprintf(stderr, "FAIL %s\n", reason);
        return okay;
    };
    std::vector<std::byte> payload(32);
    const auto set = [&](unsigned offset, uint32_t value)
    {
        for (unsigned i = 0; i < 4; ++i)
            payload[offset + i] = std::byte((value >> (8 * i)) & 255);
    };
    payload[0] = std::byte{'N'};
    payload[1] = std::byte{'T'};
    payload[2] = std::byte{'E'};
    payload[3] = std::byte{'X'};
    set(4, 1);
    set(8, 1);
    set(12, 2);
    set(16, 1);
    set(20, 8);
    for (unsigned i = 0; i < 8; ++i)
        payload[24 + i] = std::byte(i * 20);
    auto image = parseCpuTexture(payload);
    if (!check(image && (*image)->width == 1 && (*image)->height == 2 && (*image)->pixels[7] == std::byte{140}, "canonical texture/orientation"))
        return 1;
    const auto good = payload;
    for (auto [offset, value] : {
             std::pair{ 4u,          2u},
             { 8u,          0u},
             { 8u,       4097u},
             { 8u, 0xffffffffu},
             {12u, 0xffffffffu},
             {16u,          2u},
             {20u,          7u},
             {20u, 0xffffffffu}
    })
    {
        payload = good;
        set(offset, value);
        if (!check(!parseCpuTexture(payload), "invalid texture header"))
            return 1;
    }
    for (unsigned length : {0u, 4u, 23u, 24u, 31u, 33u})
    {
        payload = good;
        payload.resize(length);
        if (!check(!parseCpuTexture(payload), "exact payload length"))
            return 1;
    }
    auto stream = io::createReadonlyMemoryStream(good);
    CpuTextureContainerLoader loader;
    AssetContentInfo info{};
    auto task = loader.loadFromStream(stream, info);
    if (!check(task.isReady() && !task.isRejected(), "texture stream loader"))
        return 1;
    auto container = task.asResult();
    if (!check(container && *container && (*container)->getAsset("") && !(*container)->getAsset("invalid"), "texture subresource"))
        return 1;
    auto asset = (*container)->getAsset("");
    CpuTextureViewFactory factory;
    auto view = factory.createAssetView(asset, rtti::getTypeInfo<CpuTextureView>());
    if (!check(view.isReady() && !view.isRejected(), "typed texture factory"))
        return 1;
    render::MatrixValues vp{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, -1, -1, 0, 0, -0.2f, 0};
    render::BillboardDraw draw;
    draw.position = {1, 2, -3};
    draw.size = .12f;
    auto center = render::billboardCenter(vp, draw);
    if (!check(center && render::billboardInDepth(*center), "projected marker"))
        return 1;
    for (float distance : {1.f, 3.f, 20.f})
    {
        draw.position[2] = -distance;
        auto clip = render::billboardCenter(vp, draw);
        const float pixelWidth = ((clip->at(0) + draw.size * clip->at(3)) / clip->at(3) - (clip->at(0) - draw.size * clip->at(3)) / clip->at(3)) * 900 / 2;
        if (!check(std::abs(pixelWidth - 108) < .001f, "distance-independent width"))
            return 1;
    }
    draw.position[2] = 1;
    if (!check(!render::billboardInDepth(*render::billboardCenter(vp, draw)), "behind camera"))
        return 1;
    for (float size : {0.f, -1.f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()})
    {
        draw.size = size;
        if (!check(!render::billboardCenter(vp, draw), "invalid size"))
            return 1;
    }
    draw.size = .1f;
    draw.position[0] = std::numeric_limits<float>::infinity();
    if (!check(!render::billboardCenter(vp, draw), "invalid position"))
        return 1;
    std::printf("PASS %u portable billboard contract checks\n", checks);
    return 0;
}

// Copyright 2026 N-GINN LLC. All rights reserved.
// Use of this source code is governed by a BSD-3 Clause license that can be found in the LICENSE file.
#pragma once
#include <algorithm>

#include "nau/render/scene_backend.h"

namespace nau::render
{
    struct VectorVertex
    {
        std::array<float, 4> position;
        std::array<float, 3> color;
    };
    struct VectorTriangles
    {
        std::vector<VectorVertex> background, depth, world, overlay;
        bool blackBackground = false;
    };
    inline Result<VectorTriangles> tessellateVectors(const SceneFrame& frame)
    {
        VectorTriangles result;
        size_t worldCount = 0, overlayCount = 0;
        for (const auto& source : frame.vectors)
        {
            worldCount += source.world.size();
            overlayCount += source.overlay.size() + source.background.size();
            if (worldCount > VectorPresentation::WorldLimit || overlayCount > VectorPresentation::OverlayLimit)
                return NauMakeError("Vector capacity exceeded: {}", source.owner);
            result.blackBackground |= source.blackBackground;
            for (unsigned layer = 0; layer < 3; ++layer)
            {
                const auto& lines = layer == 0 ? source.background : layer == 1 ? source.world
                                                                                : source.overlay;
                for (const auto& line : lines)
                {
                    for (float value : line.a)
                        if (!std::isfinite(value))
                            return NauMakeError("Invalid vector coordinate: {}", source.owner);
                    for (float value : line.b)
                        if (!std::isfinite(value))
                            return NauMakeError("Invalid vector coordinate: {}", source.owner);
                    for (float value : line.color)
                        if (!std::isfinite(value) || value < 0 || value > 1)
                            return NauMakeError("Invalid vector color: {}", source.owner);
                    if (!std::isfinite(line.width) || line.width <= 0 || line.width > 16)
                        return NauMakeError("Invalid vector width: {}", source.owner);
                    if (!frame.drawable[0] || !frame.drawable[1])
                        continue;
                    std::array<float, 4> a{}, b{};
                    float width = line.width;
                    if (layer == 1)
                    {
                        for (unsigned row = 0; row < 4; ++row)
                        {
                            a[row] = b[row] = frame.viewProjection[12 + row];
                            for (unsigned axis = 0; axis < 3; ++axis)
                            {
                                a[row] += frame.viewProjection[axis * 4 + row] * line.a[axis];
                                b[row] += frame.viewProjection[axis * 4 + row] * line.b[axis];
                            }
                        }
                    }
                    else
                    {
                        const float scale = std::min(frame.drawable[0] / 800.f, frame.drawable[1] / 600.f);
                        a = {(line.a[0] - 400) * scale * 2 / frame.drawable[0], (300 - line.a[1]) * scale * 2 / frame.drawable[1], 0, 1};
                        b = {(line.b[0] - 400) * scale * 2 / frame.drawable[0], (300 - line.b[1]) * scale * 2 / frame.drawable[1], 0, 1};
                        width = std::max(1.f, width * scale);
                    }
                    for (float value : a)
                        if (!std::isfinite(value))
                            return NauMakeError("Invalid vector projection: {}", source.owner);
                    for (float value : b)
                        if (!std::isfinite(value))
                            return NauMakeError("Invalid vector projection: {}", source.owner);
                    // Homogeneous six-plane clipping happens before perspective divide.
                    float enter = 0, leave = 1;
                    bool visible = true;
                    for (unsigned plane = 0; plane < 6; ++plane)
                    {
                        const unsigned axis = plane / 2;
                        const float sign = plane % 2 ? -1.f : 1.f;
                        const float da = a[3] + sign * a[axis], db = b[3] + sign * b[axis];
                        if (!std::isfinite(da) || !std::isfinite(db) || ((da < 0 || db < 0) && !std::isfinite(da - db)))
                            return NauMakeError("Vector clipping is not representable: {}", source.owner);
                        if (da < 0 && db < 0)
                        {
                            visible = false;
                            break;
                        }
                        if (da < 0)
                            enter = std::max(enter, da / (da - db));
                        if (db < 0)
                            leave = std::min(leave, da / (da - db));
                    }
                    if (!visible || enter > leave)
                        continue;
                    const auto original = a;
                    for (unsigned i = 0; i < 4; ++i)
                    {
                        a[i] = original[i] + (b[i] - original[i]) * enter;
                        b[i] = original[i] + (b[i] - original[i]) * leave;
                        if (!std::isfinite(a[i]) || !std::isfinite(b[i]))
                            return NauMakeError("Vector clipping overflow: {}", source.owner);
                    }
                    if (a[3] <= 0 || b[3] <= 0)
                        continue;
                    for (unsigned i = 0; i < 3; ++i)
                    {
                        a[i] /= a[3];
                        b[i] /= b[3];
                    }
                    a[3] = b[3] = 1;
                    const float dx = (b[0] - a[0]) * frame.drawable[0], dy = (b[1] - a[1]) * frame.drawable[1];
                    const float length = std::hypot(dx, dy);
                    if (length < 1e-5f)
                        continue;
                    const float ox = -dy / length * width / frame.drawable[0], oy = dx / length * width / frame.drawable[1];
                    auto& output = layer == 0 ? result.background : layer == 2   ? result.overlay
                                                                : line.depthTest ? result.depth
                                                                                 : result.world;
                    auto vertex = [&](const auto& point, float side)
                    {
                        auto position = point;
                        position[0] += ox * side;
                        position[1] += oy * side;
                        return VectorVertex{position, line.color};
                    };
                    for (auto item : {vertex(a, -1), vertex(b, -1), vertex(a, 1), vertex(a, 1), vertex(b, -1), vertex(b, 1)})
                        output.push_back(item);
                }
            }
        }
        return result;
    }
}  // namespace nau::render

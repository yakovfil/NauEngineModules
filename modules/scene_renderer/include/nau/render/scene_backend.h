// Copyright 2026 N-GINN LLC. All rights reserved.
// Use of this source code is governed by a BSD-3 Clause license that can be found in the LICENSE file.
#pragma once

#include <array>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include "nau/assets/cpu_mesh.h"
#include "nau/assets/cpu_texture.h"

namespace nau::render
{
    using MatrixValues = std::array<float, 16>;
    struct VectorLine
    {
        std::array<float, 3> a{}, b{};
        std::array<float, 3> color{0, 1, 0};
        float width = 2;
        bool depthTest = false;
    };
    struct VectorPresentation
    {
        static constexpr size_t WorldLimit = 16384, OverlayLimit = 8192;
        std::string owner;
        std::vector<VectorLine> background, world, overlay;
        bool blackBackground = false;
    };
    struct ISceneVectorSource
    {
        NAU_TYPEID(nau::render::ISceneVectorSource)
        virtual ~ISceneVectorSource() = default;
        virtual Result<VectorPresentation> vectorPresentation() const = 0;
    };
    struct SceneDraw
    {
        std::string component;
        std::string geometry;
        nau::Ptr<CpuMeshView> mesh;
        MatrixValues model;
        MatrixValues normals;
        std::array<float, 3> color;
    };
    struct BillboardDraw
    {
        std::string component;
        std::string texture;
        nau::Ptr<CpuTextureView> image;
        std::array<float, 3> position;
        float size = 0.1f;
    };
    // Clip coordinates remain OpenGL-depth; native conversion happens at submission.
    inline Result<std::array<float, 4>> billboardCenter(const MatrixValues& vp, const BillboardDraw& draw)
    {
        if (!std::isfinite(draw.size) || draw.size <= 0)
            return NauMakeError("Invalid billboard size {}", draw.component);
        std::array<float, 4> clip{};
        for (unsigned row = 0; row < 4; ++row)
        {
            clip[row] = vp[12 + row];
            for (unsigned axis = 0; axis < 3; ++axis)
                clip[row] += vp[axis * 4 + row] * draw.position[axis];
            if (!std::isfinite(clip[row]))
                return NauMakeError("Non-finite billboard position/projection {}", draw.component);
        }
        return clip;
    }
    inline bool billboardInDepth(const std::array<float, 4>& clip)
    {
        return clip[3] > 0 && clip[2] >= -clip[3] && clip[2] <= clip[3];
    }
    struct SceneFrame
    {
        std::string camera;
        std::array<unsigned, 2> drawable{};
        // Column-major RH view-projection with OpenGL [-1, 1] clip depth.
        // Backends convert depth convention without changing camera selection.
        MatrixValues viewProjection;
        std::array<float, 3> lightDirection;
        std::array<float, 3> ambient;
        std::array<float, 3> directional;
        std::vector<SceneDraw> draws;
        std::vector<BillboardDraw> billboards;
        std::vector<VectorPresentation> vectors;
    };
    struct ISceneFrameObserver
    {
        NAU_TYPEID(nau::render::ISceneFrameObserver)
        virtual ~ISceneFrameObserver() = default;
        // Owner-thread observation of a successfully submitted immutable frame.
        // Observers must not retain borrowed references or mutate scene state here.
        virtual void frameSubmitted(const SceneFrame&) = 0;
    };
    struct SceneDrawable
    {
        unsigned width = 0;
        unsigned height = 0;
    };
    class SceneBackend
    {
    public:
        enum class TestFailure
        {
            None,
            VectorShader,
            VectorUpload
        };
        virtual ~SceneBackend() = default;
        // Acceptance-only injection into checked resource creation/upload paths.
        virtual void injectTestFailure(TestFailure)
        {
        }
        virtual Result<> initialize(void* window) = 0;
        virtual Result<SceneDrawable> poll() = 0;
        virtual Result<> submit(const SceneFrame&) = 0;
        virtual Result<> stop() = 0;
    };
    struct ISceneBackendFactory
    {
        NAU_TYPEID(nau::render::ISceneBackendFactory)
        virtual ~ISceneBackendFactory() = default;
        virtual std::unique_ptr<SceneBackend> createBackend() = 0;
    };
    std::unique_ptr<SceneBackend> createSceneBackend();
}  // namespace nau::render

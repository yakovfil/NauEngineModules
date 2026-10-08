// Copyright 2026 N-GINN LLC. All rights reserved.
// Use of this source code is governed by a BSD-3 Clause license that can be found in the LICENSE file.
#pragma once

#include <array>
#include <memory>
#include <string>
#include <vector>

#include "nau/assets/cpu_mesh.h"

namespace nau::render
{
    using MatrixValues = std::array<float, 16>;
    struct SceneDraw
    {
        std::string component;
        std::string geometry;
        nau::Ptr<CpuMeshView> mesh;
        MatrixValues model;
        MatrixValues normals;
        std::array<float, 3> color;
    };
    struct SceneFrame
    {
        std::string camera;
        // Column-major RH view-projection with OpenGL [-1, 1] clip depth.
        // Backends convert depth convention without changing camera selection.
        MatrixValues viewProjection;
        std::array<float, 3> lightDirection;
        std::array<float, 3> ambient;
        std::array<float, 3> directional;
        std::vector<SceneDraw> draws;
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
        virtual ~SceneBackend() = default;
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

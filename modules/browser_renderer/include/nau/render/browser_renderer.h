// Copyright 2026 N-GINN LLC. All rights reserved.
// Use of this source code is governed by a BSD-3 Clause license that can be found in the LICENSE file.
#pragma once

#include <memory>
#include <string_view>
#include <utility>

#include "nau/assets/cpu_mesh.h"
#include "nau/scene/components/camera_component.h"

namespace nau
{
    namespace render
    {
        struct SceneFrame;
    }
    // All methods and destruction belong to the application worker. The DOM
    // canvas belongs to the page and must already be transferred at startup.
    class BrowserRenderer
    {
    public:
        BrowserRenderer();
        ~BrowserRenderer();
        BrowserRenderer(const BrowserRenderer&) = delete;
        BrowserRenderer& operator=(const BrowserRenderer&) = delete;
        Result<> initialize(const CpuMeshView&, std::string_view vertexShader, std::string_view fragmentShader);
        Result<> initializeBatch(std::string_view vertexShader, std::string_view fragmentShader, std::string_view billboardVertex = {}, std::string_view billboardFragment = {});
        Result<> drawBatch(const render::SceneFrame&);
        std::pair<unsigned, unsigned> getDrawableSize() const;
        Result<> poll();
        Result<> draw(const scene::SceneObject& mesh, const scene::SceneObject& camera);
        Result<> stop();

    private:
        Result<> initializeProgram(const CpuMeshView*, std::string_view, std::string_view);
        struct State;
        std::unique_ptr<State> m_state;
    };
}  // namespace nau

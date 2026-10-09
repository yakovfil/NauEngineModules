// Copyright 2026 N-GINN LLC. All rights reserved.
// Use of this source code is governed by a BSD-3 Clause license that can be found in the LICENSE file.
#include "nau/render/browser_renderer.h"
#include "nau/render/scene_backend.h"
#include "nau_scene_shaders.h"

namespace nau::render
{
    namespace
    {
        class BrowserBackend final : public SceneBackend
        {
        public:
            Result<> initialize(void*) override
            {
                return m_renderer.initializeBatch(shaders::VertexSource, shaders::FragmentSource, shaders::BillboardVertex, shaders::BillboardFragment);
            }
            Result<SceneDrawable> poll() override
            {
                NauCheckResult(m_renderer.poll());
                const auto [width, height] = m_renderer.getDrawableSize();
                return SceneDrawable{width, height};
            }
            Result<> submit(const SceneFrame& frame) override
            {
                return m_renderer.drawBatch(frame);
            }
            Result<> stop() override
            {
                return m_renderer.stop();
            }

        private:
            BrowserRenderer m_renderer;
        };
    }  // namespace
    std::unique_ptr<SceneBackend> createSceneBackend()
    {
        return std::make_unique<BrowserBackend>();
    }
}  // namespace nau::render

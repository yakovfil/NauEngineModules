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
            void injectTestFailure(TestFailure value) override
            {
                m_testFailure = value;
            }
            Result<> initialize(void*) override
            {
                return m_renderer.initializeBatch(shaders::VertexSource, shaders::FragmentSource, shaders::BillboardVertex, shaders::BillboardFragment, m_testFailure == TestFailure::VectorShader);
            }
            Result<SceneDrawable> poll() override
            {
                NauCheckResult(m_renderer.poll());
                const auto [width, height] = m_renderer.getDrawableSize();
                return SceneDrawable{width, height};
            }
            Result<> submit(const SceneFrame& frame) override
            {
                return m_renderer.drawBatch(frame, m_testFailure == TestFailure::VectorUpload);
            }
            Result<> stop() override
            {
                return m_renderer.stop();
            }

        private:
            BrowserRenderer m_renderer;
            TestFailure m_testFailure = TestFailure::None;
        };
    }  // namespace
    std::unique_ptr<SceneBackend> createSceneBackend()
    {
        return std::make_unique<BrowserBackend>();
    }
}  // namespace nau::render

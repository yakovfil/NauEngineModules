// Copyright 2026 N-GINN LLC. All rights reserved.
// Use of this source code is governed by a BSD-3 Clause license that can be found in the LICENSE file.
#include "nau/app/application.h"
#include "nau/app/core_window_manager.h"
#include "nau/app/run_application.h"
#include "nau/assets/asset_db.h"
#include "nau/assets/asset_ref.h"
#include "nau/io/asset_pack_file_system.h"
#include "nau/io/virtual_file_system.h"
#include "nau/platform/windows/app/windows_window.h"
#include "nau/platform/windows/windows_headers.h"
#include "nau/render/scene_backend.h"
#include "nau/service/service.h"
#include "nau/service/service_provider.h"

namespace
{
    struct BackendOwner final : nau::IServiceShutdown
    {
        NAU_RTTI_CLASS(BackendOwner, nau::IServiceShutdown)
        std::unique_ptr<nau::render::SceneBackend> backend = nau::render::createSceneBackend();
        nau::Error::Ptr failure;
        nau::async::Task<> shutdownService() override
        {
            auto stopped = backend->stop();
            if (!stopped && !failure)
                failure = stopped.getError();
            if (failure)
                co_await nau::async::Task<>::makeRejected(failure);
            NAU_LOG_INFO("Native scene backend checkpoint cleanup complete");
        }
    };
    class BackendCheckpoint final : public nau::ApplicationDelegate
    {
    public:
        explicit BackendCheckpoint(std::string archive) :
            m_archive(std::move(archive))
        {
        }

    private:
        nau::Result<> configureApplication() override
        {
            return {};
        }
        std::string getModulesListString() const override
        {
            return {};
        }
        nau::Result<> initializeServices() override
        {
            auto owner = std::make_unique<BackendOwner>();
            m_owner = owner.get();
            nau::getServiceProvider().addService(std::move(owner));
            return {};
        }
        void onApplicationInitialized() override
        {
            auto& window = nau::getServiceProvider().get<nau::ICoreWindowManager>().getActiveWindow();
            window.setName("Nau scene backend checkpoint");
            window.setVisible(true);
        }
        nau::async::Task<> startupApplication() override
        {
            using namespace nau;
            auto& window = getServiceProvider().get<ICoreWindowManager>().getActiveWindow().as<IWindowsWindow&>();
            co_await m_owner->backend->initialize(window.getWindowHandle());
            auto filesystem = io::openAssetPackFileSystem(m_archive.c_str());
            if (!filesystem)
                co_await async::Task<>::makeRejected(filesystem.getError());
            co_await getServiceProvider().get<io::IVirtualFileSystem>().mount("/", *filesystem);
            getServiceProvider().get<IAssetDB>().addAssetDB("/assets_db/database.db");
            const render::MatrixValues identity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
            const char* paths[] = {"uid:22222222-3333-4444-5555-666666666672+[mesh/0]",
                                   "uid:22222222-3333-4444-5555-666666666674+[mesh/0]"};
            for (unsigned i = 0; i < 3; ++i)
            {
                StaticMeshAssetRef reference{paths[i % 2]};
                auto mesh = co_await reference.getAssetViewTyped<CpuMeshView>();
                if (!mesh)
                    co_await async::Task<>::makeRejected(NauMakeError("Backend checkpoint mesh unavailable"));
                render::SceneDraw draw;
                draw.geometry = paths[i % 2];
                draw.mesh = mesh;
                draw.model = draw.normals = identity;
                draw.model[12] = i == 0 ? -1.f : i == 1 ? 1.f
                                                        : 0.f;
                draw.model[13] = i == 2 ? -1.7f : 0.f;
                draw.model[14] = i == 2 ? -6.f : -5.f;
                draw.color = i % 2 ? std::array<float, 3>{.9f, .25f, .025f} : std::array<float, 3>{.03f, .25f, .8f};
                m_frame.draws.push_back(std::move(draw));
            }
            m_frame.lightDirection = {0, 0, 1};
            m_frame.ambient = {.2f, .2f, .2f};
            m_frame.directional = {.7f, .7f, .7f};
            co_await drawFrame();
            NAU_LOG_INFO("Native scene backend checkpoint running");
        }
        nau::Result<> drawFrame()
        {
            auto drawable = m_owner->backend->poll();
            if (!drawable)
                return drawable.getError();
            if (!drawable->width || !drawable->height)
                return {};
            float f = 1.7320508f, nearPlane = .1f, farPlane = 100.f;
            float aspect = float(drawable->width) / drawable->height;
            m_frame.viewProjection = {f / aspect, 0, 0, 0, 0, f, 0, 0, 0, 0, (farPlane + nearPlane) / (nearPlane - farPlane), -1,
                                      0, 0, 2 * farPlane * nearPlane / (nearPlane - farPlane), 0};
            return m_owner->backend->submit(m_frame);
        }
        void onApplicationPostUpdate(std::chrono::milliseconds) override
        {
            auto rendered = drawFrame();
            if (!rendered)
            {
                m_owner->failure = rendered.getError();
                nau::getApplication().stop();
            }
        }
        std::string m_archive;
        BackendOwner* m_owner = nullptr;
        nau::render::SceneFrame m_frame;
    };
}  // namespace
int main(int argc, char** argv)
{
    if (argc != 2)
        return 1;
    return nau::runApplication(std::make_unique<BackendCheckpoint>(argv[1]));
}

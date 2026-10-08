// Copyright 2026 N-GINN LLC. All rights reserved.
// Use of this source code is governed by a BSD-3 Clause license that can be found in the LICENSE file.
#include "nau/app/application.h"
#include "nau/app/run_application.h"
#include "nau/assets/asset_db.h"
#include "nau/assets/asset_ref.h"
#include "nau/assets/cpu_mesh.h"
#include "nau/io/asset_pack_file_system.h"
#include "nau/io/virtual_file_system.h"
#include "nau/service/service_provider.h"

namespace
{
    class PassivePolicy final : public nau::IAssetReferenceLoadingPolicy, public nau::IRttiObject
    {
        NAU_RTTI_CLASS(PassivePolicy, nau::IAssetReferenceLoadingPolicy, nau::IRttiObject)
        bool preloadAssetReferences() const override { return false; }
    };
    class MeshCheckpoint final : public nau::ApplicationDelegate
    {
    public:
        explicit MeshCheckpoint(std::string archive) :
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
            nau::getServiceProvider().addService(std::make_unique<PassivePolicy>());
            return {};
        }
        void onApplicationInitialized() override
        {
        }
        nau::async::Task<> startupApplication() override
        {
            using namespace nau;
            auto filesystem = io::openAssetPackFileSystem(m_archive.c_str());
            if (!filesystem)
                co_await async::Task<>::makeRejected(filesystem.getError());
            co_await getServiceProvider().get<io::IVirtualFileSystem>().mount("/", *filesystem);
            getServiceProvider().get<IAssetDB>().addAssetDB("/assets_db/database.db");
            for (const char* path : {"uid:22222222-3333-4444-5555-666666666672+[mesh/0]",
                                     "uid:22222222-3333-4444-5555-666666666674+[mesh/0]"})
            {
                StaticMeshAssetRef reference{path};
                auto mesh = co_await reference.getAssetViewTyped<CpuMeshView>();
                if (!mesh || mesh->positions.empty() || mesh->normals.size() != mesh->positions.size() || mesh->indices.empty())
                    co_await async::Task<>::makeRejected(NauMakeError("CPU checkpoint could not load {}", path));
                NAU_LOG_INFO("CPU checkpoint loaded {}: {} vertices, {} indices", path, mesh->positions.size(), mesh->indices.size());
            }
            NAU_LOG_INFO("CPU mesh checkpoint passed");
            getApplication().stop();
        }
        std::string m_archive;
    };
}  // namespace
int main(int argc, char** argv)
{
    if (argc != 2)
        return 1;
    return nau::runApplication(std::make_unique<MeshCheckpoint>(argv[1]));
}

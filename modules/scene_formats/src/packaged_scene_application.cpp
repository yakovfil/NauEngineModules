// Copyright 2026 N-GINN LLC. All rights reserved.
// Use of this source code is governed by a BSD-3 Clause license that can be found in the LICENSE file.

#include "nau/scene/packaged_scene_application.h"

#include <mutex>
#include <unordered_set>

#include "nau/app/application.h"
#include "nau/app/core_window_manager.h"
#include "nau/app/global_properties.h"
#include "nau/assets/asset_db.h"
#include "nau/assets/asset_ref.h"
#include "nau/io/asset_pack_file_system.h"
#include "nau/io/special_paths.h"
#include "nau/io/virtual_file_system.h"
#include "nau/rtti/rtti_impl.h"
#include "nau/scene/checked_scene_startup.h"
#include "nau/scene/packaged_scene_presenter.h"
#include "nau/scene/scene_manager.h"
#include "nau/serialization/json.h"
#include "nau/service/service_provider.h"

namespace nau::scene
{
    namespace
    {
        struct PackageDatabase
        {
            std::vector<AssetMetaInfoBase> content;
            NAU_CLASS_FIELDS(CLASS_FIELD(content))
        };

        struct PackageInputs
        {
            std::string profile;
            std::vector<std::string> requiredFiles;
            NAU_CLASS_FIELDS(CLASS_FIELD(profile), CLASS_FIELD(requiredFiles))
        };

        Result<RuntimeValue::Ptr> readJson(const std::string& path)
        {
            auto file = getServiceProvider().get<io::IVirtualFileSystem>().openFile(
                path, io::AccessMode::Read, io::OpenFileMode::OpenExisting);
            if (!file || file->getSize() == 0 || file->getSize() > 16 * 1024 * 1024)
                return NauMakeError("Missing, empty or oversized packaged JSON {}", path);
            io::IStreamReader::Ptr stream = file->createStream();
            if (!stream)
                return NauMakeError("Unreadable packaged JSON {}", path);
            auto parsed = serialization::jsonParse(*stream);
            if (!parsed || !*parsed || !(*parsed)->is<RuntimeReadonlyDictionary>())
                return NauMakeError("Malformed packaged JSON {}", path);
            return *parsed;
        }

        class PassiveReferences final : public IAssetReferenceLoadingPolicy,
                                        public IRttiObject
        {
            NAU_RTTI_CLASS(PassiveReferences, IAssetReferenceLoadingPolicy, IRttiObject)
            bool preloadAssetReferences() const override
            {
                return false;
            }
        };

        class ActivationDiagnostics final : public ISceneActivationDiagnostics,
                                            public IRttiObject
        {
            NAU_RTTI_CLASS(ActivationDiagnostics, ISceneActivationDiagnostics, IRttiObject)
        public:
            void sceneActivationFailed(Uid world, Error::Ptr error) override
            {
                std::lock_guard lock(m_mutex);
                std::string message = m_error ? m_error->getDiagMessage() + "\n" : "";
                message += "Scene activation in world " + toString(world) + ": " + error->getDiagMessage();
                m_error = NauMakeError(message);
            }
            Error::Ptr failure() const
            {
                std::lock_guard lock(m_mutex);
                return m_error;
            }

        private:
            mutable std::mutex m_mutex;
            Error::Ptr m_error;
        };

        class PackagedSceneDelegate final : public ApplicationDelegate
        {
        public:
            explicit PackagedSceneDelegate(PackagedSceneOptions options) :
                m_options(std::move(options))
            {
            }

        private:
            Result<> configureApplication() override
            {
                if (m_options.supportedComponents.empty())
                    return NauMakeError("Packaged scene requires explicit supported component types");
                if (m_options.runtimeProfile != "non-rendering-scene" && m_options.runtimeProfile != "basic-rendering-scene")
                    return NauMakeError("Unsupported packaged scene runtime profile {}", m_options.runtimeProfile);
                if (m_options.runtimeProfile == "basic-rendering-scene" && m_options.renderingConfiguration.empty())
                    return NauMakeError("Rendering profile requires an explicit rendering policy");
                return ResultSuccess;
            }

            std::string getModulesListString() const override
            {
                return {};
            }

            Result<> initializeServices() override
            {
                getServiceProvider().addService(std::make_unique<PassiveReferences>());
                getServiceProvider().addService(std::make_unique<ActivationDiagnostics>());
                return ResultSuccess;
            }

            void onApplicationInitialized() override
            {
                if (auto* manager = getServiceProvider().find<ICoreWindowManager>())
                {
                    manager->getActiveWindow().setVisible(true);
                }
            }

            async::Task<> startupApplication() override
            {
                NAU_LOG_INFO("Packaged scene startup");
                co_await getApplication().getExecutor();
                if (getApplication().isClosing())
                    co_return;
                const auto archive = m_options.archivePath.empty() ? (io::getKnownFolderPath(io::KnownFolder::ExecutableLocation) / "content.nau_pack").string() : m_options.archivePath;
                auto filesystem = io::openAssetPackFileSystem(archive.c_str());
                if (!filesystem)
                    co_await async::Task<>::makeRejected(filesystem.getError());
                auto& vfs = getServiceProvider().get<io::IVirtualFileSystem>();
                co_await vfs.mount("/", *filesystem);

                auto inputsJson = readJson("/config/assets.json");
                if (!inputsJson)
                    co_await async::Task<>::makeRejected(inputsJson.getError());
                PackageInputs inputs;
                co_await runtimeValueApply(inputs, *inputsJson);
                if (inputs.profile != m_options.runtimeProfile || inputs.requiredFiles.empty())
                    co_await async::Task<>::makeRejected(NauMakeError("Invalid required asset manifest /config/assets.json"));
                for (const auto& path : inputs.requiredFiles)
                {
                    if (path.empty() || path.front() == '/' || path.find("..") != std::string::npos || path.find('\\') != std::string::npos)
                        co_await async::Task<>::makeRejected(NauMakeError("Invalid required packaged entry {}", path));
                    auto file = vfs.openFile("/" + path, io::AccessMode::Read, io::OpenFileMode::OpenExisting);
                    io::IStreamReader::Ptr stream = file ? file->createStream() : nullptr;
                    std::byte byte;
                    auto read = stream ? stream->read(&byte, 1) : Result<size_t>{NauMakeError("Unreadable entry")};
                    if (!file || file->getSize() == 0 || !read || *read != 1)
                        co_await async::Task<>::makeRejected(NauMakeError("Missing or unreadable required packaged entry {}", path));
                }

                auto& properties = getServiceProvider().get<GlobalProperties>();
                for (const auto& path : {std::string("/config/app.json"), m_options.configurationPath})
                {
                    auto json = readJson(path);
                    if (!json)
                        co_await async::Task<>::makeRejected(json.getError());
                    co_await properties.mergeWithValue(**json);
                }
                const auto startup = properties.getValue<std::string>("scene/startupScene");
                if (!startup || startup->empty() || !AssetPath::isValid(*startup))
                    co_await async::Task<>::makeRejected(NauMakeError("Missing or invalid required scene/startupScene configuration"));

                auto databaseJson = readJson(m_options.databasePath);
                if (!databaseJson)
                    co_await async::Task<>::makeRejected(databaseJson.getError());
                PackageDatabase database;
                co_await runtimeValueApply(database, *databaseJson);
                if (database.content.empty())
                    co_await async::Task<>::makeRejected(NauMakeError("Required packaged asset database is empty"));
                std::unordered_set<Uid> identities;
                for (const auto& entry : database.content)
                {
                    const auto relative = io::FsPath{entry.dbPath};
                    if (entry.uid == NullUid || !identities.insert(entry.uid).second || entry.dbPath.empty() ||
                        relative.isAbsolute() || entry.dbPath.find("..") != std::string::npos ||
                        entry.dbPath.find('\\') != std::string::npos)
                        co_await async::Task<>::makeRejected(NauMakeError("Invalid packaged asset mapping {}", entry.dbPath));
                    const auto location = io::FsPath{m_options.databasePath}.getParentPath() / relative;
                    auto file = vfs.openFile(location, io::AccessMode::Read, io::OpenFileMode::OpenExisting);
                    if (!file || file->getSize() == 0 || !file->createStream())
                        co_await async::Task<>::makeRejected(NauMakeError("Missing or unreadable mapped asset {}", entry.dbPath));
                }
                getServiceProvider().get<IAssetDB>().addAssetDB(m_options.databasePath);

                for (auto* preparation : getServiceProvider().getAll<IPackagedScenePreparation>())
                {
                    if (getApplication().isClosing())
                        co_return;
                    co_await preparation->preparePackagedScene();
                }

                if (getApplication().isClosing())
                    co_return;
                auto scene = co_await loadStartupSceneChecked(*startup, m_options.supportedComponents);
                if (getApplication().isClosing())
                    co_return;
                auto activated = co_await getServiceProvider().get<ISceneManager>().activateScene(std::move(scene));
                if (auto error = getServiceProvider().get<ActivationDiagnostics>().failure())
                    co_await async::Task<>::makeRejected(error);
                if (!activated)
                    co_await async::Task<>::makeRejected(NauMakeError("Required startup scene activation failed"));
                if (m_options.runtimeProfile == "basic-rendering-scene" && !getApplication().isClosing())
                {
                    auto* presenter = getServiceProvider().find<IPackagedScenePresenter>();
                    if (!presenter)
                        co_await async::Task<>::makeRejected(NauMakeError("Required SceneRenderer service is unavailable"));
                    co_await presenter->preparePresentation(m_options.renderingConfiguration);
                }
                if (getApplication().isClosing())
                    co_return;
                for (auto* preparation : getServiceProvider().getAll<IPackagedScenePreparation>())
                    preparation->packagedSceneReady();
                NAU_LOG_INFO("Packaged scene Running");
            }

            void onApplicationPostUpdate(std::chrono::milliseconds) override
            {
                if (m_options.runtimeProfile != "basic-rendering-scene" || getApplication().isClosing())
                    return;
                if (auto* presenter = getServiceProvider().find<IPackagedScenePresenter>())
                {
                    auto result = presenter->presentFrame();
                    if (!result)
                        getApplication().stop();
                }
            }

            PackagedSceneOptions m_options;
        };
    }  // namespace

    ApplicationDelegate::Ptr createPackagedSceneApplication(PackagedSceneOptions options)
    {
        return std::make_unique<PackagedSceneDelegate>(std::move(options));
    }
}  // namespace nau::scene

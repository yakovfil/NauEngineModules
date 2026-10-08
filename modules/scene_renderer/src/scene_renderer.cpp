// Copyright 2026 N-GINN LLC. All rights reserved.
// Use of this source code is governed by a BSD-3 Clause license that can be found in the LICENSE file.
#include <algorithm>
#include <cmath>
#include <map>
#include <optional>
#include <set>

#include "nau/app/application.h"
#include "nau/app/application_runtime_service.h"
#include "nau/app/core_window_manager.h"
#include "nau/module/module.h"
#include "nau/render/scene_backend.h"
#include "nau/scene/camera/camera_manager.h"
#include "nau/scene/components/static_mesh_component.h"
#include "nau/scene/internal/scene_manager_internal.h"
#include "nau/scene/packaged_scene_presenter.h"
#include "nau/scene/scene_manager.h"
#include "nau/serialization/json.h"
#include "nau/service/service.h"
#include "nau/service/service_provider.h"
#include "rendering_policy.h"
#ifndef __EMSCRIPTEN__
    #include "nau/platform/windows/app/windows_window.h"
#endif

namespace nau::render
{
    namespace
    {
        MatrixValues matrixValues(const math::Matrix4& matrix)
        {
            MatrixValues values;
            for (unsigned column = 0; column < 4; ++column)
            {
                const auto vector = matrix.getCol(column);
                for (unsigned row = 0; row < 4; ++row)
                    values[column * 4 + row] = float(vector.getElem(row));
            }
            return values;
        }
        Result<> validateTransform(const math::Transform& transform, const std::string& identity)
        {
            const auto scale = transform.getScale();
            if (!transform.isValid() || float(scale.getX()) <= 0 || float(scale.getY()) <= 0 || float(scale.getZ()) <= 0)
                return NauMakeError("Invalid, singular or reflected render transform {}", identity);
            for (float value : matrixValues(transform.getMatrix()))
                if (!std::isfinite(value))
                    return NauMakeError("Non-finite render transform {}", identity);
            return {};
        }
        class SceneRenderer final : public scene::IPackagedScenePresenter,
                                    public IServiceInitialization,
                                    public IServiceShutdown,
                                    public IApplicationRuntimeService
        {
            NAU_RTTI_CLASS(SceneRenderer, scene::IPackagedScenePresenter, IServiceInitialization, IServiceShutdown, IApplicationRuntimeService)
        public:
            std::vector<const rtti::TypeInfo*> getServiceDependencies() const override
            {
                return {&rtti::getTypeInfo<scene::ISceneManager>(), &rtti::getTypeInfo<ICoreWindowManager>()};
            }
            async::Task<> preparePresentation(std::string expectedPolicy) override
            {
                if (m_started)
                    co_await async::Task<>::makeRejected(NauMakeError("Scene presentation cannot start twice"));
                m_started = true;
                auto json = readRenderingJson("/config/rendering.json");
                if (!json)
                    co_await async::Task<>::makeRejected(json.getError());
                auto policy = parseRenderingPolicy(*json);
                if (!policy)
                    co_await async::Task<>::makeRejected(policy.getError());
                auto expected = serialization::jsonParseString(expectedPolicy);
                if (!expected)
                    co_await async::Task<>::makeRejected(expected.getError());
                auto declared = parseRenderingPolicy(*expected);
                if (!declared)
                    co_await async::Task<>::makeRejected(declared.getError());
                if (*policy != *declared)
                    co_await async::Task<>::makeRejected(NauMakeError("Packaged rendering settings do not match the project declaration"));
                m_policy = *policy;
                std::set<std::string> materialPaths;
                for (const auto& [identity, path] : m_policy.materials)
                    materialPaths.insert(path);
                if (!m_policy.defaultMaterial.empty())
                    materialPaths.insert(m_policy.defaultMaterial);
                for (const auto& path : materialPaths)
                {
                    auto value = readRenderingJson("/" + path);
                    if (!value)
                        co_await async::Task<>::makeRejected(value.getError());
                    auto material = parseSimpleMaterial(*value);
                    if (!material)
                        co_await async::Task<>::makeRejected(NauMakeError("Invalid material {}: {}", path, material.getError()->getDiagMessage()));
                    m_materials.emplace(path, *material);
                }
                if (getApplication().isClosing())
                    co_return;
                if (auto* factory = getServiceProvider().find<ISceneBackendFactory>())
                    m_backend = factory->createBackend();
                else
                    m_backend = createSceneBackend();
                if (!m_backend)
                    co_await async::Task<>::makeRejected(NauMakeError("Scene backend factory returned no renderer"));
                void* window = nullptr;
#ifndef __EMSCRIPTEN__
                window = getServiceProvider().get<ICoreWindowManager>().getActiveWindow().as<IWindowsWindow&>().getWindowHandle();
#endif
                co_await m_backend->initialize(window);
                m_initialFrame.emplace();
                m_ready = true;
                co_await m_initialFrame->getTask();
            }
            Result<> pollApplicationRuntime() override
            {
                if (!m_ready || m_stopped)
                    return m_failure ? Result<>{m_failure} : Result<>{};
                if (getApplication().isClosing())
                {
                    // Release the startup gate but retain all accepted load work
                    // for shutdown; no startup frame is submitted after Stop.
                    if (m_initialFrame)
                    {
                        auto source = std::move(*m_initialFrame);
                        m_initialFrame.reset();
                        source.resolve();
                    }
                    return m_failure ? Result<>{m_failure} : Result<>{};
                }
                if (m_loading && m_loading.isReady())
                {
                    if (m_loading.isRejected())
                        return fail(m_loading.getError());
                    m_loading = nullptr;
                }
                if (m_initialFrame || m_deferredFrame)
                {
                    getServiceProvider().get<scene::ISceneManagerInternal>().pollSceneWork();
                    auto drawn = presentFrame();
                    if (!drawn)
                        return drawn;
                }
                else
                {
                    auto state = m_backend->poll();
                    if (!state)
                        return fail(state.getError());
                }
                return m_failure ? Result<>{m_failure} : Result<>{};
            }
            Result<> presentFrame() override
            {
                if (m_failure)
                    return m_failure;
                if (!m_ready || m_stopped || getApplication().isClosing())
                    return {};
                if (!getServiceProvider().get<scene::ISceneManagerInternal>().isSceneSnapshotReady())
                {
                    m_deferredFrame = true;
                    return {};
                }
                m_deferredFrame = false;
                auto members = meshes();
                std::map<std::string, StaticMeshAssetRef> missing;
                std::set<std::string> live;
                for (auto* mesh : members)
                {
                    auto reference = mesh->getMeshGeometry();
                    auto identity = toString(reference);
                    if (identity.empty())
                        return fail(NauMakeError("Missing geometry on component {}", toString(mesh->getUid())));
                    if (reference.getAssetDiscriptor()->getAssetPath().getAssetInnerPath() != "mesh/0")
                        return fail(NauMakeError("Unsupported geometry subresource {} on component {}", identity, toString(mesh->getUid())));
                    live.insert(identity);
                    if (!m_meshes.contains(identity))
                        missing.emplace(identity, std::move(reference));
                }
                if (!missing.empty() && !m_loading)
                    m_loading = loadMeshes(std::move(missing));
                if (m_loading && m_loading.isReady())
                {
                    if (m_loading.isRejected())
                        return fail(m_loading.getError());
                    m_loading = nullptr;
                }
                // A loader may complete synchronously and owner-thread callbacks
                // may change membership. Never reuse borrowed scene pointers
                // across that accepted operation.
                members = meshes();
                live.clear();
                for (auto* mesh : members)
                    live.insert(toString(mesh->getMeshGeometry()));
                auto camera = getServiceProvider().get<scene::ICameraManager>().getCameras().getWorldMainCamera();
                if (!camera)
                    return fail(NauMakeError("Active render world has no camera"));
                const float fov = camera->getFov(), nearPlane = camera->getClipNearPlane(), farPlane = camera->getClipFarPlane();
                if (!std::isfinite(fov) || fov <= 0 || fov >= 180 || !std::isfinite(nearPlane) || nearPlane <= 0 || !std::isfinite(farPlane) || farPlane <= nearPlane)
                    return fail(NauMakeError("Invalid render camera {}", toString(camera->getCameraUid())));
                auto validCamera = validateTransform(camera->getWorldTransform(), toString(camera->getCameraUid()));
                if (!validCamera)
                    return fail(validCamera.getError());
                auto drawable = m_backend->poll();
                if (!drawable)
                    return fail(drawable.getError());
                if (!drawable->width || !drawable->height)
                    return {};
                SceneFrame frame;
                frame.camera = toString(camera->getCameraUid());
                frame.lightDirection = m_policy.direction;
                frame.ambient = m_policy.ambient;
                frame.directional = m_policy.directional;
                const float f = 1.f / std::tan(fov * .00872664626f);
                const float aspect = float(drawable->width) / drawable->height;
                const math::Matrix4 projection{
                    math::Vector4(f / aspect, 0, 0, 0), math::Vector4(0, f, 0, 0),
                    math::Vector4(0, 0, (farPlane + nearPlane) / (nearPlane - farPlane), -1),
                    math::Vector4(0, 0, 2 * farPlane * nearPlane / (nearPlane - farPlane), 0)};
                frame.viewProjection = matrixValues(projection * math::inverse(camera->getWorldTransform().getMatrix()));
                for (float value : frame.viewProjection)
                    if (!std::isfinite(value))
                        return fail(NauMakeError("Render camera projection is not representable: {}", frame.camera));
                for (auto* mesh : members)
                {
                    const auto geometry = toString(mesh->getMeshGeometry());
                    auto found = m_meshes.find(geometry);
                    if (found == m_meshes.end())
                        continue;
                    const auto identity = toString(mesh->getUid());
                    auto valid = validateTransform(mesh->getWorldTransform(), identity);
                    if (!valid)
                        return fail(valid.getError());
                    auto material = toString(mesh->getMaterial());
                    std::string path = m_policy.defaultMaterial;
                    if (!material.empty())
                    {
                        if (!material.starts_with("uid:"))
                            return fail(NauMakeError("Unsupported material reference {} on {}", material, identity));
                        auto mapping = m_policy.materials.find(material.substr(4));
                        if (mapping == m_policy.materials.end())
                            return fail(NauMakeError("Missing material mapping {} on {}", material, identity));
                        path = mapping->second;
                    }
                    auto color = m_materials.find(path);
                    if (color == m_materials.end())
                        return fail(NauMakeError("Missing explicit material for component {}", identity));
                    SceneDraw draw;
                    draw.component = identity;
                    draw.geometry = geometry;
                    draw.mesh = found->second;
                    const auto model = mesh->getWorldTransform().getMatrix();
                    draw.model = matrixValues(model);
                    draw.normals = matrixValues(math::transpose(math::inverse(model)));
                    for (float value : draw.normals)
                        if (!std::isfinite(value))
                            return fail(NauMakeError("Render normal transform is singular or not representable: {}", identity));
                    draw.color = color->second;
                    frame.draws.push_back(std::move(draw));
                }
                // Initial presentation requires all visible geometry. Later
                // activated objects join frames when their accepted loads settle.
                if (m_initialFrame && (!missing.empty() || m_loading))
                    return {};
                auto submitted = m_backend->submit(frame);
                if (!submitted)
                    return fail(submitted.getError());
                for (auto* observer : getServiceProvider().getAll<ISceneFrameObserver>())
                    observer->frameSubmitted(frame);
                for (auto iterator = m_meshes.begin(); iterator != m_meshes.end();)
                    if (!live.contains(iterator->first))
                        iterator = m_meshes.erase(iterator);
                    else
                        ++iterator;
                if (m_initialFrame)
                {
                    auto source = std::move(*m_initialFrame);
                    m_initialFrame.reset();
                    source.resolve();
                    NAU_LOG_INFO("Packaged scene initial frame submitted: {} meshes", frame.draws.size());
                }
                return {};
            }
            async::Task<> shutdownService() override
            {
                m_stopped = true;
                if (m_initialFrame)
                {
                    auto source = std::move(*m_initialFrame);
                    m_initialFrame.reset();
                    source.resolve();
                }
                if (m_loading)
                {
                    auto settled = co_await m_loading.doTry();
                    if (!settled && !m_failure)
                        m_failure = settled.getError();
                    m_loading = nullptr;
                }
                if (m_backend)
                {
                    auto stopped = m_backend->stop();
                    if (!stopped && !m_failure)
                        m_failure = stopped.getError();
                    m_backend.reset();
                }
                m_meshes.clear();
                m_materials.clear();
                NAU_LOG_INFO("Packaged scene presentation cleanup complete");
                if (m_failure)
                    co_await async::Task<>::makeRejected(m_failure);
            }

        private:
            std::vector<scene::StaticMeshComponent*> meshes()
            {
                NAU_ASSERT(getApplication().isMainThread());
                std::vector<scene::StaticMeshComponent*> result;
                for (auto scene : getServiceProvider().get<scene::ISceneManager>().getDefaultWorld().getScenes())
                {
                    if (!scene)
                        continue;
                    scene->getRoot().walkComponents([](scene::Component& component, void* context)
                    {
                        auto* mesh = component.as<scene::StaticMeshComponent*>();
                        if (mesh && mesh->getActivationState() == scene::ActivationState::Active && mesh->getVisibility())
                            static_cast<std::vector<scene::StaticMeshComponent*>*>(context)->push_back(mesh);
                        return true;
                    }, &result, true);
                }
                return result;
            }
            async::Task<> loadMeshes(std::map<std::string, StaticMeshAssetRef> requests)
            {
                for (auto& [identity, reference] : requests)
                {
                    if (getApplication().isClosing() || m_stopped)
                        co_return;
                    auto view = co_await reference.getAssetViewTyped<CpuMeshView>();
                    NAU_ASSERT(getApplication().isMainThread());
                    if (!view)
                        co_await async::Task<>::makeRejected(NauMakeError("Required CPU geometry unavailable: {}", identity));
                    if (getApplication().isClosing() || m_stopped)
                        co_return;
                    auto members = meshes();
                    if (std::any_of(members.begin(), members.end(), [&](auto* mesh)
                    {
                        return toString(mesh->getMeshGeometry()) == identity;
                    }))
                        m_meshes.emplace(identity, std::move(view));
                }
            }
            Result<> fail(Error::Ptr error)
            {
                if (!m_failure)
                    m_failure = std::move(error);
                getApplication().stop();
                return m_failure;
            }
            bool m_started = false, m_ready = false, m_stopped = false;
            Error::Ptr m_failure;
            RenderingPolicy m_policy;
            std::map<std::string, std::array<float, 3>> m_materials;
            std::map<std::string, nau::Ptr<CpuMeshView>> m_meshes;
            std::unique_ptr<SceneBackend> m_backend;
            async::Task<> m_loading;
            bool m_deferredFrame = false;
            std::optional<async::TaskSource<>> m_initialFrame;
        };
    }  // namespace
    struct SceneRenderingModule final : IModule
    {
        std::string getModuleName() override
        {
            return "SceneRenderer";
        }
        void initialize() override
        {
            // This logical feature owns its CPU asset services; projects do not
            // need to declare or statically register a second internal module.
            NAU_MODULE_EXPORT_SERVICE(CpuMeshContainerLoader);
            NAU_MODULE_EXPORT_SERVICE(CpuMeshViewFactory);
            NAU_MODULE_EXPORT_SERVICE(SceneRenderer);
        }
        void postInit() override
        {
        }
        void deinitialize() override
        {
        }
    };
}  // namespace nau::render
IMPLEMENT_MODULE(nau::render::SceneRenderingModule);

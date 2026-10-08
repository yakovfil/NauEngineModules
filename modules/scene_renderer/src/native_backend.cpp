// Copyright 2026 N-GINN LLC. All rights reserved.
// Use of this source code is governed by a BSD-3 Clause license that can be found in the LICENSE file.
#include <algorithm>
#include <map>
#include <set>

#include "nau/3d/dag_drv3d.h"
#include "nau/3d/dag_renderStates.h"
#include "nau/dataBlock/dag_dataBlock.h"
#include "nau/osApiWrappers/dag_cpuJobs.h"
#include "nau/platform/windows/windows_headers.h"
#include "nau/render/scene_backend.h"
#include "nau/startup/dag_globalSettings.h"
#include "nau_scene_shaders.h"

namespace nau::render
{
    namespace
    {
        const DataBlock* rendererSettings()
        {
            static const DataBlock settings = []
            {
                DataBlock value;
                value.addBlock("dx12")->setBool("persistentPipelineCache", false);
                return value;
            }();
            return &settings;
        }
        struct WindowCallback : Driver3dInitCallback
        {
            HWND window;
            explicit WindowCallback(HWND value) :
                window(value)
            {
            }
            int validateDesc(Driver3dDesc&) const override
            {
                return 1;
            }
            int compareDesc(Driver3dDesc&, Driver3dDesc&) const override
            {
                return 0;
            }
            void verifyResolutionSettings(int& width, int& height, int, int, bool) const override
            {
                RECT client{};
                GetClientRect(window, &client);
                width = std::max<LONG>(1, client.right);
                height = std::max<LONG>(1, client.bottom);
            }
        };
        class NativeBackend final : public SceneBackend
        {
        public:
            ~NativeBackend() override
            {
                [[maybe_unused]] auto cleanup = stop();
            }
            Result<> initialize(void* window) override
            {
                if (m_driver || m_stopped)
                    return NauMakeError("Native scene renderer cannot initialize twice");
                m_window = static_cast<HWND>(window);
                if (!IsWindow(m_window))
                    return NauMakeError("Native scene renderer requires a live window");
                if (d3d::is_inited())
                    return NauMakeError("Native scene renderer requires exclusive driver ownership");
                m_previousSettings = dgs_get_settings;
                dgs_get_settings = rendererSettings;
                if (!cpujobs::is_inited())
                {
                    cpujobs::init();
                    m_ownedJobs = true;
                }
                dgs_set_window_mode(WindowMode::WINDOWED);
                m_driver = d3d::init_driver();
                if (!m_driver)
                    return NauMakeError("Native scene driver initialization failed");
                WindowCallback callback(m_window);
                void* mainWindow = m_window;
                if (!d3d::init_video(GetModuleHandleW(nullptr), nullptr, "Nau", SW_SHOW, mainWindow, m_window,
                                     nullptr, "Nau packaged scene", &callback))
                    return NauMakeError("Native scene graphics initialization failed");
                m_video = true;
                VSDTYPE layout[] = {VSD_STREAM_PER_VERTEX_DATA(0), VSD_REG(VSDR_POS, VSDT_FLOAT3), VSD_REG(VSDR_NORM, VSDT_FLOAT3), VSD_END};
                m_declaration = d3d::create_vdecl(layout);
                dxil::ShaderResourceUsageTable usage;
                usage.bRegisterUseMask = 1;
                const auto vertex = d3d::create_raw_vertex_shader(std::span{shaders::NativeVertex}, usage, m_declaration);
                const auto pixel = d3d::create_raw_pixel_shader(std::span{shaders::NativeFragment}, usage);
                if (vertex != BAD_VPROG && pixel != BAD_FSHADER && m_declaration != BAD_VDECL)
                    m_program = d3d::create_program(vertex, pixel, m_declaration);
                if (m_program == BAD_PROGRAM)
                {
                    if (vertex != BAD_VPROG)
                        d3d::delete_vertex_shader(vertex);
                    if (pixel != BAD_FSHADER)
                        d3d::delete_pixel_shader(pixel);
                    return NauMakeError("Native scene shader creation failed");
                }
                ::shaders::RenderState settings;
                settings.ztest = settings.zwrite = 1;
                settings.zFunc = CMPF_LESS;
                settings.cull = CULL_NONE;
                m_renderState = d3d::create_render_state(settings);
                return {};
            }
            Result<SceneDrawable> poll() override
            {
                if (m_stopped || !m_video)
                    return NauMakeError("Native scene renderer is not ready");
                if (!IsWindow(m_window))
                    return NauMakeError("Native scene window was lost");
                bool canReset = false;
                if (d3d::device_lost(&canReset))
                {
                    m_lost = true;
                    return NauMakeError("Native scene graphics device was lost");
                }
                RECT size{};
                if (!GetClientRect(m_window, &size))
                    return NauMakeError("Native scene drawable query failed");
                SceneDrawable drawable{static_cast<unsigned>(std::max<LONG>(0, size.right)), static_cast<unsigned>(std::max<LONG>(0, size.bottom))};
                if (IsIconic(m_window))
                    return SceneDrawable{};
                if (drawable.width && drawable.height && (drawable.width != m_drawable.width || drawable.height != m_drawable.height))
                {
                    d3d::finish_render_commands();
                    d3d::set_screen_size(drawable.width, drawable.height);
                    if (m_depth)
                        m_depth->destroy();
                    m_depth = d3d::create_tex(nullptr, drawable.width, drawable.height, TEXCF_RTARGET | TEXFMT_DEPTH32, 1, u8"portable_scene_depth");
                    if (!m_depth)
                        return NauMakeError("Native scene depth target creation failed");
                }
                m_drawable = drawable;
                return drawable;
            }
            Result<> submit(const SceneFrame& frame) override
            {
                auto drawable = poll();
                if (!drawable)
                    return drawable.getError();
                if (!drawable->width || !drawable->height)
                    return {};
                std::set<std::string> live;
                for (const auto& draw : frame.draws)
                    live.insert(draw.geometry);
                if (std::any_of(m_meshes.begin(), m_meshes.end(), [&](const auto& item)
                {
                    return !live.contains(item.first);
                }))
                {
                    d3d::finish_render_commands();
                    for (auto iterator = m_meshes.begin(); iterator != m_meshes.end();)
                    {
                        if (!live.contains(iterator->first))
                        {
                            iterator->second.release();
                            iterator = m_meshes.erase(iterator);
                        }
                        else
                            ++iterator;
                    }
                }
                if (!d3d::set_render_target() || !d3d::set_depth(m_depth, DepthAccess::RW) ||
                    !d3d::setview(0, 0, drawable->width, drawable->height, 0, 1) ||
                    !d3d::clearview(CLEAR_TARGET | CLEAR_ZBUFFER, math::E3DCOLOR(12, 16, 24, 255), 1, 0) || !d3d::set_program(m_program))
                    return NauMakeError("Native scene frame setup failed");
                d3d::set_render_state(m_renderState);
                for (const auto& draw : frame.draws)
                {
                    auto found = m_meshes.find(draw.geometry);
                    if (found == m_meshes.end())
                    {
                        if (!draw.mesh || draw.mesh->positions.empty() || draw.mesh->normals.size() != draw.mesh->positions.size() || draw.mesh->indices.empty())
                            return NauMakeError("Invalid native scene mesh {}", draw.geometry);
                        Mesh mesh;
                        std::vector<float> vertices;
                        for (size_t i = 0; i < draw.mesh->positions.size(); ++i)
                        {
                            vertices.insert(vertices.end(), draw.mesh->positions[i].begin(), draw.mesh->positions[i].end());
                            vertices.insert(vertices.end(), draw.mesh->normals[i].begin(), draw.mesh->normals[i].end());
                        }
                        mesh.vertices = d3d::create_vb(vertices.size() * sizeof(float), 0, u8"portable_scene_vertices");
                        mesh.indices = d3d::create_ib(draw.mesh->indices.size() * sizeof(uint16_t), 0, u8"portable_scene_indices");
                        if (!mesh.vertices || !mesh.indices || !mesh.vertices->updateData(0, vertices.size() * sizeof(float), vertices.data(), VBLOCK_WRITEONLY) ||
                            !mesh.indices->updateData(0, draw.mesh->indices.size() * sizeof(uint16_t), draw.mesh->indices.data(), VBLOCK_WRITEONLY))
                        {
                            mesh.release();
                            return NauMakeError("Native scene mesh upload failed: {}", draw.geometry);
                        }
                        mesh.count = draw.mesh->indices.size();
                        found = m_meshes.emplace(draw.geometry, mesh).first;
                    }
                    std::array<float, 64> parameters{};
                    std::copy(draw.model.begin(), draw.model.end(), parameters.begin());
                    std::copy(frame.viewProjection.begin(), frame.viewProjection.end(), parameters.begin() + 16);
                    for (unsigned column = 0; column < 4; ++column)
                        parameters[16 + column * 4 + 2] = .5f * (frame.viewProjection[column * 4 + 2] + frame.viewProjection[column * 4 + 3]);
                    std::copy(draw.normals.begin(), draw.normals.end(), parameters.begin() + 32);
                    std::copy(draw.color.begin(), draw.color.end(), parameters.begin() + 48);
                    std::copy(frame.lightDirection.begin(), frame.lightDirection.end(), parameters.begin() + 52);
                    std::copy(frame.ambient.begin(), frame.ambient.end(), parameters.begin() + 56);
                    std::copy(frame.directional.begin(), frame.directional.end(), parameters.begin() + 60);
                    if (!d3d::set_vs_const(0, parameters.data(), 16) || !d3d::set_ps_const(0, parameters.data(), 16) ||
                        !d3d::setvsrc(0, found->second.vertices, 6 * sizeof(float)) || !d3d::setind(found->second.indices) ||
                        !d3d::drawind(PRIM_TRILIST, 0, found->second.count / 3, 0))
                        return NauMakeError("Native scene draw failed: {}", draw.geometry);
                }
                if (!d3d::update_screen())
                    return NauMakeError("Native scene presentation failed");
                return {};
            }
            Result<> stop() override
            {
                if (m_stopped)
                    return {};
                m_stopped = true;
                if (m_video && !m_lost)
                    d3d::finish_render_commands();
                for (auto& [identity, mesh] : m_meshes)
                    mesh.release();
                m_meshes.clear();
                if (m_depth)
                    m_depth->destroy(), m_depth = nullptr;
                if (m_program != BAD_PROGRAM)
                    d3d::delete_program(m_program), m_program = BAD_PROGRAM;
                if (m_declaration != BAD_VDECL)
                    d3d::delete_vdecl(m_declaration), m_declaration = BAD_VDECL;
                if (m_driver)
                    d3d::release_driver(), m_driver = false;
                if (m_ownedJobs)
                {
                    cpujobs::term(false);
                    m_ownedJobs = false;
                    if (cpujobs::is_inited())
                        return NauMakeError("Native scene job system did not complete teardown");
                }
                if (m_previousSettings)
                    dgs_get_settings = m_previousSettings;
                return {};
            }

        private:
            struct Mesh
            {
                Sbuffer* vertices = nullptr;
                Sbuffer* indices = nullptr;
                unsigned count = 0;
                void release()
                {
                    if (vertices)
                        vertices->destroy(), vertices = nullptr;
                    if (indices)
                        indices->destroy(), indices = nullptr;
                }
            };
            HWND m_window = nullptr;
            bool m_driver = false, m_video = false, m_stopped = false, m_lost = false;
            bool m_ownedJobs = false;
            const DataBlock* (*m_previousSettings)() = nullptr;
            PROGRAM m_program = BAD_PROGRAM;
            VDECL m_declaration = BAD_VDECL;
            ::shaders::DriverRenderStateId m_renderState;
            BaseTexture* m_depth = nullptr;
            SceneDrawable m_drawable;
            std::map<std::string, Mesh> m_meshes;
        };
    }  // namespace
    std::unique_ptr<SceneBackend> createSceneBackend()
    {
        return std::make_unique<NativeBackend>();
    }
}  // namespace nau::render

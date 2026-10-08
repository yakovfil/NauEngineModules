// Copyright 2026 N-GINN LLC. All rights reserved.
// Use of this source code is governed by a BSD-3 Clause license that can be found in the LICENSE file.
#include <d3dcompiler.h>

#include "nau/3d/dag_drv3d.h"
#include "nau/3d/dag_renderStates.h"
#include "nau/dataBlock/dag_dataBlock.h"
#include "nau/diag/log_subscribers.h"
#include "nau/diag/logging.h"
#include "nau/osApiWrappers/dag_cpuJobs.h"
#include "nau/platform/windows/windows_headers.h"
#include "nau/service/service_provider.h"
#include "nau/startup/dag_globalSettings.h"

namespace
{
    const nau::DataBlock* checkpointSettings()
    {
        static nau::DataBlock settings;
        settings.addBlock("dx12")->setBool("persistentPipelineCache", false);
        return &settings;
    }
    struct InitCallback : Driver3dInitCallback
    {
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
            width = 640;
            height = 480;
        }
    };
    LRESULT CALLBACK windowProcedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
    {
        if (message == WM_DESTROY)
        {
            PostQuitMessage(0);
            return 0;
        }
        return DefWindowProcW(window, message, wparam, lparam);
    }
}  // namespace

int main()
{
    nau::diag::setLogger(nau::diag::createLogger());
    auto logs = nau::diag::getLogger().subscribe(nau::diag::createConioOutputLogSubscriber());
    nau::setDefaultServiceProvider(nau::createServiceProvider());
    cpujobs::init();
    dgs_get_settings = checkpointSettings;
    WNDCLASSW klass{};
    klass.lpfnWndProc = windowProcedure;
    klass.hInstance = GetModuleHandleW(nullptr);
    klass.lpszClassName = L"NauRenderingCheckpoint";
    if (!RegisterClassW(&klass))
        return 1;
    HWND window = CreateWindowW(klass.lpszClassName, L"Nau portable rendering checkpoint", WS_OVERLAPPEDWINDOW,
                                CW_USEDEFAULT, CW_USEDEFAULT, 640, 480, nullptr, nullptr, klass.hInstance, nullptr);
    if (!window)
        return 2;
    bool driver = d3d::init_driver();
    int result = 0;
    InitCallback callback;
    void* mainWindow = window;
    dgs_set_window_mode(WindowMode::WINDOWED);
    if (!driver || !d3d::init_video(klass.hInstance, nullptr, "NauRenderingCheckpoint", SW_SHOW, mainWindow,
                                    window, nullptr, "Nau portable rendering checkpoint", &callback))
        result = 3;
    VDECL declaration = BAD_VDECL;
    PROGRAM program = BAD_PROGRAM;
    if (!result)
    {
        constexpr char source[] = R"(
float4 vs(float3 p:POSITION):SV_POSITION { return float4(p,1); }
float4 ps():SV_TARGET { return float4(0.15,0.55,0.95,1); }
)";
        ID3DBlob* vertex = nullptr;
        ID3DBlob* pixel = nullptr;
        if (FAILED(D3DCompile(source, sizeof(source) - 1, "checkpoint", nullptr, nullptr, "vs", "vs_5_0", 0, 0, &vertex, nullptr)) ||
            FAILED(D3DCompile(source, sizeof(source) - 1, "checkpoint", nullptr, nullptr, "ps", "ps_5_0", 0, 0, &pixel, nullptr)))
            result = 4;
        else
        {
            VSDTYPE layout[] = {VSD_STREAM_PER_VERTEX_DATA(0), VSD_REG(VSDR_POS, VSDT_FLOAT3), VSD_END};
            declaration = d3d::create_vdecl(layout);
            dxil::ShaderResourceUsageTable usage;
            const auto bytes = [](ID3DBlob* blob)
            {
                return std::span<const uint8_t>(static_cast<const uint8_t*>(blob->GetBufferPointer()), blob->GetBufferSize());
            };
            const auto vs = d3d::create_raw_vertex_shader(bytes(vertex), usage, declaration);
            const auto ps = d3d::create_raw_pixel_shader(bytes(pixel), usage);
            if (vs != BAD_VPROG && ps != BAD_FSHADER && declaration != BAD_VDECL)
                program = d3d::create_program(vs, ps, declaration);
            if (program == BAD_PROGRAM)
            {
                if (vs != BAD_VPROG)
                    d3d::delete_vertex_shader(vs);
                if (ps != BAD_FSHADER)
                    d3d::delete_pixel_shader(ps);
                result = 5;
            }
        }
        if (vertex)
            vertex->Release();
        if (pixel)
            pixel->Release();
    }
    if (!result)
    {
        shaders::RenderState settings;
        settings.ztest = settings.zwrite = 0;
        settings.cull = CULL_NONE;
        const auto state = d3d::create_render_state(settings);
        const float vertices[] = {-0.7f, -0.6f, 0.5f, 0, 0.7f, 0.5f, 0.7f, -0.6f, 0.5f};
        ShowWindow(window, SW_SHOW);
        NAU_LOG_INFO("Native driver checkpoint initialized");
        bool running = true;
        int drawableWidth = 0;
        int drawableHeight = 0;
        while (running)
        {
            MSG message;
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
            {
                if (message.message == WM_QUIT)
                    running = false;
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
            if (!running)
                break;
            RECT size{};
            GetClientRect(window, &size);
            if (IsIconic(window) || !size.right || !size.bottom)
            {
                Sleep(10);
                continue;
            }
            bool canReset = false;
            if (d3d::device_lost(&canReset))
            {
                result = 6;
                break;
            }
            if (size.right != drawableWidth || size.bottom != drawableHeight)
            {
                d3d::finish_render_commands();
                d3d::set_screen_size(size.right, size.bottom);
                drawableWidth = size.right;
                drawableHeight = size.bottom;
            }
            d3d::set_render_target();
            d3d::setview(0, 0, size.right, size.bottom, 0, 1);
            d3d::clearview(CLEAR_TARGET, nau::math::E3DCOLOR(12, 16, 24, 255), 1, 0);
            d3d::set_render_state(state);
            if (!d3d::set_program(program) || !d3d::draw_up(PRIM_TRILIST, 1, vertices, 3 * sizeof(float)) || !d3d::update_screen())
            {
                result = 7;
                break;
            }
            Sleep(5);
        }
        d3d::finish_render_commands();
    }
    if (program != BAD_PROGRAM)
        d3d::delete_program(program);
    if (declaration != BAD_VDECL)
        d3d::delete_vdecl(declaration);
    if (driver)
        d3d::release_driver();
    if (IsWindow(window))
        DestroyWindow(window);
    NAU_LOG_INFO("Native driver checkpoint finished: {}", result);
    return result;
}

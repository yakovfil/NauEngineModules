// Copyright 2026 N-GINN LLC. All rights reserved.
// Use of this source code is governed by a BSD-3 Clause license that can be found in the LICENSE file.
#include "text_overlay.h"

#include <d3dcompiler.h>

#include <algorithm>
#include <map>
#if NAU_TEXT_CAPTURE_TESTS
    #include <chrono>
    #include <cstdlib>
    #include <fstream>
#endif

#include "nau/3d/dag_drv3d.h"
#include "nau/3d/dag_render.h"
#include "nau/3d/dag_renderStates.h"
#include "nau/3d/shader_header.h"
#include "nau/image/dag_texPixel.h"
#include "nau/memory/heap_byte_allocator.h"
#include "nau/service/service_provider.h"
#include "nau/text/screen_text.h"

namespace nau::render
{
    struct TextOverlay::State
    {
        PROGRAM program = BAD_PROGRAM;
        VDECL declaration = BAD_VDECL;
        shaders::DriverRenderStateId renderState;
        d3d::SamplerHandle sampler;
        struct Texture
        {
            std::weak_ptr<const text::TextLine> owner;
            BaseTexture* texture = nullptr;
        };
        std::map<const text::TextLine*, Texture> textures;
#if NAU_TEXT_CAPTURE_TESTS
        std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
        bool captured = false;
#endif

        ~State()
        {
            NAU_LOG_DEBUG("Screen text releasing {} native coverage textures", textures.size());
            for (auto& [key, value] : textures)
                if (value.texture)
                    value.texture->destroy();
            if (program != BAD_PROGRAM)
                d3d::delete_program(program);
            if (declaration != BAD_VDECL)
                d3d::delete_vdecl(declaration);
        }

        Result<> initialize()
        {
            constexpr char source[] = R"(
struct Vertex { float2 position:POSITION; float2 uv:TEXCOORD0; float4 color:COLOR0; };
struct Pixel { float4 position:SV_POSITION; float2 uv:TEXCOORD0; float4 color:COLOR0; };
Pixel vs(Vertex v) { Pixel p; p.position=float4(v.position,0,1); p.uv=v.uv; p.color=v.color; return p; }
Texture2D coverage:register(t0); SamplerState sampleCoverage:register(s0);
float4 ps(Pixel p):SV_TARGET { return float4(p.color.rgb,p.color.a*coverage.Sample(sampleCoverage,p.uv).a); }
)";
            ID3DBlob* vertex = nullptr;
            ID3DBlob* pixel = nullptr;
            ID3DBlob* diagnostic = nullptr;
            auto compile = [&](const char* entry, const char* profile, ID3DBlob** output)
            {
                const HRESULT result = D3DCompile(source, sizeof(source) - 1, "screen_text", nullptr, nullptr,
                                                  entry, profile, D3DCOMPILE_ENABLE_STRICTNESS, 0, output, &diagnostic);
                if (diagnostic)
                    diagnostic->Release(), diagnostic = nullptr;
                return SUCCEEDED(result);
            };
            const bool compiled = compile("vs", "vs_5_0", &vertex) && compile("ps", "ps_5_0", &pixel);
            if (!compiled)
            {
                if (vertex)
                    vertex->Release();
                if (pixel)
                    pixel->Release();
                return NauMakeError("Screen text shader compilation failed");
            }
            VSDTYPE layout[] = {VSD_STREAM_PER_VERTEX_DATA(0), VSD_REG(VSDR_POS, VSDT_FLOAT2),
                                VSD_REG(VSDR_TEXC0, VSDT_FLOAT2), VSD_REG(VSDR_DIFF, VSDT_FLOAT4), VSD_END};
            declaration = d3d::create_vdecl(layout);
            dxil::ShaderResourceUsageTable usage;
            const auto bytes = [](ID3DBlob* blob)
            {
                return std::span<const uint8_t>(static_cast<const uint8_t*>(blob->GetBufferPointer()), blob->GetBufferSize());
            };
            const VPROG vs = d3d::create_raw_vertex_shader(bytes(vertex), usage, declaration);
            usage.tRegisterUseMask = usage.sRegisterUseMask = 1;
            const FSHADER ps = d3d::create_raw_pixel_shader(bytes(pixel), usage);
            vertex->Release();
            pixel->Release();
            if (vs != BAD_VPROG && ps != BAD_FSHADER && declaration != BAD_VDECL)
                program = d3d::create_program(vs, ps, declaration);
            if (program == BAD_PROGRAM)
            {
                if (vs != BAD_VPROG)
                    d3d::delete_vertex_shader(vs);
                if (ps != BAD_FSHADER)
                    d3d::delete_pixel_shader(ps);
                return NauMakeError("Screen text shader/program creation failed");
            }
            shaders::RenderState state;
            state.ztest = state.zwrite = 0;
            auto& blend = state.blendParams[0];
            blend.ablend = blend.sepablend = 1;
            blend.ablendFactors.src = BLEND_SRCALPHA;
            blend.ablendFactors.dst = BLEND_INVSRCALPHA;
            blend.sepablendFactors.src = BLEND_ONE;
            blend.sepablendFactors.dst = BLEND_INVSRCALPHA;
            renderState = d3d::create_render_state(state);
            d3d::SamplerInfo info;
            info.filter_mag_mode = info.filter_min_mode = d3d::FilterMode::Linear;
            info.address_mode_u = info.address_mode_v = d3d::AddressMode::Clamp;
            sampler = d3d::create_sampler(info);
            return {};
        }
    };

    TextOverlay::TextOverlay() :
        m_state(std::make_unique<State>())
    {
    }
    TextOverlay::~TextOverlay() = default;

    Result<> TextOverlay::render(int width, int height)
    {
        if (width <= 0 || height <= 0 || !getServiceProvider().has<text::ScreenText>())
            return {};
        auto& service = getServiceProvider().get<text::ScreenText>();
        const auto frame = service.snapshot();
        for (auto it = m_state->textures.begin(); it != m_state->textures.end();)
        {
            const bool active = std::any_of(frame->begin(), frame->end(), [&](const auto& draw)
            {
                return draw.text.get() == it->first;
            });
            if (!active || it->second.owner.expired())
            {
                if (it->second.texture)
                    it->second.texture->destroy();
                it = m_state->textures.erase(it);
            }
            else
                ++it;
        }
        if (frame->empty())
            return {};
        if (m_state->program == BAD_PROGRAM)
        {
            auto result = m_state->initialize();
            if (!result)
                return result;
        }
        d3d::setview(0, 0, width, height, 0, 1);
        d3d::set_program(m_state->program);
        d3d::setvdecl(m_state->declaration);
        d3d::set_render_state(m_state->renderState);
        d3d::set_sampler(STAGE_PS, 0, m_state->sampler);
        struct Vertex
        {
            float x, y, u, v;
            std::array<float, 4> color;
        };
        for (const auto& draw : *frame)
        {
            const auto& bitmap = *draw.text->bitmap;
            if (bitmap.coverage.empty())
                continue;
            auto& cached = m_state->textures[draw.text.get()];
            if (!cached.texture)
            {
                HeapByteAllocator allocator;
                TexImage32* image = TexImage32::create(bitmap.width, bitmap.height, &allocator);
                if (!image)
                    return NauMakeError("Screen text texture staging allocation failed");
                auto* pixels = reinterpret_cast<unsigned char*>(image->getPixels());
                for (size_t i = 0; i < bitmap.coverage.size(); ++i)
                {
                    pixels[i * 4] = pixels[i * 4 + 1] = pixels[i * 4 + 2] = 255;
                    pixels[i * 4 + 3] = bitmap.coverage[i];
                }
                cached.texture = d3d::create_tex(image, bitmap.width, bitmap.height, TEXFMT_R8G8B8A8, 1, u8"screen_text");
                memfree(image, &allocator);
                if (!cached.texture)
                    return NauMakeError("Screen text coverage upload failed");
                cached.owner = draw.text;
            }
            const float x0 = (draw.position[0] + bitmap.left) * 2 / width - 1;
            const float x1 = x0 + bitmap.width * 2.0f / width;
            const float y0 = 1 - (draw.position[1] + bitmap.top) * 2 / height;
            const float y1 = y0 - bitmap.height * 2.0f / height;
            const Vertex quad[] = {
                {x0, y0, 0, 0, draw.color},
                {x1, y0, 1, 0, draw.color},
                {x1, y1, 1, 1, draw.color},
                {x0, y0, 0, 0, draw.color},
                {x1, y1, 1, 1, draw.color},
                {x0, y1, 0, 1, draw.color}
            };
            d3d::settex(0, cached.texture);
            if (!d3d::draw_up(PRIM_TRILIST, 2, quad, sizeof(Vertex)))
                return NauMakeError("Screen text rectangle draw failed");
        }
        d3d::settex(0, nullptr);
#if NAU_TEXT_CAPTURE_TESTS
        if (const char* path = std::getenv("NAU_SCREEN_TEXT_CAPTURE"); path && !m_state->captured &&
                                                                       std::chrono::steady_clock::now() - m_state->started > std::chrono::seconds(3))
        {
            int w, h, stride, format;
            const auto* pixels = static_cast<const unsigned char*>(d3d::fast_capture_screen(w, h, stride, format));
            if (!pixels)
                return NauMakeError("Native acceptance framebuffer readback failed");
            std::ofstream image(path, std::ios::binary);
            image << "P6\n"
                  << w << " " << h << "\n255\n";
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x)
                {
                    const auto* pixel = pixels + y * stride + x * 4;
                    const char rgb[] = {char(pixel[2]), char(pixel[1]), char(pixel[0])};
                    image.write(rgb, 3);
                }
            d3d::end_fast_capture_screen();
            if (!image)
                return NauMakeError("Native acceptance capture write failed: {}", path);
            m_state->captured = true;
        }
#endif
        service.completePresentation({});
        return {};
    }
}  // namespace nau::render

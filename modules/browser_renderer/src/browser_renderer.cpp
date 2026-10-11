// Copyright 2026 N-GINN LLC. All rights reserved.
// Use of this source code is governed by a BSD-3 Clause license that can be found in the LICENSE file.
#include "nau/render/browser_renderer.h"

#include <GLES3/gl3.h>
#include <emscripten.h>
#include <emscripten/html5.h>
#include <emscripten/threading.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

#include "nau/render/scene_backend.h"
#include "nau/render/vector_lines.h"
#include "nau/scene/scene_object.h"
#include "nau/service/service_provider.h"
#include "nau/text/screen_text.h"

namespace nau
{
    struct BrowserRenderer::State
    {
        EMSCRIPTEN_WEBGL_CONTEXT_HANDLE context = 0;
        GLuint program = 0, vao = 0, buffers[3] = {};
        GLsizei count = 0;
        int width = 0, height = 0, limit = 0;
        unsigned frames = 0;
        bool stopped = false;
        bool batch = false;
        struct MeshResource
        {
            GLuint vao = 0;
            GLuint buffers[3] = {};
            GLsizei count = 0;
        };
        std::map<std::string, MeshResource> meshes;
        GLuint billboardProgram = 0, billboardVao = 0;
        GLuint vectorProgram = 0, vectorVao = 0, vectorBuffer = 0;
        struct BillboardTexture
        {
            nau::Ptr<CpuTextureView> owner;
            GLuint texture = 0;
        };
        std::map<const CpuTextureView*, BillboardTexture> billboardTextures;
        Error::Ptr failure;
        GLuint textProgram = 0, textVao = 0, textBuffer = 0;
        float textScale = 1;
        struct TextTexture
        {
            std::weak_ptr<const text::TextLine> line;
            text::BitmapHandle image;
            GLuint texture = 0;
        };
        std::map<std::pair<const text::TextLine*, float>, TextTexture> textTextures;
    };

    BrowserRenderer::BrowserRenderer() :
        m_state(std::make_unique<State>())
    {
    }
    BrowserRenderer::~BrowserRenderer()
    {
        stop().ignore();
    }

    Result<> BrowserRenderer::initialize(const CpuMeshView& mesh, std::string_view vertex, std::string_view fragment)
    {
        if (mesh.positions.empty() || mesh.normals.size() != mesh.positions.size() || mesh.indices.empty())
            return NauMakeError("Renderer requires validated CPU mesh geometry and normals");
        return initializeProgram(&mesh, vertex, fragment);
    }

    Result<> BrowserRenderer::initializeBatch(std::string_view vertex, std::string_view fragment, std::string_view billboardVertex, std::string_view billboardFragment, bool failVectorShader)
    {
        NauCheckResult(initializeProgram(nullptr, vertex, fragment));
        if (billboardVertex.empty() && billboardFragment.empty())
            return {};
        auto& state = *m_state;
        const auto compile = [](GLenum type, std::string_view source) -> Result<GLuint>
        {
            GLuint shader = glCreateShader(type);
            const char* data = source.data();
            const GLint length = source.size();
            glShaderSource(shader, 1, &data, &length);
            glCompileShader(shader);
            GLint okay = 0;
            glGetShaderiv(shader, GL_COMPILE_STATUS, &okay);
            if (!okay)
            {
                char message[2048]{};
                glGetShaderInfoLog(shader, sizeof(message), nullptr, message);
                glDeleteShader(shader);
                return NauMakeError("Browser billboard shader failed: {}", message);
            }
            return shader;
        };
        auto vs = compile(GL_VERTEX_SHADER, billboardVertex);
        if (!vs)
            return vs.getError();
        auto fs = compile(GL_FRAGMENT_SHADER, billboardFragment);
        if (!fs)
        {
            glDeleteShader(*vs);
            return fs.getError();
        }
        state.billboardProgram = glCreateProgram();
        glAttachShader(state.billboardProgram, *vs);
        glAttachShader(state.billboardProgram, *fs);
        glLinkProgram(state.billboardProgram);
        glDeleteShader(*vs);
        glDeleteShader(*fs);
        GLint okay = 0;
        glGetProgramiv(state.billboardProgram, GL_LINK_STATUS, &okay);
        if (!okay)
            return NauMakeError("Browser billboard shader link failed");
        glGenVertexArrays(1, &state.billboardVao);
        if (failVectorShader)
            return NauMakeError("Browser vector shader creation failed (injected)");
        auto vectorVs = compile(GL_VERTEX_SHADER, "#version 300 es\nlayout(location=0) in vec4 position; layout(location=1) in vec3 color; out vec3 tint; void main(){gl_Position=position;tint=color;}");
        if (!vectorVs)
            return vectorVs.getError();
        auto vectorFs = compile(GL_FRAGMENT_SHADER, "#version 300 es\nprecision highp float; in vec3 tint; out vec4 pixel; void main(){pixel=vec4(tint,1.0);}");
        if (!vectorFs)
        {
            glDeleteShader(*vectorVs);
            return vectorFs.getError();
        }
        state.vectorProgram = glCreateProgram();
        glAttachShader(state.vectorProgram, *vectorVs);
        glAttachShader(state.vectorProgram, *vectorFs);
        glLinkProgram(state.vectorProgram);
        glDeleteShader(*vectorVs);
        glDeleteShader(*vectorFs);
        glGetProgramiv(state.vectorProgram, GL_LINK_STATUS, &okay);
        if (!okay)
            return NauMakeError("Browser vector shader link failed");
        glGenVertexArrays(1, &state.vectorVao);
        glBindVertexArray(state.vectorVao);
        glGenBuffers(1, &state.vectorBuffer);
        glBindBuffer(GL_ARRAY_BUFFER, state.vectorBuffer);
        glBufferData(GL_ARRAY_BUFFER, (render::VectorPresentation::WorldLimit + render::VectorPresentation::OverlayLimit) * 6 * sizeof(render::VectorVertex), nullptr, GL_DYNAMIC_DRAW);
        glEnableVertexAttribArray(0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, sizeof(render::VectorVertex), nullptr);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(render::VectorVertex), reinterpret_cast<void*>(4 * sizeof(float)));
        for (const char* name : {"center", "extent", "marker"})
            if (glGetUniformLocation(state.billboardProgram, name) < 0)
                return NauMakeError("Browser billboard uniform missing: {}", name);
        if (glGetError() != GL_NO_ERROR)
            return NauMakeError("Browser billboard initialization failed");
        return {};
    }

    Result<> BrowserRenderer::initializeProgram(const CpuMeshView* mesh, std::string_view vertex, std::string_view fragment)
    {
        auto& state = *m_state;
        if (state.stopped || state.context || emscripten_is_main_browser_thread())
            return NauMakeError("Renderer initialization requires a fresh application-worker owner");
        state.batch = mesh == nullptr;
        const int canvasReady = MAIN_THREAD_EM_ASM_INT({
            const canvas = document.getElementById('canvas');
            return canvas instanceof HTMLCanvasElement && canvas.isConnected;
        });
        if (!canvasReady)
            return NauMakeError("Missing or invalid canvas host");
        EmscriptenWebGLContextAttributes attributes;
        emscripten_webgl_init_context_attributes(&attributes);
        attributes.majorVersion = 2;
        attributes.explicitSwapControl = true;
        attributes.proxyContextToMainThread = EMSCRIPTEN_WEBGL_CONTEXT_PROXY_DISALLOW;
        attributes.antialias = false;
        state.context = emscripten_webgl_create_context("#canvas", &attributes);
        if (state.context <= 0 || emscripten_webgl_make_context_current(state.context) != EMSCRIPTEN_RESULT_SUCCESS)
            return NauMakeError("WebGL 2 application-worker context unavailable");
        auto compile = [](GLenum type, std::string_view source) -> Result<GLuint>
        {
            const GLuint shader = glCreateShader(type);
            const char* text = source.data();
            const GLint length = static_cast<GLint>(source.size());
            glShaderSource(shader, 1, &text, &length);
            glCompileShader(shader);
            GLint okay = 0;
            glGetShaderiv(shader, GL_COMPILE_STATUS, &okay);
            if (!okay)
            {
                char message[2048] = {};
                glGetShaderInfoLog(shader, sizeof(message), nullptr, message);
                glDeleteShader(shader);
                return NauMakeError("Browser shader compilation failed: {}", message);
            }
            return shader;
        };
        auto vs = compile(GL_VERTEX_SHADER, vertex);
        if (!vs)
            return vs.getError();
        auto fs = compile(GL_FRAGMENT_SHADER, fragment);
        if (!fs)
        {
            glDeleteShader(*vs);
            return fs.getError();
        }
        state.program = glCreateProgram();
        glAttachShader(state.program, *vs);
        glAttachShader(state.program, *fs);
        glLinkProgram(state.program);
        glDeleteShader(*vs);
        glDeleteShader(*fs);
        GLint okay = 0;
        glGetProgramiv(state.program, GL_LINK_STATUS, &okay);
        if (!okay)
        {
            char message[2048] = {};
            glGetProgramInfoLog(state.program, sizeof(message), nullptr, message);
            return NauMakeError("Browser shader link failed: {}", message);
        }
        constexpr std::string_view textVertex = R"(#version 300 es
layout(location=0) in vec2 position;
layout(location=1) in vec2 uv;
out vec2 texcoord;
void main() { texcoord=uv; gl_Position=vec4(position,0.0,1.0); }
)";
        constexpr std::string_view textFragment = R"(#version 300 es
precision highp float;
in vec2 texcoord;
uniform sampler2D coverage;
uniform vec4 tint;
out vec4 color;
void main() { color=vec4(tint.rgb,tint.a*texture(coverage,texcoord).r); }
)";
        auto textVs = compile(GL_VERTEX_SHADER, textVertex);
        if (!textVs)
            return textVs.getError();
        auto textFs = compile(GL_FRAGMENT_SHADER, textFragment);
        if (!textFs)
        {
            glDeleteShader(*textVs);
            return textFs.getError();
        }
        state.textProgram = glCreateProgram();
        glAttachShader(state.textProgram, *textVs);
        glAttachShader(state.textProgram, *textFs);
        glLinkProgram(state.textProgram);
        glDeleteShader(*textVs);
        glDeleteShader(*textFs);
        glGetProgramiv(state.textProgram, GL_LINK_STATUS, &okay);
        if (!okay)
            return NauMakeError("Browser screen-text shader link failed");
        glGenVertexArrays(1, &state.textVao);
        glBindVertexArray(state.textVao);
        glGenBuffers(1, &state.textBuffer);
        glBindBuffer(GL_ARRAY_BUFFER, state.textBuffer);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), nullptr);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), reinterpret_cast<void*>(2 * sizeof(float)));
        const std::vector<const char*> required = state.batch ? std::vector<const char*>{"model", "viewProjection", "normalTransform", "baseColor", "lightDirection", "ambient", "directional"} : std::vector<const char*>{"model", "view", "projection"};
        for (const char* name : required)
            if (glGetUniformLocation(state.program, name) < 0)
                return NauMakeError("Browser shader missing required uniform {}", name);
        if (mesh)
        {
            glGenVertexArrays(1, &state.vao);
            glBindVertexArray(state.vao);
            glGenBuffers(3, state.buffers);
            for (unsigned index = 0; index < 2; ++index)
            {
                const auto& values = index == 0 ? mesh->positions : mesh->normals;
                glBindBuffer(GL_ARRAY_BUFFER, state.buffers[index]);
                glBufferData(GL_ARRAY_BUFFER, values.size() * sizeof(values[0]), values.data(), GL_STATIC_DRAW);
                glEnableVertexAttribArray(index);
                glVertexAttribPointer(index, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
            }
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, state.buffers[2]);
            glBufferData(GL_ELEMENT_ARRAY_BUFFER, mesh->indices.size() * sizeof(uint16_t), mesh->indices.data(), GL_STATIC_DRAW);
            state.count = static_cast<GLsizei>(mesh->indices.size());
        }
        GLint viewport[2] = {};
        glGetIntegerv(GL_MAX_VIEWPORT_DIMS, viewport);
        glGetIntegerv(GL_MAX_RENDERBUFFER_SIZE, &state.limit);
        state.limit = std::min({state.limit, viewport[0], viewport[1]});
        if (state.limit <= 0 || glGetError() != GL_NO_ERROR)
            return NauMakeError("Browser mesh upload or graphics limits failed");
        return poll();
    }

    Result<> BrowserRenderer::poll()
    {
        auto& state = *m_state;
        if (state.failure)
            return state.failure;
        if (state.stopped)
            return {};
        if (!state.context)
            return {};
        if (emscripten_is_webgl_context_lost(state.context))
            state.failure = NauMakeError("WebGL context lost; reload to retry");
        double size[3] = {};
        // Synchronous ordered snapshots; no page callback retains renderer memory.
        const int connected = MAIN_THREAD_EM_ASM_INT({
            const c = document.getElementById('canvas');
            if (!(c instanceof HTMLCanvasElement) || !c.isConnected)
                return 0;
            HEAPF64[$0 >> 3] = c.clientWidth;
            HEAPF64[($0 >> 3) + 1] = c.clientHeight;
            HEAPF64[($0 >> 3) + 2] = window.devicePixelRatio;
            return 1;
        },
                                                     size);
        if (!connected)
            state.failure = NauMakeError("Renderer canvas host lost");
        if (state.failure)
            return state.failure;
        if (!std::isfinite(size[2]) || size[2] <= 0)
            return state.failure = NauMakeError("Invalid canvas pixel ratio");
        const double width = std::round(size[0] * size[2]), height = std::round(size[1] * size[2]);
        const double scale = std::min(1.0, state.limit / std::max({1.0, width, height}));
        state.textScale = float(size[2] * scale);
        const int w = width > 0 ? std::max(1, int(width * scale)) : 0;
        const int h = height > 0 ? std::max(1, int(height * scale)) : 0;
        if (w != state.width || h != state.height)
        {
            state.width = w;
            state.height = h;
            if (w && h && emscripten_set_canvas_element_size("#canvas", w, h) != EMSCRIPTEN_RESULT_SUCCESS)
                return state.failure = NauMakeError("Canvas drawable resize failed");
        }
        return {};
    }

    std::pair<unsigned, unsigned> BrowserRenderer::getDrawableSize() const
    {
        return {m_state->width, m_state->height};
    }

    Result<> BrowserRenderer::drawBatch(const render::SceneFrame& frame, bool failVectorUpload)
    {
        auto& state = *m_state;
        if (!state.batch || state.stopped || !state.context || !state.program)
            return NauMakeError("Browser scene renderer is not ready");
        NauCheckResult(poll());
        if (!state.width || !state.height)
            return {};
        auto vectors = render::tessellateVectors(frame);
        if (!vectors)
            return state.failure = vectors.getError();
        if (failVectorUpload && !frame.vectors.empty())
            return state.failure = NauMakeError("Browser vector buffer upload failed (injected)");
        const auto drawVectors = [&](const auto& vertices, bool depth)
        {
            if (vertices.empty())
                return;
            if (depth)
                glEnable(GL_DEPTH_TEST);
            else
                glDisable(GL_DEPTH_TEST);
            glDepthMask(GL_FALSE);
            glUseProgram(state.vectorProgram);
            glBindVertexArray(state.vectorVao);
            glBindBuffer(GL_ARRAY_BUFFER, state.vectorBuffer);
            glBufferSubData(GL_ARRAY_BUFFER, 0, vertices.size() * sizeof(render::VectorVertex), vertices.data());
            glDrawArrays(GL_TRIANGLES, 0, vertices.size());
        };
        std::set<std::string> live;
        for (const auto& draw : frame.draws)
            live.insert(draw.geometry);
        for (auto iterator = state.meshes.begin(); iterator != state.meshes.end();)
        {
            if (!live.contains(iterator->first))
            {
                glDeleteBuffers(3, iterator->second.buffers);
                glDeleteVertexArrays(1, &iterator->second.vao);
                iterator = state.meshes.erase(iterator);
            }
            else
                ++iterator;
        }
        glViewport(0, 0, state.width, state.height);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
        glDisable(GL_CULL_FACE);
        if (vectors->blackBackground)
            glClearColor(0, 0, 0, 1);
        else
            glClearColor(12.f / 255, 16.f / 255, 24.f / 255, 1);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        drawVectors(vectors->background, false);
        glEnable(GL_DEPTH_TEST);
        glDepthMask(GL_TRUE);
        glUseProgram(state.program);
        glUniformMatrix4fv(glGetUniformLocation(state.program, "viewProjection"), 1, GL_FALSE, frame.viewProjection.data());
        glUniform3fv(glGetUniformLocation(state.program, "lightDirection"), 1, frame.lightDirection.data());
        glUniform3fv(glGetUniformLocation(state.program, "ambient"), 1, frame.ambient.data());
        glUniform3fv(glGetUniformLocation(state.program, "directional"), 1, frame.directional.data());
        for (const auto& draw : frame.draws)
        {
            auto found = state.meshes.find(draw.geometry);
            if (found == state.meshes.end())
            {
                if (!draw.mesh || draw.mesh->positions.empty() || draw.mesh->normals.size() != draw.mesh->positions.size() || draw.mesh->indices.empty())
                    return state.failure = NauMakeError("Invalid browser scene mesh {}", draw.geometry);
                auto& resource = state.meshes[draw.geometry];
                glGenVertexArrays(1, &resource.vao);
                glBindVertexArray(resource.vao);
                glGenBuffers(3, resource.buffers);
                for (unsigned attribute = 0; attribute < 2; ++attribute)
                {
                    const auto& values = attribute ? draw.mesh->normals : draw.mesh->positions;
                    glBindBuffer(GL_ARRAY_BUFFER, resource.buffers[attribute]);
                    glBufferData(GL_ARRAY_BUFFER, values.size() * sizeof(values[0]), values.data(), GL_STATIC_DRAW);
                    glEnableVertexAttribArray(attribute);
                    glVertexAttribPointer(attribute, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
                }
                glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, resource.buffers[2]);
                glBufferData(GL_ELEMENT_ARRAY_BUFFER, draw.mesh->indices.size() * sizeof(uint16_t), draw.mesh->indices.data(), GL_STATIC_DRAW);
                resource.count = draw.mesh->indices.size();
                if (glGetError() != GL_NO_ERROR)
                    return state.failure = NauMakeError("Browser scene upload failed: {}", draw.geometry);
                found = state.meshes.find(draw.geometry);
            }
            glUniformMatrix4fv(glGetUniformLocation(state.program, "model"), 1, GL_FALSE, draw.model.data());
            glUniformMatrix4fv(glGetUniformLocation(state.program, "normalTransform"), 1, GL_FALSE, draw.normals.data());
            glUniform3fv(glGetUniformLocation(state.program, "baseColor"), 1, draw.color.data());
            glBindVertexArray(found->second.vao);
            glDrawElements(GL_TRIANGLES, found->second.count, GL_UNSIGNED_SHORT, nullptr);
        }
        std::set<const CpuTextureView*> liveTextures;
        if (!frame.billboards.empty() && !state.billboardProgram)
            return state.failure = NauMakeError("Browser billboard program unavailable");
        glUseProgram(state.billboardProgram);
        glBindVertexArray(state.billboardVao);
        glActiveTexture(GL_TEXTURE0);
        for (const auto& draw : frame.billboards)
        {
            auto center = render::billboardCenter(frame.viewProjection, draw);
            if (!center)
                return state.failure = center.getError();
            if (!draw.image || draw.image->pixels.size() != uint64_t(draw.image->width) * draw.image->height * 4)
                return state.failure = NauMakeError("Invalid browser billboard texture {}", draw.texture);
            liveTextures.insert(draw.image.get());
            if (!render::billboardInDepth(*center))
                continue;
            auto found = state.billboardTextures.find(draw.image.get());
            if (found == state.billboardTextures.end())
            {
                auto& resource = state.billboardTextures[draw.image.get()];
                resource.owner = draw.image;
                glGenTextures(1, &resource.texture);
                glBindTexture(GL_TEXTURE_2D, resource.texture);
                glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_SRGB8_ALPHA8, draw.image->width, draw.image->height, 0, GL_RGBA, GL_UNSIGNED_BYTE, draw.image->pixels.data());
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                if (glGetError() != GL_NO_ERROR)
                    return state.failure = NauMakeError("Browser billboard upload failed: {}", draw.texture);
                found = state.billboardTextures.find(draw.image.get());
            }
            glBindTexture(GL_TEXTURE_2D, found->second.texture);
            glUniform1i(glGetUniformLocation(state.billboardProgram, "marker"), 0);
            glUniform4fv(glGetUniformLocation(state.billboardProgram, "center"), 1, center->data());
            glUniform2f(glGetUniformLocation(state.billboardProgram, "extent"), draw.size, draw.size * float(state.width) / state.height);
            glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        }
        glBindTexture(GL_TEXTURE_2D, 0);
        for (auto it = state.billboardTextures.begin(); it != state.billboardTextures.end();)
            if (!liveTextures.contains(it->first))
            {
                glDeleteTextures(1, &it->second.texture);
                it = state.billboardTextures.erase(it);
            }
            else
                ++it;
        drawVectors(vectors->depth, true);
        drawVectors(vectors->world, false);
        drawVectors(vectors->overlay, false);
        if (glGetError() != GL_NO_ERROR || emscripten_webgl_commit_frame() != EMSCRIPTEN_RESULT_SUCCESS)
            return state.failure = NauMakeError("Browser scene frame submission failed");
        ++state.frames;
        return {};
    }

    Result<> BrowserRenderer::draw(const scene::SceneObject& mesh, const scene::SceneObject& camera)
    {
        auto& state = *m_state;
        if (state.stopped)
            return NauMakeError("Draw requested after renderer Stop");
        NauCheckResult(poll());
        if (!state.context || !state.program)
            return NauMakeError("Renderer is not ready");
        if (!state.width || !state.height)
            return {};
        const auto& lens = camera.getRootComponent<scene::CameraComponent>();
        const auto model = mesh.getWorldTransform().getMatrix();
        const auto view = math::inverse(camera.getWorldTransform().getMatrix());
        const float nearPlane = lens.getClipNearPlane(), farPlane = lens.getClipFarPlane();
        const float f = 1.f / std::tan(lens.getFov() * 0.00872664626f);
        const float aspect = float(state.width) / state.height;
        const float projection[16] = {f / aspect, 0, 0, 0, 0, f, 0, 0, 0, 0, (farPlane + nearPlane) / (nearPlane - farPlane), -1, 0, 0, 2 * farPlane * nearPlane / (nearPlane - farPlane), 0};
        float modelValues[16], viewValues[16];
        const auto copyMatrix = [](const math::Matrix4& matrix, float* values)
        {
            const math::Vector4 columns[] = {matrix.getCol0(), matrix.getCol1(), matrix.getCol2(), matrix.getCol3()};
            for (unsigned column = 0; column < 4; ++column)
            {
                values[column * 4] = float(columns[column].getX());
                values[column * 4 + 1] = float(columns[column].getY());
                values[column * 4 + 2] = float(columns[column].getZ());
                values[column * 4 + 3] = float(columns[column].getW());
            }
        };
        copyMatrix(model, modelValues);
        copyMatrix(view, viewValues);
        glViewport(0, 0, state.width, state.height);
        glEnable(GL_DEPTH_TEST);
        glClearColor(0.025f, 0.035f, 0.055f, 1);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glUseProgram(state.program);
        glUniformMatrix4fv(glGetUniformLocation(state.program, "model"), 1, GL_FALSE, modelValues);
        glUniformMatrix4fv(glGetUniformLocation(state.program, "view"), 1, GL_FALSE, viewValues);
        glUniformMatrix4fv(glGetUniformLocation(state.program, "projection"), 1, GL_FALSE, projection);
        glBindVertexArray(state.vao);
        glDrawElements(GL_TRIANGLES, state.count, GL_UNSIGNED_SHORT, nullptr);
        if (getServiceProvider().has<text::ScreenText>())
        {
            auto& service = getServiceProvider().get<text::ScreenText>();
            auto frame = service.snapshot();
            std::erase_if(state.textTextures, [&](const auto& item)
            {
                const bool active = item.first.second == state.textScale &&
                                    std::any_of(frame->begin(), frame->end(), [&](const auto& command)
                {
                    return command.text.get() == item.first.first;
                });
                if (!active || item.second.line.expired())
                {
                    glDeleteTextures(1, &item.second.texture);
                    return true;
                }
                return false;
            });
            glDisable(GL_DEPTH_TEST);
            glDepthMask(GL_FALSE);
            glEnable(GL_BLEND);
            glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
            glUseProgram(state.textProgram);
            glUniform1i(glGetUniformLocation(state.textProgram, "coverage"), 0);
            glActiveTexture(GL_TEXTURE0);
            glBindVertexArray(state.textVao);
            glBindBuffer(GL_ARRAY_BUFFER, state.textBuffer);
            unsigned labels = 0;
            for (const auto& command : *frame)
            {
                const auto key = std::make_pair(command.text.get(), state.textScale);
                auto& texture = state.textTextures[key];
                if (!texture.image)
                {
                    auto image = service.rasterize(command.text, state.textScale);
                    if (!image)
                    {
                        service.completePresentation(image.getError());
                        return state.failure = image.getError();
                    }
                    texture.line = command.text;
                    texture.image = *image;
                    if (!texture.image->coverage.empty())
                    {
                        glGenTextures(1, &texture.texture);
                        glBindTexture(GL_TEXTURE_2D, texture.texture);
                        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
                        glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, texture.image->width, texture.image->height,
                                     0, GL_RED, GL_UNSIGNED_BYTE, texture.image->coverage.data());
                        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
                        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                    }
                }
                const auto& image = *texture.image;
                if (image.coverage.empty())
                    continue;
                const float x = command.position[0] * state.textScale + image.left;
                const float y = command.position[1] * state.textScale + image.top;
                const float left = 2 * x / state.width - 1, right = 2 * (x + image.width) / state.width - 1;
                const float top = 1 - 2 * y / state.height, bottom = 1 - 2 * (y + image.height) / state.height;
                const float quad[] = {left, top, 0, 0, right, top, 1, 0, left, bottom, 0, 1,
                                      left, bottom, 0, 1, right, top, 1, 0, right, bottom, 1, 1};
                glBindTexture(GL_TEXTURE_2D, texture.texture);
                glUniform4fv(glGetUniformLocation(state.textProgram, "tint"), 1, command.color.data());
                glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STREAM_DRAW);
                glDrawArrays(GL_TRIANGLES, 0, 6);
                ++labels;
            }
            glBindTexture(GL_TEXTURE_2D, 0);
            glBindVertexArray(0);
            glDisable(GL_BLEND);
            glDepthMask(GL_TRUE);
            glEnable(GL_DEPTH_TEST);
            if (glGetError() != GL_NO_ERROR)
            {
                auto error = NauMakeError("Browser text upload or draw failed");
                service.completePresentation(error);
                return state.failure = error;
            }
            if (!frame->empty())
                service.completePresentation({});
            MAIN_THREAD_EM_ASM({ Module.nauText = ({labels: $0, scale: $1, textures: $2}); }, labels, state.textScale, state.textTextures.size());
#if NAU_TEXT_CAPTURE_TESTS
            if (MAIN_THREAD_EM_ASM_INT({ return Module.nauTextCaptureRequest ? 1 : 0; }))
            {
                std::vector<unsigned char> pixels(size_t(state.width) * state.height * 4);
                glReadPixels(0, 0, state.width, state.height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
                MAIN_THREAD_EM_ASM({
                    Module.nauTextCaptureRequest = false;
                    Module.nauTextSurface = ({width: $2, height: $3, pixels: Array.from(HEAPU8.subarray($0, $0 + $1))});
                },
                                   pixels.data(), pixels.size(), state.width, state.height);
            }
#endif
        }
        if (glGetError() != GL_NO_ERROR || emscripten_webgl_commit_frame() != EMSCRIPTEN_RESULT_SUCCESS)
            return state.failure = NauMakeError("Browser draw submission failed");
        ++state.frames;
        MAIN_THREAD_EM_ASM({
            Module.nauRender = ({frames: $0, width: $1, height: $2, indices: $3, worker: true, model: Array.from(HEAPF32.subarray($4 >> 2, ($4 >> 2) + 16)), view: Array.from(HEAPF32.subarray($5 >> 2, ($5 >> 2) + 16)), projection: Array.from(HEAPF32.subarray($6 >> 2, ($6 >> 2) + 16))});
        },
                           state.frames, state.width, state.height, state.count, modelValues, viewValues, projection);
        return {};
    }

    Result<> BrowserRenderer::stop()
    {
        auto& state = *m_state;
        if (state.stopped)
            return {};
        state.stopped = true;
        if (state.context > 0)
        {
            if (!emscripten_is_webgl_context_lost(state.context))
            {
                glFinish();
                for (const auto& [identity, resource] : state.meshes)
                {
                    glDeleteBuffers(3, resource.buffers);
                    glDeleteVertexArrays(1, &resource.vao);
                }
                for (auto& [key, resource] : state.billboardTextures)
                    glDeleteTextures(1, &resource.texture);
                if (state.billboardProgram)
                    glDeleteProgram(state.billboardProgram);
                glDeleteVertexArrays(1, &state.billboardVao);
                if (state.vectorProgram)
                    glDeleteProgram(state.vectorProgram);
                glDeleteBuffers(1, &state.vectorBuffer);
                glDeleteVertexArrays(1, &state.vectorVao);
                glDeleteBuffers(3, state.buffers);
                glDeleteVertexArrays(1, &state.vao);
                if (state.program)
                    glDeleteProgram(state.program);
                for (const auto& item : state.textTextures)
                    glDeleteTextures(1, &item.second.texture);
                glDeleteBuffers(1, &state.textBuffer);
                glDeleteVertexArrays(1, &state.textVao);
                if (state.textProgram)
                    glDeleteProgram(state.textProgram);
            }
            emscripten_webgl_make_context_current(0);
            const auto destroyed = emscripten_webgl_destroy_context(state.context);
            state.context = 0;
            if (destroyed != EMSCRIPTEN_RESULT_SUCCESS)
                return NauMakeError("Browser context destruction failed");
        }
        const auto releasedTextures = state.textTextures.size();
        NAU_LOG_INFO("Browser billboards releasing {} textures", state.billboardTextures.size());
        state.textTextures.clear();
        state.meshes.clear();
        state.billboardTextures.clear();
        MAIN_THREAD_EM_ASM({if(Module.nauRender) Module.nauRender.stopped = true; });
        MAIN_THREAD_EM_ASM({ Module.nauText = ({labels: 0, textures: 0, released: $0, stopped: true}); }, releasedTextures);
        return {};
    }
}  // namespace nau

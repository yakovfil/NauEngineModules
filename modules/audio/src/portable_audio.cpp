// Copyright 2026 N-GINN LLC. All rights reserved.
// Use of this source code is governed by a BSD-3 Clause license that can be found in the LICENSE file.
#include "nau/audio/portable_audio.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <mutex>

#include "nau/app/core_window_manager.h"
#include "nau/app/main_loop/game_system.h"
#include "nau/app/platform_window_status.h"
#include "nau/io/virtual_file_system.h"
#include "nau/module/module.h"
#include "nau/rtti/rtti_impl.h"
#include "nau/service/service.h"
#include "nau/service/service_provider.h"

#ifdef __EMSCRIPTEN__
    #include <emscripten.h>
    #include <emscripten/proxying.h>
    #include <emscripten/threading.h>
#endif
#define MA_NO_FLAC
#define MA_NO_MP3
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

namespace nau::audio
{
    template <class F>
    ma_result onAudioUi(F&& function)
    {
#ifdef __EMSCRIPTEN__
        ma_result result = MA_ERROR;
        auto work = [&]
        {
            result = function();
        };
        if (emscripten_is_main_runtime_thread())
            work();
        else if (!emscripten_proxy_sync(emscripten_proxy_get_system_queue(), emscripten_main_runtime_thread_id(),
                                        [](void* data)
        {
            (*static_cast<decltype(work)*>(data))();
        }, &work))
            return MA_ERROR;
        return result;
#else
        return function();
#endif
    }
    Result<Clip> decodeClickWav(std::span<const std::byte> bytes)
    {
        const auto bad = []
        {
            return NauMakeError("Unsupported or malformed click WAV (PCM16, mono/stereo, 44.1/48 kHz, <=1 second, <=256 KiB required)");
        };
        if (bytes.size() < 44 || bytes.size() > 256 * 1024)
            return bad();
        const auto* p = reinterpret_cast<const unsigned char*>(bytes.data());
        const auto u16 = [](const unsigned char* v)
        {
            return unsigned(v[0]) | (unsigned(v[1]) << 8);
        };
        const auto u32 = [](const unsigned char* v)
        {
            return unsigned(v[0]) | (unsigned(v[1]) << 8) | (unsigned(v[2]) << 16) | (unsigned(v[3]) << 24);
        };
        if (memcmp(p, "RIFF", 4) || memcmp(p + 8, "WAVE", 4) || u32(p + 4) != bytes.size() - 8)
            return bad();
        unsigned channels = 0, rate = 0, dataBytes = 0;
        bool format = false, data = false;
        size_t at = 12;
        while (at < bytes.size())
        {
            if (bytes.size() - at < 8)
                return bad();
            const unsigned size = u32(p + at + 4);
            const size_t end = at + 8 + size_t(size);
            if (end > bytes.size() || end + (size & 1) > bytes.size())
                return bad();
            if (!memcmp(p + at, "fmt ", 4))
            {
                if (format || size != 16 || u16(p + at + 8) != 1 || u16(p + at + 22) != 16)
                    return bad();
                channels = u16(p + at + 10);
                rate = u32(p + at + 12);
                if ((channels != 1 && channels != 2) || (rate != 44100 && rate != 48000) ||
                    u16(p + at + 20) != channels * 2 || u32(p + at + 16) != rate * channels * 2)
                    return bad();
                format = true;
            }
            else if (!memcmp(p + at, "data", 4))
            {
                if (data)
                    return bad();
                dataBytes = size;
                data = true;
            }
            at = end + (size & 1);
        }
        if (!format || !data || !dataBytes || dataBytes % (channels * 2) || dataBytes / (channels * 2) > rate)
            return bad();
        ma_decoder decoder{};
        auto config = ma_decoder_config_init(ma_format_f32, 2, 48000);
        if (ma_decoder_init_memory(bytes.data(), bytes.size(), &config, &decoder) != MA_SUCCESS)
            return bad();
        auto clip = std::make_shared<AudioClip>();
        clip->samples.resize(48000 * 2);
        ma_uint64 frames = 0;
        const auto result = ma_decoder_read_pcm_frames(&decoder, clip->samples.data(), 48000, &frames);
        ma_decoder_uninit(&decoder);
        if ((result != MA_SUCCESS && result != MA_AT_END) || !frames || frames > 48000)
            return bad();
        clip->samples.resize(size_t(frames) * 2);
        return Clip{std::move(clip)};
    }

    class PortableAudio final : public IPortableAudio,
                                public IServiceInitialization,
                                public IServiceShutdown,
                                public IGamePreUpdate
    {
        friend struct PortableAudioTestAccess;
        NAU_RTTI_CLASS(nau::audio::PortableAudio, IPortableAudio, IServiceInitialization, IServiceShutdown, IGamePreUpdate)
        struct Voice
        {
            ma_audio_buffer buffer{};
            ma_sound sound{};
            Clip clip;
            unsigned long long order = 0;
            bool initialized = false;
        };
        ma_engine m_engine{};
        ma_device m_device{};
        bool m_engineReady = false, m_deviceReady = false;
        std::atomic<OutputState> m_state{OutputState::Initializing};
        mutable std::mutex m_mutex;
        std::array<Voice, 16> m_voices;
        std::array<Clip, 9> m_pending;
        unsigned m_pendingCount = 0;
        std::chrono::steady_clock::time_point m_pendingSince;
        AudioStats m_stats;

        static void render(ma_device* device, void* output, const void*, ma_uint32 frames)
        {
            auto& self = *static_cast<PortableAudio*>(device->pUserData);
            auto* samples = static_cast<float*>(output);
            std::fill(samples, samples + size_t(frames) * 2, 0.f);
            // Audio callbacks must never wait on the browser UI thread.
            std::unique_lock lock(self.m_mutex, std::try_to_lock);
            if (!lock.owns_lock())
                return;
            if (self.m_state != OutputState::Ready)
                return;
            if (ma_engine_read_pcm_frames(&self.m_engine, samples, frames, nullptr) != MA_SUCCESS)
            {
                self.m_state = OutputState::Failed;
                return;
            }
            for (ma_uint32 frame = 0; frame < frames; ++frame)
            {
                const float peak = std::max(std::abs(samples[frame * 2]), std::abs(samples[frame * 2 + 1]));
                self.m_stats.peak = std::max(self.m_stats.peak, peak);
                if (peak > .00001f)
                    ++self.m_stats.nonzeroFrames;
            }
        }
        void release(Voice& voice)
        {
            if (voice.initialized)
            {
                ma_sound_uninit(&voice.sound);
                ma_audio_buffer_uninit(&voice.buffer);
                voice.initialized = false;
            }
            voice.clip.reset();
        }
        Result<> start(Clip clip)
        {
            auto found = std::find_if(m_voices.begin(), m_voices.end(), [](const Voice& v)
            {
                return !v.initialized;
            });
            if (found == m_voices.end())
            {
                found = std::min_element(m_voices.begin(), m_voices.end(), [](const Voice& a, const Voice& b)
                {
                    return a.order < b.order;
                });
                ++m_stats.stolen;
            }
            auto& voice = *found;
            release(voice);
            voice.clip = std::move(clip);
            auto config = ma_audio_buffer_config_init(ma_format_f32, 2, voice.clip->samples.size() / 2, voice.clip->samples.data(), nullptr);
            if (ma_audio_buffer_init(&config, &voice.buffer) != MA_SUCCESS)
            {
                voice.clip.reset();
                return NauMakeError("Audio voice buffer initialization failed");
            }
            if (ma_sound_init_from_data_source(&m_engine, &voice.buffer, MA_SOUND_FLAG_NO_SPATIALIZATION, nullptr, &voice.sound) != MA_SUCCESS)
            {
                ma_audio_buffer_uninit(&voice.buffer);
                voice.clip.reset();
                return NauMakeError("Audio voice initialization failed");
            }
            voice.initialized = true;
            if (ma_sound_start(&voice.sound) != MA_SUCCESS)
            {
                release(voice);
                return NauMakeError("Audio voice start failed");
            }
            voice.order = ++m_stats.started;
            return ResultSuccess;
        }

    public:
        ~PortableAudio()
        {
            close();
        }
        async::Task<> initService() override
        {
#ifdef __EMSCRIPTEN__
            // Validate browser output before entering a library call that creates a DOM context.
            // A thrown DOM constructor must become a checked failure, not strand a UI proxy.
            // clang-format off
            const bool available = MAIN_THREAD_EM_ASM_INT({
                try {
                    const AudioContextType = window.AudioContext || window.webkitAudioContext;
                    if (!AudioContextType) return 0;
                    const context = new AudioContextType({sampleRate:48000});
                    context.close().catch(() => {});
                    return 1;
                } catch (error) { console.error('Portable audio output unavailable', error); return 0; }
            });
            // clang-format on
            if (!available)
            {
                m_state = OutputState::Failed;
                return async::Task<>::makeRejected(NauMakeError("Portable audio output device initialization failed"));
            }
#endif
            auto config = ma_engine_config_init();
            config.noDevice = MA_TRUE;
            config.channels = 2;
            config.sampleRate = 48000;
            if (ma_engine_init(&config, &m_engine) != MA_SUCCESS)
            {
                m_state = OutputState::Failed;
                return async::Task<>::makeRejected(NauMakeError("Portable audio mixer initialization failed"));
            }
            m_engineReady = true;
            // Leave headroom for sixteen overlapping copies of the bundled click.
            ma_engine_set_volume(&m_engine, .2f);
            auto device = ma_device_config_init(ma_device_type_playback);
            device.playback.format = ma_format_f32;
            device.playback.channels = 2;
            device.sampleRate = 48000;
            device.dataCallback = render;
            device.pUserData = this;
            device.notificationCallback = [](const ma_device_notification* notification)
            {
                if (notification->type == ma_device_notification_type_stopped)
                {
                    auto* self = static_cast<PortableAudio*>(notification->pDevice->pUserData);
                    if (self->m_state == OutputState::Ready)
                        self->m_state = OutputState::Failed;
                }
            };
#ifdef __EMSCRIPTEN__
            const ma_backend backend = ma_backend_webaudio;
#else
            const ma_backend backend = ma_backend_wasapi;
#endif
            if (onAudioUi([&]
            {
                return ma_device_init_ex(&backend, 1, nullptr, &device, &m_device);
            }) != MA_SUCCESS)
            {
                m_state = OutputState::Failed;
                close();
                return async::Task<>::makeRejected(NauMakeError("Portable audio output device initialization failed"));
            }
            m_deviceReady = true;
#ifdef __EMSCRIPTEN__
            m_state = OutputState::AwaitingGesture;
#else
            m_state = OutputState::Ready;
#endif
            if (onAudioUi([&]
            {
                return ma_device_start(&m_device);
            }) != MA_SUCCESS)
            {
                m_state = OutputState::Failed;
                close();
                return async::Task<>::makeRejected(NauMakeError("Portable audio output device start failed"));
            }
#ifdef __EMSCRIPTEN__
            auto* host = getServiceProvider().find<IBrowserWindowHost>();
            if (!host || !bind(host->getBrowserHostId()))
            {
                close();
                return async::Task<>::makeRejected(NauMakeError("Portable audio gesture binding failed"));
            }
#endif
            NAU_LOG_INFO("Portable audio output initialized");
            return async::Task<>::makeResolved();
        }
        Result<Clip> loadClip(const std::string& path) override
        {
            auto file = getServiceProvider().get<io::IVirtualFileSystem>().openFile(path, io::AccessMode::Read, io::OpenFileMode::OpenExisting);
            if (!file || !file->getSize() || file->getSize() > 256 * 1024)
                return NauMakeError("Missing or oversized click WAV {}", path);
            io::IStreamReader::Ptr stream = file->createStream();
            std::vector<std::byte> bytes(file->getSize());
            if (!stream)
                return NauMakeError("Unreadable click WAV {}", path);
            auto read = io::copyFromStream(bytes.data(), bytes.size(), *stream);
            if (!read || *read != bytes.size())
                return NauMakeError("Truncated click WAV {}", path);
            auto clip = decodeClickWav(bytes);
            if (!clip)
                return NauMakeError("Click WAV {}: {}", path, clip.getError()->getDiagMessage());
            return *clip;
        }
        Result<> play(Clip clip) override
        {
            if (!clip || clip->samples.empty() || clip->samples.size() > 96000 || clip->samples.size() % 2)
                return NauMakeError("Invalid portable audio clip");
            std::lock_guard lock(m_mutex);
            const auto state = m_state.load();
            if (state != OutputState::Ready && state != OutputState::AwaitingGesture)
                return NauMakeError("Audio output is not available");
            ++m_stats.requests;
            if (state == OutputState::Ready)
            {
                auto result = start(std::move(clip));
                if (!result)
                    m_state = OutputState::Failed;
                return result;
            }
            if (m_pendingCount == m_pending.size())
            {
                ++m_stats.dropped;
                return ResultSuccess;
            }
            if (!m_pendingCount)
                m_pendingSince = std::chrono::steady_clock::now();
            m_pending[m_pendingCount++] = std::move(clip);
            return ResultSuccess;
        }
        void cancelPending() override
        {
            std::lock_guard lock(m_mutex);
            m_stats.dropped += m_pendingCount;
            for (auto& clip : m_pending)
                clip.reset();
            m_pendingCount = 0;
        }
        OutputState outputState() const override
        {
            return m_state.load();
        }
        AudioStats statistics() const override
        {
            std::lock_guard lock(m_mutex);
            auto stats = m_stats;
            stats.pending = m_pendingCount;
            for (const auto& voice : m_voices)
                if (voice.initialized && ma_sound_is_playing(&voice.sound))
                    ++stats.active;
            return stats;
        }
        void gamePreUpdate(std::chrono::milliseconds) override
        {
#ifdef __EMSCRIPTEN__
            // clang-format off
            const int state = MAIN_THREAD_EM_ASM_INT({
                const binding = Module['nauAudioBindings'] ?.get($0);
                if (!binding)
                    return -1;
                if (binding.cancelled)
                {
                    binding.cancelled = false;
                    return binding.context.state === 'running' ? 3 : 2;
                }
                return binding.context.state === 'running' ? 1 : binding.context.state === 'closed' ? -1 : 0;
            },
                                                     this);
            // clang-format on
            if (state >= 2)
                cancelPending();
            if (state < 0)
                m_state = OutputState::Failed;
            else if (m_state != OutputState::Failed && m_state != OutputState::Stopping && m_state != OutputState::Stopped)
                m_state = (state & 1) ? OutputState::Ready : OutputState::AwaitingGesture;
#endif
            std::lock_guard lock(m_mutex);
            for (auto& voice : m_voices)
                if (voice.initialized && ma_sound_at_end(&voice.sound))
                    release(voice);
            if (m_pendingCount && std::chrono::steady_clock::now() - m_pendingSince > std::chrono::seconds(1))
            {
                m_stats.dropped += m_pendingCount;
                m_pendingCount = 0;
                for (auto& clip : m_pending)
                    clip.reset();
            }
            if (m_state == OutputState::Ready)
            {
                for (unsigned i = 0; i < m_pendingCount; ++i)
                {
                    if (!start(std::move(m_pending[i])))
                        m_state = OutputState::Failed;
                }
                m_pendingCount = 0;
            }
        }
        async::Task<> shutdownService() override
        {
            const bool failed = m_state == OutputState::Failed;
            close();
            return failed ? async::Task<>::makeRejected(NauMakeError("Portable audio output failed")) : async::Task<>::makeResolved();
        }
        void close()
        {
            if (!m_engineReady && !m_deviceReady)
                return;
            m_state = OutputState::Stopping;
#ifdef __EMSCRIPTEN__
            MAIN_THREAD_EM_ASM({ Module['nauAudioBindings'] ?.get($0) ?.cleanup(); }, this);
#endif
            cancelPending();
            // Never hold the mixer lock across a synchronous UI proxy/device join.
            if (m_deviceReady)
            {
                onAudioUi([&]
                {
                    ma_device_uninit(&m_device);
                    return MA_SUCCESS;
                });
                m_deviceReady = false;
            }
            std::lock_guard lock(m_mutex);
            for (auto& voice : m_voices)
                release(voice);
            if (m_engineReady)
            {
                ma_engine_uninit(&m_engine);
                m_engineReady = false;
            }
            m_state = OutputState::Stopped;
        }
#ifdef __EMSCRIPTEN__
        bool bind(int hostId)
        {
            // clang-format off
            return MAIN_THREAD_EM_ASM_INT({
                const id = $0;
                const host = Module['nauPlatformHosts']?.get($1);
                const device = window.miniaudio?.get_device_by_index($2);
                if (!host?.element.isConnected || !device?.webaudio) return 0;
                const context = device.webaudio;
                const element = host.element;
                // Replace miniaudio's global unlock hooks with scoped retryable activation.
                for (const event of window.miniaudio.unlock_event_types)
                    document.removeEventListener(event, window.miniaudio.unlock, true);
                let live = true;
                let pending = false;
                let attempt = 0;
                let activationTimer = null;
                const binding = ({context, cancelled:false});
                const reset = () => {
                    binding.cancelled = true;
                    ++attempt;
                    pending = false;
                    clearTimeout(activationTimer);
                    activationTimer = null;
                };
                const gesture = event => {
                    if (!live || !event.isTrusted || document.hidden || pending || context.state === 'running') return;
                    const key = event.type === 'keydown';
                    if (key && (document.activeElement !== element || event.repeat || !['KeyW','KeyA','KeyS','KeyD','KeyQ','KeyE'].includes(event.code))) return;
                    if (!key && ((event.type !== 'mousedown' && event.pointerType !== 'mouse') || event.sourceCapabilities?.firesTouchEvents || event.button < 0 || event.button > 2)) return;
                    pending = true;
                    const currentAttempt = ++attempt;
                    activationTimer = setTimeout(() => { if (live && attempt === currentAttempt) reset(); }, 1000);
                    context.resume().then(() => {
                        if (live && attempt === currentAttempt) { pending = false; clearTimeout(activationTimer); }
                    }, error => {
                        if (live && attempt === currentAttempt) { reset(); console.warn('Audio awaits a new activation gesture', error); }
                    });
                };
                const hidden = () => { if (document.hidden) reset(); };
                const observer = new MutationObserver(() => { if (!element.isConnected) reset(); });
                const cleanup = () => {
                    if (!live) return;
                    live = false;
                    ++attempt;
                    clearTimeout(activationTimer);
                    element.removeEventListener('keydown', gesture);
                    element.removeEventListener('pointerdown', gesture);
                    element.removeEventListener('mousedown', gesture);
                    element.removeEventListener('blur', reset);
                    window.removeEventListener('blur', reset);
                    document.removeEventListener('visibilitychange', hidden);
                    observer.disconnect();
                    Module['nauAudioBindings'].delete(id);
                };
                binding.cleanup = cleanup;
                Module['nauAudioBindings'] ||= new Map(); Module['nauAudioBindings'].set(id, binding);
                try {
                    element.addEventListener('keydown', gesture); element.addEventListener('pointerdown', gesture);
                    element.addEventListener('mousedown', gesture);
                    element.addEventListener('blur', reset); window.addEventListener('blur', reset);
                    document.addEventListener('visibilitychange', hidden);
                    observer.observe(document, {subtree:true, childList:true});
                    return 1;
                } catch (error) { cleanup(); return 0; }
            }, this, hostId, m_device.webaudio.deviceIndex);
            // clang-format on
        }
#endif
    };
    class PortableAudioModule final : public DefaultModuleImpl
    {
        std::string getModuleName() override
        {
            return "Audio";
        }
        void initialize() override
        {
            NAU_MODULE_EXPORT_SERVICE(PortableAudio);
        }
    };
}  // namespace nau::audio
IMPLEMENT_MODULE(nau::audio::PortableAudioModule)

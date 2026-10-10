// Copyright 2026 N-GINN LLC. All rights reserved.
// Use of this source code is governed by a BSD-3 Clause license that can be found in the LICENSE file.
#include <gtest/gtest.h>
// Exercise the actual decoder, mixer and ownership without opening a hardware device.
#include "../src/portable_audio.cpp"

namespace nau::audio
{
    struct PortableAudioTestAccess
    {
        static void initialize(PortableAudio& audio)
        {
            auto config = ma_engine_config_init();
            config.noDevice = MA_TRUE;
            config.channels = 2;
            config.sampleRate = 48000;
            ASSERT_EQ(ma_engine_init(&config, &audio.m_engine), MA_SUCCESS);
            audio.m_engineReady = true;
            audio.m_state = OutputState::Ready;
        }
        static std::vector<float> render(PortableAudio& audio, unsigned frames)
        {
            ma_device device{};
            device.pUserData = &audio;
            std::vector<float> result(frames * 2);
            PortableAudio::render(&device, result.data(), nullptr, frames);
            return result;
        }
        static void reclaim(PortableAudio& audio)
        {
            std::lock_guard lock(audio.m_mutex);
            for (auto& voice : audio.m_voices)
                if (voice.initialized && ma_sound_at_end(&voice.sound))
                    audio.release(voice);
        }
        static void awaiting(PortableAudio& audio)
        {
            audio.m_state = OutputState::AwaitingGesture;
        }
    };
    std::vector<std::byte> wav(unsigned channels = 1, unsigned rate = 48000)
    {
        std::vector<std::byte> bytes(44 + 100 * channels * 2);
        auto* p = reinterpret_cast<unsigned char*>(bytes.data());
        auto put16 = [](unsigned char* v, unsigned n)
        {
            v[0] = n & 255;
            v[1] = (n >> 8) & 255;
        };
        auto put32 = [](unsigned char* v, unsigned n)
        {
            for (int i = 0; i < 4; ++i)
                v[i] = (n >> (8 * i)) & 255;
        };
        memcpy(p, "RIFF", 4);
        put32(p + 4, unsigned(bytes.size() - 8));
        memcpy(p + 8, "WAVEfmt ", 8);
        put32(p + 16, 16);
        put16(p + 20, 1);
        put16(p + 22, channels);
        put32(p + 24, rate);
        put32(p + 28, rate * channels * 2);
        put16(p + 32, channels * 2);
        put16(p + 34, 16);
        memcpy(p + 36, "data", 4);
        put32(p + 40, unsigned(bytes.size() - 44));
        for (size_t i = 44; i < bytes.size(); i += 2)
            put16(p + i, 1000);
        return bytes;
    }
    TEST(PortableAudio, DecodeSupportedAndRejectMalformed)
    {
        for (auto channels : {1u, 2u})
            for (auto rate : {44100u, 48000u})
            {
                auto decoded = decodeClickWav(wav(channels, rate));
                ASSERT_TRUE(decoded);
                EXPECT_GT((*decoded)->sampleCount(), 0u);
                EXPECT_LE((*decoded)->sampleCount(), 96000u);
            }
        auto bytes = wav();
        bytes.pop_back();
        EXPECT_FALSE(decodeClickWav(bytes));
        bytes = wav();
        bytes[34] = std::byte{32};
        EXPECT_FALSE(decodeClickWav(bytes));
        bytes = wav();
        bytes[16] = std::byte{255};
        EXPECT_FALSE(decodeClickWav(bytes));
        bytes = wav(3);
        EXPECT_FALSE(decodeClickWav(bytes));
        bytes.resize(256 * 1024 + 1);
        EXPECT_FALSE(decodeClickWav(bytes));
    }
    TEST(PortableAudio, RealMixerOutputAndBoundedVoiceOwnership)
    {
        PortableAudio audio;
        PortableAudioTestAccess::initialize(audio);
        auto decoded = decodeClickWav(wav());
        ASSERT_TRUE(decoded);
        std::weak_ptr<const AudioClip> weak = *decoded;
        for (int i = 0; i < 17; ++i)
            ASSERT_TRUE(audio.play(*decoded));
        EXPECT_EQ(audio.statistics().active, 16u);
        EXPECT_EQ(audio.statistics().stolen, 1u);
        decoded = NauMakeError("Release decoded owner");
        EXPECT_FALSE(weak.expired());
        auto mixed = PortableAudioTestAccess::render(audio, 2048);
        EXPECT_GT(audio.statistics().nonzeroFrames, 0u);
        EXPECT_TRUE(std::any_of(mixed.begin(), mixed.end(), [](float value)
        {
            return std::abs(value) > .001f;
        }));
        PortableAudioTestAccess::reclaim(audio);
        EXPECT_EQ(audio.statistics().active, 0u);
        EXPECT_TRUE(weak.expired());
        audio.close();
        audio.close();
        EXPECT_EQ(audio.outputState(), OutputState::Stopped);
        EXPECT_FALSE(audio.play(nullptr));
    }
    TEST(PortableAudio, ActivationQueueIsBoundedAndCancelled)
    {
        PortableAudio audio;
        PortableAudioTestAccess::initialize(audio);
        PortableAudioTestAccess::awaiting(audio);
        auto decoded = decodeClickWav(wav());
        ASSERT_TRUE(decoded);
        for (int i = 0; i < 20; ++i)
            ASSERT_TRUE(audio.play(*decoded));
        EXPECT_EQ(audio.statistics().pending, 9u);
        EXPECT_EQ(audio.statistics().dropped, 11u);
        auto silent = PortableAudioTestAccess::render(audio, 256);
        EXPECT_TRUE(std::all_of(silent.begin(), silent.end(), [](float value)
        {
            return value == 0;
        }));
        audio.cancelPending();
        EXPECT_EQ(audio.statistics().pending, 0u);
        audio.close();
        EXPECT_FALSE(audio.play(*decoded));
    }
    TEST(PortableAudio, PartialInitializationCanStopRepeatedly)
    {
        PortableAudio audio;
        EXPECT_EQ(audio.outputState(), OutputState::Initializing);
        auto decoded = decodeClickWav(wav());
        ASSERT_TRUE(decoded);
        EXPECT_FALSE(audio.play(*decoded));
        PortableAudioTestAccess::initialize(audio);
        audio.close();
        audio.close();
        EXPECT_EQ(audio.outputState(), OutputState::Stopped);
        EXPECT_EQ(audio.statistics().active, 0u);
        EXPECT_EQ(audio.statistics().pending, 0u);
    }
}  // namespace nau::audio

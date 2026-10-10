// Copyright 2026 N-GINN LLC. All rights reserved.
// Use of this source code is governed by a BSD-3 Clause license that can be found in the LICENSE file.
#pragma once

#include <memory>
#include <span>
#include <vector>

#include "nau/rtti/type_info.h"
#include "nau/utils/result.h"

namespace nau::audio
{
    enum class OutputState
    {
        Initializing,
        AwaitingGesture,
        Ready,
        Failed,
        Stopping,
        Stopped
    };
    struct AudioClip
    {
    private:
        friend class PortableAudio;
        friend Result<std::shared_ptr<const AudioClip>> decodeClickWav(std::span<const std::byte> bytes);
        // Decoded stereo float PCM at 48 kHz, shared by bounded playback voices.
        std::vector<float> samples;

    public:
        size_t sampleCount() const
        {
            return samples.size();
        }
    };
    using Clip = std::shared_ptr<const AudioClip>;
    struct AudioStats
    {
        unsigned long long requests = 0, started = 0, stolen = 0, dropped = 0, nonzeroFrames = 0;
        unsigned active = 0, pending = 0;
        float peak = 0;
    };
    Result<Clip> decodeClickWav(std::span<const std::byte> bytes);

    struct NAU_ABSTRACT_TYPE IPortableAudio
    {
        NAU_TYPEID(nau::audio::IPortableAudio)
        virtual Result<Clip> loadClip(const std::string& packagePath) = 0;
        virtual Result<> play(Clip clip) = 0;
        virtual void cancelPending() = 0;
        virtual OutputState outputState() const = 0;
        virtual AudioStats statistics() const = 0;
    };
}  // namespace nau::audio

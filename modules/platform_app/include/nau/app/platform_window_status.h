// Copyright 2026 N-GINN LLC. All rights reserved.
// Use of this source code is governed by a BSD-3 Clause license that can be found in the LICENSE file.

#pragma once
#include <cstdint>

#include "nau/async/task.h"

namespace nau
{
    // Opaque identity of the already-bound page host, for optional DOM adapters.
    struct NAU_ABSTRACT_TYPE IBrowserWindowHost : virtual IRttiObject
    {
        NAU_INTERFACE(nau::IBrowserWindowHost, IRttiObject)
        virtual uintptr_t getBrowserHostId() const = 0;
    };

    // Optional binding result. Existing native managers need not implement it.
    struct NAU_ABSTRACT_TYPE IPlatformWindowInitialization : virtual IRttiObject
    {
        NAU_INTERFACE(nau::IPlatformWindowInitialization, IRttiObject)
        virtual async::Task<> windowReady() = 0;
    };

    enum class BrowserWindowState
    {
        Initializing,
        Ready,
        Closing,
        Closed,
        Failed
    };

    struct NAU_ABSTRACT_TYPE IBrowserWindowStatus : virtual IRttiObject
    {
        NAU_INTERFACE(nau::IBrowserWindowStatus, IRttiObject)
        virtual BrowserWindowState getBrowserState() const = 0;
        virtual bool isDocumentVisible() const = 0;
    };
}  // namespace nau

// Copyright 2026 N-GINN LLC. All rights reserved.
// Use of this source code is governed by a BSD-3 Clause license that can be found in the LICENSE file.
#pragma once

#include <string>

#include "nau/async/task.h"

namespace nau::scene
{
    // Optional feature service. The packaged application and common host do not
    // need concrete graphics types to compose checked presentation startup.
    struct IPackagedScenePresenter
    {
        NAU_TYPEID(nau::scene::IPackagedScenePresenter)
        virtual ~IPackagedScenePresenter() = default;
        virtual async::Task<> preparePresentation(std::string expectedPolicy) = 0;
        virtual Result<> presentFrame() = 0;
    };
}  // namespace nau::scene

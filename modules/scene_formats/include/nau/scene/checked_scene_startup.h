// Copyright 2026 N-GINN LLC. All rights reserved.
// Use of this source code is governed by a BSD-3 Clause license that can be found in the LICENSE file.
#pragma once

#include <span>
#include <string>

#include "nau/scene/scene.h"

namespace nau::scene
{
    // Only explicitly supported, registered component types can execute.
    async::Task<IScene::Ptr> loadStartupSceneChecked(
        const std::string& path,
        std::span<const std::string> supportedTypes);
}  // namespace nau::scene

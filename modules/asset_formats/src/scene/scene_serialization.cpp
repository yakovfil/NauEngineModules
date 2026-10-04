// Copyright 2026 N-GINN LLC. All rights reserved.
// Use of this source code is governed by a BSD-3 Clause license that can be found in the LICENSE file.
#include "nau/assets/scene_serialization.h"

#include "scene_asset_container.h"
#include "scene_container_builder.h"

namespace nau
{
    std::unique_ptr<IAssetContainerBuilder> createSceneContainerBuilder()
    {
        return std::make_unique<SceneContainerBuilder>();
    }
    std::unique_ptr<IAssetContainerLoader> createSceneContainerLoader()
    {
        return std::make_unique<SceneAssetLoader>();
    }
}  // namespace nau

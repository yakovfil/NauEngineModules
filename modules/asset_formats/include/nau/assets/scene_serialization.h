// Copyright 2026 N-GINN LLC. All rights reserved.
// Use of this source code is governed by a BSD-3 Clause license that can be found in the LICENSE file.
#pragma once
#include <memory>

#include "nau/assets/asset_container.h"
#include "nau/assets/asset_container_builder.h"

namespace nau
{
    // Portable scene container support, without image, mesh or animation importers.
    std::unique_ptr<IAssetContainerBuilder> createSceneContainerBuilder();
    std::unique_ptr<IAssetContainerLoader> createSceneContainerLoader();
}  // namespace nau

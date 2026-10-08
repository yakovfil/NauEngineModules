// Copyright 2026 N-GINN LLC. All rights reserved.
// Use of this source code is governed by a BSD-3 Clause license that can be found in the LICENSE file.
#pragma once

#include <string>
#include <vector>

#include "nau/app/application_delegate.h"

namespace nau::scene
{
    struct PackagedSceneOptions
    {
        // Empty selects content.nau_pack in the platform executable/preload directory.
        std::string archivePath;
        std::string configurationPath = "/config/scene.json";
        std::string databasePath = "/assets_db/database.db";
        std::vector<std::string> supportedComponents;
        std::string runtimeProfile = "non-rendering-scene";
        std::string renderingConfiguration;
    };

    ApplicationDelegate::Ptr createPackagedSceneApplication(PackagedSceneOptions options);
}  // namespace nau::scene

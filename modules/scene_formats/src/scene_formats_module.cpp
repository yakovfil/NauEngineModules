// Copyright 2026 N-GINN LLC. All rights reserved.
// Use of this source code is governed by a BSD-3 Clause license that can be found in the LICENSE file.

#include "nau/module/module.h"
#include "nau/service/service_provider.h"
#include "scene_asset_container.h"

namespace nau
{
    class SceneFormatsModule final : public DefaultModuleImpl
    {
        string getModuleName() override
        {
            return "CoreSceneFormats";
        }

        void initialize() override
        {
            NAU_MODULE_EXPORT_SERVICE(SceneAssetLoader);
        }
    };
}  // namespace nau

IMPLEMENT_MODULE(nau::SceneFormatsModule)

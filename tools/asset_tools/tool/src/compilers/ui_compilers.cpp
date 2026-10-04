// Copyright 2024 N-GINN LLC. All rights reserved.
// Use of this source code is governed by a BSD-3 Clause license that can be found in the LICENSE file.

#include "nau/asset_tools/compilers/ui_compilers.h"
#include "nau/diag/error.h"

namespace nau::compilers
{
    nau::Result<AssetMetaInfo> UiCompiler::compile(PXR_NS::UsdStageRefPtr, const std::string&, const std::string&, const nau::UsdMetaInfo&, int)
    {
        return NauMakeError("UI assets are unsupported: the Cocos2d-backed UI implementation has been retired.");
    }
}  // namespace nau::compilers

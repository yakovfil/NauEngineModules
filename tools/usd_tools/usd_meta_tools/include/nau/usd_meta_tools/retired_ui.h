// Copyright 2024 N-GINN LLC. All rights reserved.
// Use of this source code is governed by a BSD-3 Clause license that can be found in the LICENSE file.

#pragma once

#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/usd/stage.h>

namespace nau
{
    // Authored type names remain readable even after removing the GUI schema plugin.
    inline bool containsRetiredUi(PXR_NS::UsdStagePtr stage)
    {
        if (!stage)
            return false;
        for (const auto& prim : stage->TraverseAll())
        {
            const auto& type = prim.GetTypeName().GetString();
            if (type.starts_with("NauGui") || type == "NauAssetUI")
                return true;
        }
        return false;
    }
}  // namespace nau

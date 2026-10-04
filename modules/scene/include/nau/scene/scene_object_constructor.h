// Copyright 2026 N-GINN LLC. All rights reserved.
// Use of this source code is governed by a BSD-3 Clause license that can be found in the LICENSE file.
#pragma once

#include "nau/scene/scene_object.h"

namespace nau::scene
{
    // Optional root-component extension. Consume the supplied root into its owner;
    // the scene factory invokes creation callbacks after ownership is established.
    struct ISceneObjectConstructor : virtual IRttiObject
    {
        NAU_INTERFACE(nau::scene::ISceneObjectConstructor, IRttiObject)

        virtual ObjectUniquePtr<SceneObject> constructSceneObject(ObjectUniquePtr<SceneComponent>&& root) = 0;
    };
}  // namespace nau::scene

// Copyright 2026 N-GINN LLC. All rights reserved.
// Use of this source code is governed by a BSD-3 Clause license that can be found in the LICENSE file.
#pragma once

#include "nau/serialization/runtime_value.h"

namespace nau::scene
{
    // Optional, read-only validation of the original property record, including
    // explicit nulls. Failure skips property assignment; UID and transform
    // restoration still follow the normal scene path.
    struct IComponentPropertyValidator : virtual IRttiObject
    {
        NAU_INTERFACE(nau::scene::IComponentPropertyValidator, IRttiObject)

        virtual Result<> validateComponentProperties(const RuntimeValue::Ptr& properties) const = 0;
    };
}  // namespace nau::scene

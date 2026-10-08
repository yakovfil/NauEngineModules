// Copyright 2026 N-GINN LLC. All rights reserved.
// Use of this source code is governed by a BSD-3 Clause license that can be found in the LICENSE file.
#pragma once

#include <array>
#include <map>
#include <string>

#include "nau/serialization/runtime_value.h"

namespace nau::render
{
    struct RenderingPolicy
    {
        std::map<std::string, std::string> materials;
        std::string defaultMaterial;
        std::array<float, 3> direction, ambient, directional;
        bool operator==(const RenderingPolicy&) const = default;
    };
    Result<RuntimeValue::Ptr> readRenderingJson(const std::string& path);
    Result<RenderingPolicy> parseRenderingPolicy(RuntimeValue::Ptr);
    Result<std::array<float, 3>> parseSimpleMaterial(RuntimeValue::Ptr);
}  // namespace nau::render

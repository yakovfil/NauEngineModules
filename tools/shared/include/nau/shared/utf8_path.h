// Copyright 2026 N-GINN LLC. All rights reserved.
// Use of this source code is governed by a BSD-3 Clause license that can be found in the LICENSE file.

#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace nau
{
    inline std::string pathToUtf8(const std::filesystem::path& path)
    {
        const auto value = path.generic_u8string();
        return {reinterpret_cast<const char*>(value.data()), value.size()};
    }

    inline std::filesystem::path pathFromUtf8(std::string_view path)
    {
        return std::filesystem::u8path(path);
    }
}  // namespace nau

// Copyright 2026 N-GINN LLC. All rights reserved.
// Use of this source code is governed by a BSD-3 Clause license that can be found in the LICENSE file.

#pragma once

#include <windows.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <stdexcept>
#include <string>

#include "nau/string/string_conv.h"
#include "nlohmann/json.hpp"

namespace nau
{
    inline bool needsUnicodePchRepair(const std::string& project)
    {
        return GetACP() != CP_UTF8 && std::ranges::any_of(project, [](unsigned char value)
        {
            return value >= 128;
        });
    }

    inline void prepareUnicodePchQuery(const std::filesystem::path& build)
    {
        const auto query = build / ".cmake/api/v1/query/codemodel-v2";
        std::filesystem::create_directories(query.parent_path());
        std::ofstream file(query, std::ios::app);
        if (!file)
            throw std::runtime_error("Cannot create Unicode PCH codemodel query");
    }

    inline std::string pchAnsiToUtf8(const std::string& bytes)
    {
        const int count = MultiByteToWideChar(CP_ACP, 0, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0);
        std::wstring wide(count, L'\0');
        MultiByteToWideChar(CP_ACP, 0, bytes.data(), static_cast<int>(bytes.size()), wide.data(), count);
        const auto utf8 = strings::wstringToUtf8(wide);
        return {reinterpret_cast<const char*>(utf8.data()), utf8.size()};
    }

    inline std::string pchLossyHeader(const std::string& header)
    {
        const auto wide = strings::utf8ToWString(header);
        const int count = WideCharToMultiByte(CP_ACP, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
        std::string ansi(count, '\0');
        WideCharToMultiByte(CP_ACP, 0, wide.data(), static_cast<int>(wide.size()), ansi.data(), count, nullptr, nullptr);
        return pchAnsiToUtf8(ansi);
    }

    inline size_t repairUnicodePchHeaders(const std::filesystem::path& build)
    {
        const auto reply = build / ".cmake/api/v1/reply";
        std::filesystem::path index;
        for (const auto& entry : std::filesystem::directory_iterator(reply))
        {
            const auto name = entry.path().filename().string();
            if (name.starts_with("index-") && entry.path().extension() == ".json" &&
                (index.empty() || entry.last_write_time() > std::filesystem::last_write_time(index)))
                index = entry.path();
        }
        if (index.empty())
            throw std::runtime_error("Missing Unicode PCH codemodel reply");
        const auto read = [](const std::filesystem::path& path)
        {
            std::ifstream file(path);
            return nlohmann::json::parse(file);
        };
        const auto model = read(reply / read(index).at("reply").at("codemodel-v2").at("jsonFile").get<std::string>());
        std::set<std::filesystem::path> repaired;
        for (const auto& configuration : model.at("configurations"))
        {
            for (const auto& reference : configuration.at("targets"))
            {
                const auto target = read(reply / reference.at("jsonFile").get<std::string>());
                for (const auto& group : target.value("compileGroups", nlohmann::json::array()))
                {
                    if (!group.contains("precompileHeaders"))
                        continue;
                    const auto language = group.at("language").get<std::string>();
                    if (language != "CXX" && language != "C")
                        continue;
                    const auto header = build / std::filesystem::u8path(target.at("paths").at("build").get<std::string>()) /
                                        "CMakeFiles" / (target.at("name").get<std::string>() + ".dir") /
                                        configuration.at("name").get<std::string>() / (language == "CXX" ? "cmake_pch.hxx" : "cmake_pch.h");
                    if (!std::filesystem::exists(header) || repaired.contains(header))
                        continue;
                    const auto relative = std::filesystem::weakly_canonical(header).lexically_relative(std::filesystem::weakly_canonical(build));
                    if (relative.empty() || *relative.begin() == "..")
                        throw std::runtime_error("PCH wrapper is outside selected build cache");
                    std::ifstream input(header, std::ios::binary);
                    const std::string original{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
                    input.close();
                    std::string content = original.starts_with("\xef\xbb\xbf") ? original.substr(3) : pchAnsiToUtf8(original);
                    for (const auto& declaration : group.at("precompileHeaders"))
                    {
                        const auto value = declaration.at("header").get<std::string>();
                        const auto include = [](const std::string& text)
                        {
                            return "#include " + (text.starts_with('<') || text.starts_with('"') ? text : '"' + text + '"');
                        };
                        const auto before = include(pchLossyHeader(value));
                        const auto after = include(value);
                        size_t offset = 0;
                        while ((offset = content.find(before, offset)) != std::string::npos)
                        {
                            content.replace(offset, before.size(), after);
                            offset += after.size();
                        }
                    }
                    const auto updated = "\xef\xbb\xbf" + content;
                    if (updated != original)
                    {
                        std::ofstream output(header, std::ios::binary | std::ios::trunc);
                        output.write(updated.data(), static_cast<std::streamsize>(updated.size()));
                        if (!output)
                            throw std::runtime_error("Cannot repair generated PCH wrapper");
                    }
                    repaired.insert(header);
                }
            }
        }
        return repaired.size();
    }
}  // namespace nau

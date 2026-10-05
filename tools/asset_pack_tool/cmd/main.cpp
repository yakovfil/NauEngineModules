// Copyright 2026 N-GINN LLC. All rights reserved.
// Use of this source code is governed by a BSD-3 Clause license that can be found in the LICENSE file.
#include <json/json.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#ifdef _WIN32
    #include <windows.h>
#endif
#include "nau/asset_pack/asset_pack_builder.h"
#include "nau/io/asset_pack_file_system.h"
#include "nau/io/file_system.h"

namespace
{
    int fail(std::string_view message)
    {
        std::cerr << "AssetPackToolCmd: " << message << '\n';
        return 1;
    }
    int build(const std::filesystem::path& manifest, const std::filesystem::path& destination)
    {
        std::ifstream input(manifest, std::ios::binary | std::ios::ate);
        if (!input || input.tellg() < 0 || input.tellg() > 8 * 1024 * 1024)
            return fail("cannot read manifest " + manifest.string());
        std::string text(static_cast<size_t>(input.tellg()), '\0');
        input.seekg(0);
        if (!input.read(text.data(), text.size()))
            return fail("manifest read failed");
        Json::CharReaderBuilder parser;
        parser["rejectDupKeys"] = true;
        parser["failIfExtra"] = true;
        Json::Value root;
        std::string error;
        auto reader = std::unique_ptr<Json::CharReader>(parser.newCharReader());
        if (!reader->parse(text.data(), text.data() + text.size(), &root, &error) || !root.isObject() || !root["files"].isArray())
            return fail("invalid manifest");
        std::map<std::string, std::filesystem::path> files;
        for (const auto& file : root["files"])
        {
            if (!file.isObject() || !file["source"].isString() || !file["path"].isString())
                return fail("manifest entry needs source and path strings");
            const auto rawPath = file["path"].asString();
            auto name = nau::io::normalizeAssetPackPath(rawPath);
            if (!name || name->empty() || *name != rawPath || !files.emplace(*name, manifest.parent_path() / std::filesystem::u8path(file["source"].asString())).second)
                return fail("invalid or duplicate package path " + rawPath);
        }
        std::vector<nau::PackInputFileData> content;
        for (const auto& [name, source] : files)
        {
            std::error_code ec;
            if (!std::filesystem::is_regular_file(source, ec))
                return fail("missing input " + source.string());
            content.push_back({[source]() -> nau::io::IStreamReader::Ptr
            {
                return nau::io::createNativeFileStream(source.string().c_str(), nau::io::AccessMode::Read, nau::io::OpenFileMode::OpenExisting);
            }, name});
        }
        nau::PackBuildOptions options;
        if (root.isMember("description"))
        {
            if (!root["description"].isString())
                return fail("description must be a string");
            options.description = root["description"].asString();
        }
        std::error_code ec;
        if (!destination.parent_path().empty())
            std::filesystem::create_directories(destination.parent_path(), ec);
        if (ec)
            return fail("cannot create output directory: " + ec.message());
        auto temporary = destination;
        temporary += ".staging";
        if (std::filesystem::exists(temporary, ec))
            return fail("owned staging path already exists: " + temporary.string());
        struct Cleanup
        {
            std::filesystem::path path;
            ~Cleanup()
            {
                std::error_code error;
                std::filesystem::remove(path, error);
            }
        } cleanup{temporary};
        nau::io::IStreamWriter::Ptr output = nau::io::createNativeFileStream(temporary.string().c_str(), nau::io::AccessMode::Write, nau::io::OpenFileMode::CreateNew);
        if (!output)
            return fail("cannot create staging output " + temporary.string());
        auto result = nau::buildAssetPackage(content, options, output);
        output = nullptr;
        if (!result)
            return fail(result.getError()->getMessage());
        auto length = std::filesystem::file_size(temporary, ec);
        if (ec || length > std::numeric_limits<size_t>::max())
            return fail("invalid output size");
        nau::io::IStreamReader::Ptr check = nau::io::createNativeFileStream(temporary.string().c_str(), nau::io::AccessMode::Read, nau::io::OpenFileMode::OpenExisting);
        auto index = nau::io::readAssetPackIndex(check, static_cast<size_t>(length));
        check = nullptr;
        if (!index)
            return fail(index.getError()->getMessage());
#ifdef _WIN32
        if (!MoveFileExW(temporary.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            return fail("publication failed, Windows error " + std::to_string(GetLastError()));
#else
        std::filesystem::rename(temporary, destination, ec);
        if (ec)
            return fail("publication failed: " + ec.message());
#endif
        std::cout << "Packaged " << content.size() << " files: " << destination.string() << '\n';
        return 0;
    }
}  // namespace

int main(int argc, char** argv)
{
    if (argc == 2 && std::string_view(argv[1]) == "--help")
    {
        std::cout << "AssetPackToolCmd --manifest <manifest.json> --output <archive.nau_pack>\n";
        return 0;
    }
    if (argc != 5 || std::string_view(argv[1]) != "--manifest" || std::string_view(argv[3]) != "--output")
        return fail("expected --manifest <manifest.json> --output <archive.nau_pack>");
    return build(std::filesystem::u8path(argv[2]), std::filesystem::u8path(argv[4]));
}

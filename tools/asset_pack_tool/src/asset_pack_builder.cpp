// Copyright 2026 N-GINN LLC. All rights reserved.
// Use of this source code is governed by a BSD-3 Clause license that can be found in the LICENSE file.
#include "nau/asset_pack/asset_pack_builder.h"

#include <json/json.h>

#include <array>
#include <cstdio>
#include <limits>
#include <memory>
#include <set>

#include "nau/io/asset_pack_file_system.h"

namespace nau
{
    namespace
    {
        Result<> writeExact(io::IStreamWriter& stream, const void* bytes, size_t size)
        {
            if (!size)
                return {};
            auto written = stream.write(static_cast<const std::byte*>(bytes), size);
            if (!written)
                return written.getError();
            if (*written != size)
                return NauMakeError("Asset pack writer: short write");
            return {};
        }
    }  // namespace

    Result<> buildAssetPackage(const std::vector<PackInputFileData>& content, PackBuildOptions options, io::IStreamWriter::Ptr output)
    {
        if (!output || options.version != "0.1" || options.contentType != "application/json")
            return NauMakeError("Asset pack writer: invalid output or options");
        std::unique_ptr<std::FILE, decltype(&std::fclose)> temporary(std::tmpfile(), &std::fclose);
        if (!temporary)
            return NauMakeError("Asset pack writer: temporary file creation failed");
        Json::Value root(Json::objectValue);
        root["version"] = options.version;
        root["description"] = options.description;
        root["content"] = Json::Value(Json::arrayValue);
        std::set<std::string> names;
        uint64_t total = 0;
        std::array<std::byte, 65536> buffer;
        for (const auto& input : content)
        {
            auto name = io::normalizeAssetPackPath(input.filePathInPack);
            if (!name || name->empty() || !names.insert(*name).second)
                return NauMakeError("Asset pack writer: invalid or duplicate path {}", input.filePathInPack);
            auto stream = input.stream ? input.stream() : nullptr;
            if (!stream)
                return NauMakeError("Asset pack writer: cannot open {}", *name);
            uint64_t size = 0;
            while (true)
            {
                auto read = stream->read(buffer.data(), buffer.size());
                if (!read)
                    return read.getError();
                if (*read > buffer.size())
                    return NauMakeError("Asset pack writer: invalid read count");
                if (!*read)
                    break;
                if (std::fwrite(buffer.data(), 1, *read, temporary.get()) != *read)
                    return NauMakeError("Asset pack writer: temporary write failed");
                if (*read > uint64_t(std::numeric_limits<int64_t>::max()) - total - size)
                    return NauMakeError("Asset pack writer: package exceeds supported size");
                size += *read;
            }
            Json::Value entry(Json::objectValue);
            entry["filePath"] = *name;
            entry["contentCompression"] = "";
            entry["clientSize"] = Json::UInt64(size);
            entry["blobData"]["offset"] = Json::UInt64(total);
            entry["blobData"]["size"] = Json::UInt64(size);
            root["content"].append(std::move(entry));
            total += size;
        }
        for (const auto& name : names)
            for (size_t slash = name.find('/'); slash != name.npos; slash = name.find('/', slash + 1))
                if (names.contains(name.substr(0, slash)))
                    return NauMakeError("Asset pack writer: file/directory conflict");
        Json::StreamWriterBuilder builder;
        builder["indentation"] = "";
        const std::string json = Json::writeString(builder, root);
        if (json.size() > io::AssetPackReadOptions{}.maxIndexBytes || content.size() > io::AssetPackReadOptions{}.maxEntries)
            return NauMakeError("Asset pack writer: index limit exceeded");
        const std::string header = "NauContent-Kind: nau-vfs-pack\nContent-Type: application/json\nContent-Length: " + std::to_string(json.size()) + "\n\n\n";
        NauCheckResult(writeExact(*output, header.data(), header.size()));
        NauCheckResult(writeExact(*output, json.data(), json.size()));
        if (std::fflush(temporary.get()) || std::fseek(temporary.get(), 0, SEEK_SET))
            return NauMakeError("Asset pack writer: temporary seek/flush failed");
        while (total)
        {
            const size_t count = static_cast<size_t>(std::min<uint64_t>(total, buffer.size()));
            if (std::fread(buffer.data(), 1, count, temporary.get()) != count)
                return NauMakeError("Asset pack writer: temporary read failed");
            NauCheckResult(writeExact(*output, buffer.data(), count));
            total -= count;
        }
        output->flush();
        return {};
    }

    Result<io::AssetPackIndexData> readAssetPackage(io::IStreamReader::Ptr stream)
    {
        if (!stream)
            return NauMakeError("Asset pack reader: missing stream");
        const size_t length = stream->setPosition(io::OffsetOrigin::End, 0);
        return io::readAssetPackIndex(std::move(stream), length);
    }
}  // namespace nau

// Copyright 2024 N-GINN LLC. All rights reserved.
// Use of this source code is governed by a BSD-3 Clause license that can be found in the LICENSE file.


#pragma once

#include <filesystem>
#include <map>
#include <concepts>
#include <utility>
#include "nau/shared/utf8_path.h"

#include "nau/shared/api.h"

namespace nau
{
    SHARED_API inline const char* getAssetsSubfolderDefaultName()
    {
        return "content";
    }
    SHARED_API inline const char* getAssetsDBfolderName()
    {
        return "assets_database";
    }

    SHARED_API inline const char* getAssetsDbName()
    {
		return "database.db";
    }

    SHARED_API std::string getShadersIncludeDir(const std::filesystem::path& shadersIn);

    struct FileSearchOptions
    {
        std::vector<std::string> allowedExtensions;
        std::vector<std::string> excludedExtensions;
        bool recursive = true;
    };

    struct FileInfo
    {
        std::string path;
        std::string subpath;
        std::string name;
        std::string extension;
        uintmax_t size = 0;
        std::time_t lastModified = 0;
    };

    class SHARED_API Paths
    {
    public:
        std::string getExecutablePath() const;
        std::string getAssetsPath() const;
        std::string getEnginePath() const;
        void setPath(const std::string& name, const std::string& path);

        static Paths& instance();

    private:
        friend class IJob;
        std::map<std::string, std::string> m_paths;
    };

    class SHARED_API FileSystem
    {
    public:
        bool isEmpty(const std::filesystem::path& path);
        bool exist(const std::filesystem::path& path);
        bool existInFolder(const std::filesystem::path& path, const std::string& name, bool withExtension = true);
        bool createDirectoryRecursive(const std::filesystem::path& path, std::error_code& error);
        bool copyFile(const std::filesystem::path& from, const std::filesystem::path& to);
        bool copyAll(const std::filesystem::path& from, const std::filesystem::path& to);
        bool moveFile(const std::filesystem::path& from, const std::filesystem::path& to);
        bool moveFilesInFolder(const std::filesystem::path& from, const std::filesystem::path& to);
        bool deleteFile(const std::filesystem::path& path);
        bool deleteDirectory(const std::filesystem::path& path);
        bool readFile(const std::filesystem::path& path, std::stringstream& content);
        bool readBinaryFile(const std::filesystem::path& path, std::vector<std::byte>& content);
        bool writeFile(const std::filesystem::path& path, const std::string& content);
        bool writeFile(const std::filesystem::path& path, const char* content, size_t size);
        bool writeBinaryFile(const std::filesystem::path& path, const std::vector<uint8_t>& content);
        void removeAllFilesByName(const std::filesystem::path& path, const std::string& name);
        void findAllFilesByName(const std::filesystem::path& path, const std::string& fileName, std::vector<FileInfo>& out, const std::string& excludedExtension);
        bool findAllFiles(const std::filesystem::path& path, std::vector<std::string>& out);
        bool findAllFiles(const std::filesystem::path& path, std::vector<FileInfo>& out, const FileSearchOptions& options);
        FileInfo getFileInfo(const std::filesystem::path& path);
        std::string findFirst(const std::filesystem::path& directory, const std::string& extension);
        void createFile(const std::filesystem::path& path);
        int countFiles(const std::filesystem::path& path);
        std::string findExtension(const std::filesystem::path& path, const std::string& fileName);
#define NAU_FILE_SYSTEM_UTF8_OVERLOAD(Method) \
        template <typename TPath, typename... TArgs> requires std::convertible_to<TPath, std::string_view> \
        decltype(auto) Method(TPath&& path, TArgs&&... args) \
        { return Method(pathFromUtf8(std::string_view(path)), std::forward<TArgs>(args)...); }
        NAU_FILE_SYSTEM_UTF8_OVERLOAD(isEmpty)
        NAU_FILE_SYSTEM_UTF8_OVERLOAD(exist)
        NAU_FILE_SYSTEM_UTF8_OVERLOAD(existInFolder)
        NAU_FILE_SYSTEM_UTF8_OVERLOAD(createDirectoryRecursive)
        NAU_FILE_SYSTEM_UTF8_OVERLOAD(readFile)
        NAU_FILE_SYSTEM_UTF8_OVERLOAD(readBinaryFile)
        NAU_FILE_SYSTEM_UTF8_OVERLOAD(writeFile)
        NAU_FILE_SYSTEM_UTF8_OVERLOAD(writeBinaryFile)
        NAU_FILE_SYSTEM_UTF8_OVERLOAD(removeAllFilesByName)
        NAU_FILE_SYSTEM_UTF8_OVERLOAD(findAllFilesByName)
        NAU_FILE_SYSTEM_UTF8_OVERLOAD(findAllFiles)
        NAU_FILE_SYSTEM_UTF8_OVERLOAD(getFileInfo)
        NAU_FILE_SYSTEM_UTF8_OVERLOAD(findFirst)
        NAU_FILE_SYSTEM_UTF8_OVERLOAD(createFile)
        NAU_FILE_SYSTEM_UTF8_OVERLOAD(countFiles)
        NAU_FILE_SYSTEM_UTF8_OVERLOAD(findExtension)
        NAU_FILE_SYSTEM_UTF8_OVERLOAD(deleteFile)
        NAU_FILE_SYSTEM_UTF8_OVERLOAD(deleteDirectory)
#undef NAU_FILE_SYSTEM_UTF8_OVERLOAD
        template <typename... args>
        void moveAll(const std::filesystem::path& to, args&&... params)
        {
            static_assert(sizeof...(params) > 0, "moveAll() requires at least one argument");
            (FileSystem::moveFilesInFolder(params, to), ...);
        }
    };

    class SHARED_API FileSystemExtensions
    {
    public:
        static constexpr char g_configExtension[] = ".nauproject";

        static std::string findDirectoryInUpperRecursive(const std::string& basePath, const std::string& directoryName);
        static std::filesystem::path getAssetsDirectory(const std::filesystem::path& currPath);
        static bool isGeometryAsset(const std::filesystem::path& path);
        static std::filesystem::path getRelativeAssetPath(const std::filesystem::path& currPath, bool stripExtension = false);
        static std::string resolveToNativePathContentFolder(const std::string& relativePath);
        static std::string replaceExtension(const std::string& path, const std::string& newExtension);

        template <class TPath>
        static TPath nameFromPath(TPath const& path, TPath const& sep = "/\\")
        {
            return path.substr(path.find_last_of(sep) + 1);
        }

        template <class TPath>
        static TPath removeExtension(TPath const& filename)
        {
            typename TPath::size_type const position(filename.find_last_of('.'));
            return position > 0 && position != TPath::npos ? filename.substr(0, position) : filename;
        }

        template <class TPath>
        static TPath getExtension(TPath const& filename)
        {
            typename TPath::size_type const position(filename.find_last_of('.'));
            return position != TPath::npos ? filename.substr(position) : TPath();
        }

        template <class TPath>
        static bool replace(TPath& str, TPath const& from, const TPath& to = "", int offset = 0)
        {
            size_t start_pos = str.find(from);

            if (start_pos == TPath::npos)
                return false;

            str.replace(start_pos, from.length() + offset, to);

            return true;
        }

        template <class TPath>
        static TPath getSubPath(TPath const& path, TPath const& fullPathToFile, bool stripFileName = true, std::string const& pathSeparator = "/\\")
        {
            TPath result = fullPathToFile;

            FileSystemExtensions::replace<std::string>(result, path);

            if (stripFileName)
            {
                typename TPath::size_type const position(result.find_last_of(pathSeparator));

                result = result.substr(0, position);
            }

            // Gets absolute project subpath if presented (exmp. C:/Users/username/Documents/Template/empty/source/game/main.cpp -> source/game)
            if (result.length() > 2)
            {
                result = result.substr(1);
            }

            return result;
        }
    };
}  // namespace nau

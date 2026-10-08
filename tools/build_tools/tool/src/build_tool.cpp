// Copyright 2024 N-GINN LLC. All rights reserved.
// Use of this source code is governed by a BSD-3 Clause license that can be found in the LICENSE file.

#include "nau/build_tool/build_tool.h"

#include <vector>

#include "nau/build_tool/build_config.h"
#if defined(_WIN32) || defined(_WIN64)
    #include "nau/shared/platform/win/process.h"
#elif defined(__linux__) || defined(__linux)
    #include "nau/shared/platform/linux/process.h"
#else
    #include "nau/shared/platform/mac/process.h"
#endif

#include <nau/io/special_paths.h>
#include <nau/module/module_manager.h>
#include <nau/project_tools/project_info.h>
#include <nau/service/service_provider.h>
#include <nau/shared/macro.h>
#include <nau/shared/util.h>

#include <format>
#include <fstream>

#include "nau/app/application.h"
#include "nau/app/application_services.h"
#include "nau/app/platform_window.h"
#include "nau/app/window_manager.h"
#include "nau/asset_pack/asset_pack_builder.h"
#include "nau/asset_tools/asset_api.h"
#include "nau/asset_tools/asset_manager.h"
#include "nau/asset_tools/db_manager.h"
#include "nau/build_tool/interface/build_tool.h"
#include "nau/io/memory_stream.h"
#include "nau/io/virtual_file_system.h"
#include "nau/serialization/json_utils.h"
#include "nau/serialization/serialization.h"
#include "nau/shared/file_system.h"
#include "nau/shared/logger.h"
#include "nau/shared/platform/win/utils.h"
#include "unicode_pch.h"

namespace nau
{
    static std::string utf8Path(const std::filesystem::path& path)
    {
        const auto value = path.u8string();
        return {reinterpret_cast<const char*>(value.data()), value.size()};
    }

    static std::string quoteArgument(const std::string& value)
    {
        std::string result = "\"";
        size_t slashes = 0;
        for (char character : value)
        {
            if (character == '\\') { ++slashes; continue; }
            result.append(character == '"' ? slashes * 2 + 1 : slashes, '\\');
            result += character;
            slashes = 0;
        }
        result.append(slashes * 2, '\\');
        return result + '"';
    }

    static std::string sourceEncodingCommand(const std::filesystem::path& build, const std::string& project)
    {
        std::ifstream cache(build / "CMakeCache.txt");
        std::string line;
        const std::string key = "CMAKE_CXX_FLAGS:STRING=";
        while (std::getline(cache, line))
        {
            if (!line.starts_with(key)) continue;
            auto flags = line.substr(key.size());
            if (!flags.empty() && flags.back() == '\r') flags.pop_back();
            if (flags.find("/utf-8") != std::string::npos) return {};
            flags += " /utf-8";
            return std::format("cmake -B{} -S{} {}", quoteArgument(utf8Path(build)), quoteArgument(project), quoteArgument("-DCMAKE_CXX_FLAGS=" + flags));
        }
        throw std::runtime_error("Missing CMAKE_CXX_FLAGS in native build cache");
    }

#define NAU_PROGRESS_LOG(progress) m_progressCallback(progress);
#define NAU_MKDIR(dir)                                                      \
    {                                                                       \
        std::error_code err;                                                \
        fs.createDirectoryRecursive(dir, err);                              \
        if (err)                                                            \
        {                                                                   \
            fail("Could not create build directory!", BuildResult::Failed); \
            return BuildResult::Failed;                                     \
        }                                                                   \
    }
#define NAU_ERROR(err, ...)                                              \
    {                                                                    \
        app->stop();                                                     \
        while (app->step())                                              \
        {                                                                \
            std::this_thread::yield();                                   \
        };                                                               \
        return fail(std::format(err, __VA_ARGS__), BuildResult::Failed); \
    }

#define NAU_CHECK_IS_CANCELLED       \
    if (m_cancelled)                 \
    {                                \
        LOG_INFO("Build cancelled"); \
        return;                      \
    }

    void copyDirectory(const std::filesystem::path& source, const std::filesystem::path& destination, const std::vector<std::string>& extensionsBlacklist)
    {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(source))
        {
            const auto& path = entry.path();
            const auto& destPath = destination / std::filesystem::relative(path, source);

            if (std::find(extensionsBlacklist.begin(), extensionsBlacklist.end(), path.extension().string()) != extensionsBlacklist.end())
            {
                continue;
            }

            if (std::filesystem::is_directory(path))
            {
                std::filesystem::create_directories(destPath);
            }
            else if (std::filesystem::is_regular_file(path))
            {
                std::filesystem::copy_file(path, destPath, std::filesystem::copy_options::overwrite_existing);
            }
        }
    }

    void configureVirtualFileSystem(io::IVirtualFileSystem& vfs, const BuildConfig& config)
    {
        auto contentFs = io::createNativeFileSystem(config.projectPath);
        vfs.mount("/project", std::move(contentFs)).ignore();
    }

    std::shared_ptr<IBuildTool> IBuildTool::get()
    {
        static std::shared_ptr<IBuildTool> instance = std::make_shared<BuildTool>();
        return instance;
    }

    void BuildTool::build(const BuildConfig& config, ProgressCallback& progressCallback, BuildResultCallback& resultCallback)
    {
        m_progressCallback = progressCallback;
        m_resultCallback = resultCallback;
        m_buildConfig = std::make_unique<BuildConfig>(config);
        m_failed = false;

        const std::filesystem::path pathToProject = std::filesystem::u8path(m_buildConfig->projectPath);

        NAU_ASSERT(std::filesystem::exists(pathToProject), "Project path does not exist {}", config.projectPath);
        NAU_ASSERT(nau::util::validateEnvironment(), "Invalid environment!");

        auto app = createApplication([config]
        {
            loadModulesList(NAU_MODULES_LIST).ignore();
            configureVirtualFileSystem(getServiceProvider().get<io::IVirtualFileSystem>(), config);
            return nau::ResultSuccess;
        });

        app->startupOnCurrentThread();

        FileSystem fs;

        if (!fs.exist(pathToProject) || fs.isEmpty(pathToProject))
        {
            NAU_ERROR("Project path does not exist or is empty {}", m_buildConfig->projectPath);
        }

        const std::filesystem::path targetDestination = std::filesystem::u8path(m_buildConfig->targetDestination);

        if (pathToProject.compare(targetDestination) == 0)
        {
            NAU_ERROR("Project path and target destination are the same {}!", m_buildConfig->projectPath);
        }

        NAU_CHECK_IS_CANCELLED

        NAU_PROGRESS_LOG(10);

        if (config.compileSources)
        {
            LOG_INFO("Compile project sources at path {}", m_buildConfig->projectPath);

            {
                auto result = compileSources();

                if (result != BuildResult::Success)
                {
                    NAU_ERROR("Could not compile sources at path {}", m_buildConfig->projectPath);
                }
            }
        }
        else if (config.forceCopyBinaries)
        {
            LOG_INFO("Copying project binaries at path {}", m_buildConfig->projectPath);

            const std::filesystem::path buildPath = std::filesystem::u8path(m_buildConfig->targetDestination) / "bin";
            const std::filesystem::path cmakeFiles = std::filesystem::u8path(m_buildConfig->projectPath) / "build";

            if (!fs.exist(buildPath))
            {
                std::error_code err;

                fs.createDirectoryRecursive(buildPath, err);

                if (err)
                {
                    LOG_ERROR("Could not create firectory {}!", pathToUtf8(buildPath));
                }
            }

            if (!copyBinaries(buildPath, cmakeFiles))
            {
                LOG_ERROR("Could not copy binaries from {}!", pathToUtf8(buildPath));
            }
        }
        else
        {
            LOG_INFO("Skipping project sources compilation at path {}", m_buildConfig->projectPath);
        }

        NAU_CHECK_IS_CANCELLED
        NAU_PROGRESS_LOG(50);

        if (config.compileAssets)
        {
            LOG_INFO("Compile assets at path {}", m_buildConfig->projectPath);

            {
                auto result = compileAssets();

                if (result != BuildResult::Success)
                {
                    NAU_ERROR("Could not compile assets at path {}", m_buildConfig->projectPath);
                }
            }
        }

        NAU_CHECK_IS_CANCELLED
        NAU_PROGRESS_LOG(75)

        LOG_INFO("Create package at path {}", m_buildConfig->projectPath);

        {
            auto result = createPackage();

            if (result != BuildResult::Success)
            {
                NAU_ERROR("Could not create package at path {}", m_buildConfig->projectPath);
            }
        }

        NAU_CHECK_IS_CANCELLED
        NAU_PROGRESS_LOG(95)

        LOG_INFO("Copying resources...");

        const auto stagedResources = std::filesystem::u8path(m_buildConfig->projectPath) / "build/legacy-resources";
        const std::filesystem::path resourcesContentPath = fs.exist(stagedResources) ? stagedResources : std::filesystem::u8path(m_buildConfig->projectPath) / "resources";
        const std::filesystem::path resourcesTargetDir = std::filesystem::u8path(m_buildConfig->targetDestination) / "resources";

        if (!fs.exist(resourcesContentPath))
        {
            LOG_WARN("Could not find resources folder {}", pathToUtf8(resourcesContentPath));
        }
        else
        {
            std::error_code err;
            fs.createDirectoryRecursive(resourcesTargetDir, err);

            if (err)
            {
                NAU_ERROR("Could not create resources folder {}", pathToUtf8(resourcesTargetDir));
            }
            else
            {
                copyDirectory(resourcesContentPath, resourcesTargetDir, {".nausd"});

                NAU_LOG("Resources copied to {}", pathToUtf8(resourcesTargetDir));
            }
        }

        LOG_INFO("Copying config...");

        const std::filesystem::path configPath = std::filesystem::u8path(m_buildConfig->targetDestination) / "config";
        const std::filesystem::path sourceConfigPath = std::filesystem::u8path(m_buildConfig->projectPath) / "config";

        if (!fs.exist(sourceConfigPath))
        {
            NAU_ERROR("Could not find config folder {}", pathToUtf8(configPath));
        }

        NAU_CHECK_IS_CANCELLED
        LOG_INFO("Creating config files {}", pathToUtf8(configPath));

        {
            std::error_code err;
            fs.createDirectoryRecursive(configPath, err);

            if (err)
            {
                NAU_ERROR("Could not create config folder {}", pathToUtf8(configPath));
            }
            else
            {
                for (const auto& entry : std::filesystem::directory_iterator(sourceConfigPath))
                {
                    LOG_INFO("Copying config file {}", pathToUtf8(entry.path().filename()));
                    std::filesystem::copy_file(entry.path(), configPath / entry.path().filename(), std::filesystem::copy_options::overwrite_existing);
                    if (entry.path().filename() == "vfs.json" && fs.exist(stagedResources))
                    {
                        const auto deliveredConfig = configPath / entry.path().filename();
                        std::ifstream input(deliveredConfig);
                        auto document = nlohmann::json::parse(input);
                        input.close();
                        for (auto& mount : document["app"]["vfs"]["mounts"])
                        {
                            if (mount.value("path", "") == "${projectDir}/build/legacy-resources")
                                mount["path"] = "${projectDir}/resources";
                        }
                        std::ofstream output(deliveredConfig);
                        output << document.dump(4);
                        if (!output) NAU_ERROR("Cannot write delivered resource mounts {}", utf8Path(deliveredConfig));
                    }
                }
            }
        }

        app->stop();

        while (app->step())
        {
            std::this_thread::yield();
        }

        if (!m_failed)
        {
            m_resultCallback(BuildResult::Success, "Done!");

            if (config.openAfterBuild)
            {
                IPlatformUtils::openFolder(m_buildConfig->targetDestination);
            }
        }

        success();

        NAU_PROGRESS_LOG(100)
    }

    void BuildTool::cancel()
    {
        m_cancelled = true;
    }

    bool BuildTool::failed() const
    {
        return m_failed;
    }

    bool BuildTool::compile(const BuildConfig& config, ProgressCallback& progressCallback, BuildResultCallback& resultCallback)
    {
        m_progressCallback = progressCallback;
        m_resultCallback = resultCallback;
        m_buildConfig = std::make_unique<BuildConfig>(config);
        m_failed = false;

        NAU_VERIFY(std::filesystem::exists(std::filesystem::u8path(m_buildConfig->projectPath)), "Project path is empty!");

        const std::filesystem::path cmakeFiles = std::filesystem::u8path(m_buildConfig->projectPath) / "build";
        const std::filesystem::path buildPath = std::filesystem::u8path(m_buildConfig->projectPath) / "bin";
        const bool unicodePch = needsUnicodePchRepair(m_buildConfig->projectPath);
        if (unicodePch)
            prepareUnicodePchQuery(cmakeFiles);

        {
            auto result = runProcess(std::format("cmake -B\"{}\" -S\"{}\" --preset \"{}\" -DCMAKE_INSTALL_PREFIX=\"{}\" -DNAU_CORE_TOOLS=OFF -DNAU_PACKAGE_BUILD=ON -DNAU_CORE_TESTS=OFF -DNAU_CORE_SAMPLES=OFF -DNAU_FORCE_ENABLE_SHADER_COMPILER_TOOL=ON", utf8Path(cmakeFiles), m_buildConfig->projectPath, m_buildConfig->preset, utf8Path(buildPath)));

            if (result != BuildResult::Success)
            {
                fail("Could not generate solution file!", result);
                return false;
            }

            const auto encoding = sourceEncodingCommand(cmakeFiles, m_buildConfig->projectPath);
            if (!encoding.empty() && runProcess(encoding) != BuildResult::Success)
            {
                fail("Could not configure UTF-8 source encoding", BuildResult::Failed);
                return false;
            }
            if (unicodePch)
                LOG_INFO("Repaired {} Unicode PCH wrappers", repairUnicodePchHeaders(cmakeFiles));
            LOG_INFO("Solution file generated at path {}", utf8Path(cmakeFiles));
        }

        {
            auto result = runProcess(std::format("cmake --build \"{}\" --config {}", utf8Path(cmakeFiles), m_buildConfig->buildConfiguration));

            if (result != BuildResult::Success)
            {
                fail("Could not build project!", result);
                return false;
            }

            LOG_INFO("Project built at path {}", utf8Path(cmakeFiles));
        }

        // Reuturn code
        success();

        return true;
    }

    BuildResult BuildTool::compileSources()
    {
        const std::filesystem::path buildPath = std::filesystem::u8path(m_buildConfig->targetDestination) / "bin";
        const std::filesystem::path cmakeFiles = std::filesystem::u8path(m_buildConfig->projectPath) / "build";

        const bool unicodePch = needsUnicodePchRepair(m_buildConfig->projectPath);
        if (unicodePch)
            prepareUnicodePchQuery(cmakeFiles);
        LOG_INFO("Generating solution file at path {}", pathToUtf8(buildPath));

        FileSystem fs;

        if (!fs.exist(cmakeFiles))
        {
            NAU_MKDIR(cmakeFiles);
        }

        if (fs.exist(buildPath))
        {
            fs.deleteDirectory(buildPath);
            NAU_MKDIR(buildPath);
        }
        else
        {
            NAU_MKDIR(buildPath);
        }

        {
            auto result = runProcess(std::format("cmake -B\"{}\" -S\"{}\" --preset {} -DCMAKE_INSTALL_PREFIX=\"{}\" -DNAU_CORE_TOOLS=OFF -DNAU_PACKAGE_BUILD=ON -DNAU_CORE_TESTS=OFF -DNAU_CORE_SAMPLES=OFF -DNAU_FORCE_ENABLE_SHADER_COMPILER_TOOL=ON", pathToUtf8(cmakeFiles), m_buildConfig->projectPath, m_buildConfig->preset, pathToUtf8(buildPath)));

            if (result != BuildResult::Success)
            {
                fail("Could not generate solution file!", result);
                return result;
            }

            const auto encoding = sourceEncodingCommand(cmakeFiles, m_buildConfig->projectPath);
            if (!encoding.empty() && runProcess(encoding) != BuildResult::Success)
            {
                fail("Could not configure UTF-8 source encoding", BuildResult::Failed);
                return BuildResult::Failed;
            }
            if (unicodePch)
                LOG_INFO("Repaired {} Unicode PCH wrappers", repairUnicodePchHeaders(cmakeFiles));
            LOG_INFO("Solution file generated at path {}", pathToUtf8(cmakeFiles));
        }

        {
            auto result = runProcess(std::format("cmake --build \"{}\" --config {}", pathToUtf8(cmakeFiles), m_buildConfig->buildConfiguration));

            if (result != BuildResult::Success)
            {
                fail("Could not build project!", result);
                return result;
            }

            LOG_INFO("Project built at path {}", pathToUtf8(cmakeFiles));
        }

        if (!copyBinaries(buildPath, cmakeFiles))
        {
            LOG_ERROR("Could not copy binaries from {}!", pathToUtf8(buildPath));
            return BuildResult::Failed;
        }

        return BuildResult::Success;
    }

    BuildResult BuildTool::compileAssets()
    {
        ImportAssetsArguments args;

        args.projectPath = m_buildConfig->projectPath;

        std::unique_ptr<IJob> job = std::make_unique<NauImportAssetsJob>();

        LOG_INFO("Importing assets... {}", m_buildConfig->projectPath);

        auto result = job->run(&args);

        if (result != 0)
        {
            return BuildResult::Failed;
        }

        LOG_INFO("Assets imported at path {}", m_buildConfig->projectPath);

        return BuildResult::Success;
    }

    BuildResult BuildTool::createPackage()
    {
        FileSystem fs;
        PackBuildOptions options;
        options.contentType = "application/json";
        options.description = "Assets package";
        options.version = "0.1";

        LOG_INFO("Creating package... {}", m_buildConfig->targetDestination);

        std::vector<PackInputFileData> packData;

        std::filesystem::path assetDbPath = std::filesystem::u8path(m_buildConfig->projectPath) / getAssetsDBfolderName();
        std::filesystem::path assetsPackPath = std::filesystem::u8path(m_buildConfig->targetDestination) / getAssetsSubfolderDefaultName();

        NAU_VERIFY(fs.exist(assetDbPath), "Could not find asset database at path {}", pathToUtf8(assetDbPath));

        AssetDatabaseManager& db = AssetDatabaseManager::instance();
        NAU_VERIFY(db.load(pathToUtf8(assetDbPath)), "Could not load asset database at path {}", pathToUtf8(assetDbPath));

        auto& assets = db.assets();

        const std::string assetsDbPath = std::format("{}/{}", getAssetsDBfolderName(), getAssetsDbName());

        // Add asset.db file into package
        const std::string assetDb = "project/" + assetsDbPath;
        {
            PackInputFileData& data = packData.emplace_back();
            data.filePathInPack = assetsDbPath.c_str();
            data.stream = [assetDb]()
            {
                using namespace io;
                IFileSystem& fileSystem = getServiceProvider().get<IFileSystem>();
                auto file = fileSystem.openFile(assetDb, AccessMode::Read, OpenFileMode::OpenExisting);
                if (!file)
                {
                    NAU_ASSERT(false, "Could not open file {}", assetDb);
                }
                return file->createStream();
            };
        }

        std::vector<FileInfo> subFiles;

        for (int i = 0; i < assets.size(); i++)
        {
            const AssetMetaInfo& metafile = assets[i];

            PackInputFileData& data = packData.emplace_back();

            const std::string uid = toString(metafile.uid);
            const std::filesystem::path folderPath = assetDbPath / std::filesystem::u8path(metafile.dbPath.c_str()).parent_path();
            const std::string compiledExtension = std::filesystem::u8path(metafile.dbPath.c_str()).extension().string();

            if (compiledExtension.empty())
            {
                LOG_WARN("Could not find compiled extension for asset {}", uid);
                continue;
            }

            fs.findAllFilesByName(folderPath, toString(metafile.uid), subFiles, compiledExtension);

            const std::string filePathInPack = metafile.dbPath.c_str();
            data.filePathInPack = std::format("{}/{}", "assets_database", filePathInPack).c_str();
            std::string relativePath = std::format("project/assets_database/{}", filePathInPack);
            LOG_INFO("Adding {} to package", filePathInPack);
            data.stream = [relativePath, metafile]()
            {
                using namespace io;
                IFileSystem& fileSystem = getServiceProvider().get<IFileSystem>();
                auto file = fileSystem.openFile(relativePath, AccessMode::Read, OpenFileMode::OpenExisting);
                if (!file)
                {
                    NAU_ASSERT(false, "Could not open file {}", relativePath);
                }
                return file->createStream();
            };
        }

        if (subFiles.size() > 0)
        {
            LOG_INFO("Adding subfiles {} to package", subFiles.size());

            for (int j = 0; j < subFiles.size(); j++)
            {
                PackInputFileData& subFileData = packData.emplace_back();

                const std::string fileName = subFiles[j].name;
                const std::string fileExt = subFiles[j].extension;

                // substr from last / to end
                const std::string parentPath = pathToUtf8(std::filesystem::u8path(subFiles[j].path).parent_path());
                const std::string parentIndex = parentPath.substr(parentPath.find_last_of("/"), std::string::npos);
                const std::string subfileRelativePath = std::format("project/assets_database{}/{}{}", parentIndex, fileName, fileExt);

                subFileData.filePathInPack = std::format("{}{}/{}{}", "assets_database", parentIndex, fileName, fileExt).c_str();

                LOG_INFO("Adding {}/{}{} to package", parentPath, fileName, fileExt);

                subFileData.stream = [subfileRelativePath]()
                {
                    using namespace io;
                    IFileSystem& fileSystem = getServiceProvider().get<IFileSystem>();
                    auto file = fileSystem.openFile(subfileRelativePath, AccessMode::Read, OpenFileMode::OpenExisting);
                    if (!file)
                    {
                        NAU_ASSERT(false, "Could not open file {}", subfileRelativePath);
                    }
                    return file->createStream();
                };
            }
        }

        NAU_ASSERT(assets.size() > 0, "Asset database is empty, something is wrong!");

        const std::filesystem::path packDest = assetsPackPath / "content.assets";

        NAU_MKDIR(assetsPackPath);
        fs.createFile(packDest);

        io::IStreamWriter::Ptr tempStream = io::createNativeFileStream(pathToUtf8(packDest).c_str(), io::AccessMode::Write, io::OpenFileMode::CreateAlways);
        buildAssetPackage(packData, options, tempStream).ignore();

        LOG_INFO("Package created at path {}", pathToUtf8(assetsPackPath));

        return BuildResult::Success;
    }

    void BuildTool::fail(const std::string& msg, BuildResult reason)
    {
        LOG_ERROR("Build failed: {}", msg);
        m_failed = true;
        m_resultCallback(reason, msg);
    }

    void BuildTool::success()
    {
        m_resultCallback(BuildResult::Success, "Done!");
        m_failed = false;
    }

    bool BuildTool::copyBinaries(const std::filesystem::path& buildPath, const std::filesystem::path& cMakeFiles)
    {
        FileSystem fs;

        const auto binaryPath = std::format("{}/bin/{}", m_buildConfig->projectPath, m_buildConfig->buildConfiguration);

        if (!fs.exist(binaryPath))
        {
            fail(std::format("Could not find binary at path {}", binaryPath), BuildResult::Failed);
            return false;
        }

        LOG_INFO("Copying binary from {} to {}", binaryPath, pathToUtf8(buildPath));

        fs.copyAll(std::filesystem::u8path(binaryPath), buildPath);

        LOG_INFO("Binaries generated at path {}", pathToUtf8(buildPath));

        // Read app info from .nauproject
        std::filesystem::path appConfigPath = std::filesystem::u8path(fs.findFirst(m_buildConfig->projectPath, FileSystemExtensions::g_configExtension));

        std::stringstream ss;
        fs.readFile(appConfigPath, ss);

        auto appConfig = nlohmann::json::parse(ss.str());
        auto info = std::make_shared<ProjectInfo>(appConfig.template get<ProjectInfo>());

        // TODO: Make with reading main game module from .nauproject
        const std::string exePath = std::format("{}/{}Main.{}", pathToUtf8(buildPath), info->ProjectName, "exe");

        NAU_ASSERT(exePath != "", "Could not find generated executable in path: {}", pathToUtf8(buildPath));

        const std::filesystem::path exeDir = std::filesystem::u8path(exePath);
        const std::string shortcutName = pathToUtf8(exeDir.stem()) + ".lnk";
        const std::filesystem::path lnkPath = std::filesystem::u8path(m_buildConfig->targetDestination) / shortcutName;

        LOG_INFO("Creating shortcut {}", shortcutName);

        if (!IPlatformUtils::createLink(exePath, pathToUtf8(lnkPath)))
        {
            LOG_WARN("Could not create shortcut {}", shortcutName);
        }

        return true;
    }

    BuildResult BuildTool::runProcess(std::string makeArgs)
    {

        IProcessWorker process;

        if (const int processResult = process.runProcess(makeArgs); processResult != 0)
        {
            return BuildResult::Failed;
        }

        return BuildResult::Success;
    }
}  // namespace nau

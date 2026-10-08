// Copyright 2024 N-GINN LLC. All rights reserved.
// Use of this source code is governed by a BSD-3 Clause license that can be found in the LICENSE file.

#include "nau/project_tools/project_manager.h"

#include <cstdlib>
#include <format>
#include <fstream>
#include <set>
#include <sstream>
#include <string>

#include "nau/project_tools/project_template.h"
#include "nau/project_tools/string_processor.h"
#include "nau/shared/args.h"
#include "nau/shared/error_codes.h"
#include "nau/shared/file_system.h"
#include "nau/shared/logger.h"
#include "nau/shared/util.h"

#if defined(_WIN32) || defined(_WIN64)
    #include "nau/shared/platform/win/process.h"
#elif defined(__linux__) || defined(__linux)
    #include "nau/shared/platform/linux/process.h"
#else
    #include "nau/shared/platform/mac/process.h"
#endif
#include <nau/shared/macro.h>

#include "nlohmann/json.hpp"

namespace nau
{
    static std::string utf8Path(const std::filesystem::path& path)
    {
        const auto value = path.u8string();
        return {reinterpret_cast<const char*>(value.data()), value.size()};
    }
    constexpr static char g_pattern[] = "%%\\((.*?)\\)%%";
    constexpr static int g_intent = 4;

    static bool compileShaders(const std::filesystem::path& shadersDir, FileSystem& fs)
    {
        if (std::filesystem::exists(shadersDir))
        {
            const std::filesystem::path shadersMeta = shadersDir / "meta";
            const std::filesystem::path shadersSrc = shadersDir / "src";
            const std::filesystem::path shadersOut = shadersDir / "cache";

            LOG_INFO("Making shader cache at path {}!", shadersDir.string());

            std::string shadersInclude = getShadersIncludeDir(shadersDir);
            std::replace(shadersInclude.begin(), shadersInclude.end(), '\\', '/');

            const std::string makeArgs = std::format("ShaderCompilerTool.exe -o \"{}\" -s \"{}\" -m \"{}\" -i {} -c {}",
                                                     shadersOut.string(),
                                                     shadersSrc.string(),
                                                     shadersMeta.string(),
                                                     shadersInclude,
                                                     "shader_cache.nsbc");
            IProcessWorker process;

            LOG_INFO("Compiling shaders {}", makeArgs);

            return process.runProcess(makeArgs) == 0;
        }

        LOG_WARN("Could not find shaders directory at path {}!", shadersDir.string());

        return false;
    }

    struct ProjectPreset
    {
        std::filesystem::path binaryDirectory;
        std::string generator;
        nlohmann::json variables;
    };

    static void replaceAll(std::string& value, const std::string& token, const std::string& replacement)
    {
        size_t position = 0;
        while ((position = value.find(token, position)) != std::string::npos)
        {
            value.replace(position, token.size(), replacement);
            position += replacement.size();
        }
    }

    static ProjectPreset resolvePreset(const std::filesystem::path& projectPath, const std::string& name)
    {
        std::map<std::string, nlohmann::json> presets;
        std::set<std::filesystem::path> loaded;
        std::function<void(std::filesystem::path)> load = [&](std::filesystem::path path)
        {
            path = std::filesystem::weakly_canonical(path);
            if (!loaded.insert(path).second)
                return;
            std::ifstream input(path);
            if (!input)
                throw std::runtime_error("Cannot read preset file: " + path.string());
            const auto document = nlohmann::json::parse(input);
            if (document.contains("include"))
            {
                for (const auto& include : document["include"])
                    load(path.parent_path() / include.get<std::string>());
            }
            for (const auto& preset : document.value("configurePresets", nlohmann::json::array()))
                presets.emplace(preset.at("name").get<std::string>(), preset);
        };
        load(projectPath / "CMakePresets.json");
        if (std::filesystem::exists(projectPath / "CMakeUserPresets.json"))
            load(projectPath / "CMakeUserPresets.json");
        std::set<std::string> active;
        std::function<nlohmann::json(const std::string&)> merge = [&](const std::string& key)
        {
            if (!active.insert(key).second)
                throw std::runtime_error("Preset inheritance cycle");
            const auto& preset = presets.at(key);
            nlohmann::json result = nlohmann::json::object();
            if (preset.contains("inherits"))
            {
                const auto parents = preset["inherits"].is_array() ? preset["inherits"] : nlohmann::json::array({preset["inherits"]});
                // CMake gives earlier parents precedence.
                for (auto it = parents.rbegin(); it != parents.rend(); ++it)
                    result.merge_patch(merge(it->get<std::string>()));
            }
            result.merge_patch(preset);
            active.erase(key);
            return result;
        };
        const auto selected = merge(name);
        std::string directory = selected.at("binaryDir").get<std::string>();
        replaceAll(directory, "${sourceDir}", std::filesystem::absolute(projectPath).generic_string());
        replaceAll(directory, "${sourceParentDir}", std::filesystem::absolute(projectPath).parent_path().generic_string());
        replaceAll(directory, "${sourceDirName}", projectPath.filename().string());
        replaceAll(directory, "${presetName}", name);
        size_t offset = 0;
        while ((offset = directory.find("$env{")) != std::string::npos)
        {
            const auto close = directory.find('}', offset);
            if (close == std::string::npos)
                throw std::runtime_error("Malformed environment macro");
            const auto variable = directory.substr(offset + 5, close - offset - 5);
            const auto value = std::getenv(variable.c_str());
            if (!value)
                throw std::runtime_error("Missing environment variable: " + variable);
            directory.replace(offset, close - offset + 1, value);
        }
        if (directory.find('$') != std::string::npos)
            throw std::runtime_error("Unsupported binaryDir macro; use sourceDir, presetName or environment paths");
        auto binary = std::filesystem::path(directory);
        if (binary.is_relative())
            binary = projectPath / binary;
        return {std::filesystem::absolute(binary).lexically_normal(), selected.value("generator", ""),
                selected.value("cacheVariables", nlohmann::json::object())};
    }

    static void validateCleanPath(const std::filesystem::path& projectPath, const std::filesystem::path& target)
    {
        const auto root = std::filesystem::weakly_canonical(projectPath / "build");
        if (root != (std::filesystem::weakly_canonical(projectPath) / "build"))
            throw std::runtime_error("Refusing to clean a redirected build directory");
        const auto relative = std::filesystem::weakly_canonical(target).lexically_relative(root);
        if (relative.empty() || relative == "." || *relative.begin() == ".." || relative.is_absolute())
            throw std::runtime_error("Refusing to clean outside a selected subdirectory of project build");
        const auto first = relative.begin()->string();
        if (first == "portable-host-components" || first.starts_with("prepared-") ||
            relative.generic_string() == "basic-rendering-scene" ||
            (first == "basic-rendering-scene" && std::next(relative.begin()) != relative.end() && std::next(relative.begin())->string().starts_with("prepared-")))
            throw std::runtime_error("Refusing to clean shared host/preparation outputs");
        for (auto current = target; !current.empty(); current = current.parent_path())
        {
            if (std::filesystem::is_symlink(std::filesystem::symlink_status(current)))
                throw std::runtime_error("Refusing to clean through a symbolic link");
            if (current == current.parent_path())
                break;
        }
        if (std::filesystem::exists(target))
        {
            for (const auto& entry : std::filesystem::recursive_directory_iterator(target))
            {
                if (std::filesystem::is_symlink(entry.symlink_status()))
                    throw std::runtime_error("Refusing to clean a directory containing symbolic links");
            }
        }
    }

    static int configureProject(const std::filesystem::path& projectPath, const std::string& preset, bool clean)
    {
        try
        {
            const auto selected = resolvePreset(projectPath, preset);
            if (clean)
            {
                validateCleanPath(projectPath, selected.binaryDirectory);
                auto orchestration = selected.variables.value("NAU_PROJECT_ORCHESTRATION", nlohmann::json(false));
                if (orchestration.is_object())
                    orchestration = orchestration.at("value");
                const bool portable = orchestration == true || orchestration == "ON" || orchestration == "TRUE" || orchestration == "1";
                if (portable)
                {
                    const auto platform = selected.variables.at("NAU_PROJECT_PLATFORM").get<std::string>();
                    const auto configuration = selected.variables.at("NAU_PROJECT_CONFIGURATION").get<std::string>();
                    IProcessWorker worker;
                    auto command = std::format("powershell -NoProfile -ExecutionPolicy Bypass -File \"{}\" -Platform {} -Configuration {} -CleanOnly",
                                               (projectPath / "build.ps1").string(), platform, configuration);
                    for (const auto& [cacheName, argument] : std::vector<std::pair<std::string, std::string>>{
                             {"NAU_PROJECT_DECLARATION",      "Declaration"},
                             {  "NAU_ENGINE_SOURCE_DIR",  "EngineDirectory"},
                             { "NAU_MODULES_SOURCE_DIR", "ModulesDirectory"}
                    })
                    {
                        if (!selected.variables.contains(cacheName))
                            continue;
                        auto value = selected.variables[cacheName];
                        if (value.is_object())
                            value = value.at("value");
                        if (!value.is_string())
                            throw std::runtime_error("Invalid preset path setting");
                        auto path = value.get<std::string>();
                        replaceAll(path, "${sourceDir}", projectPath.generic_string());
                        if (path.starts_with("$env{") && path.ends_with("}"))
                        {
                            const auto variable = path.substr(5, path.size() - 6);
                            const auto resolved = std::getenv(variable.c_str());
                            if (!resolved)
                                throw std::runtime_error("Missing environment variable: " + variable);
                            path = resolved;
                        }
                        if (path.find('"') != std::string::npos || path.find('$') != std::string::npos)
                            throw std::runtime_error("Unsupported preset path expression");
                        command += std::format(" -{} \"{}\"", argument, path);
                    }
                    if (worker.runProcess(command) != 0)
                        return ErrorCode::internalError;
                }
                std::filesystem::remove_all(selected.binaryDirectory);
            }
            IProcessWorker worker;
            return worker.runProcess(std::format("cmake -S \"{}\" --preset \"{}\"", projectPath.string(), preset));
        }
        catch (const std::exception& error)
        {
            LOG_ERROR("Project configuration failed: {}", error.what());
            return ErrorCode::invalidArgumentsError;
        }
    }

    static int openIde(const std::filesystem::path& projectPath, const std::string& projectName, const std::string& preset, FileSystem& fs)
    {
        try
        {
            const auto selected = resolvePreset(projectPath, preset);
            if (selected.generator.find("Visual Studio") == std::string::npos)
            {
                LOG_ERROR("This preset has no Visual Studio solution; use its generated build commands.");
                return ErrorCode::invalidArgumentsError;
            }
            const auto idePath = selected.binaryDirectory / std::format("{}.{}", projectName, util::getIdeExtension(preset));
            if (!fs.exist(idePath))
            {
                LOG_ERROR("Selected preset solution does not exist");
                return ErrorCode::invalidPathError;
            }
            const auto command = std::format("start \"\" \"{}\"", idePath.string());
            return std::system(command.c_str());
        }
        catch (const std::exception& error)
        {
            LOG_ERROR("{}", error.what());
            return ErrorCode::invalidArgumentsError;
        }
    }

    int NauInitProjectJob::run(const CommonArguments* const params)
    {
        const InitProjectArguments* args = static_cast<const InitProjectArguments*>(params);
        const std::filesystem::path pathToTemplate = std::filesystem::u8path(args->toolsPath) / "project_templates" / args->templateName;
        const std::filesystem::path projectPath = std::filesystem::u8path(args->projectPath) / args->projectName;

        FileSystem fs;
        const std::string preset = args->cMakePreset.empty()
                                       ? (fs.exist(pathToTemplate / "portable_project.json") ? "portable-windows-debug" : "win_vs2026_x64_dll")
                                       : args->cMakePreset;

        if (!fs.exist(pathToTemplate))
        {
            return result(std::format("Template not found at path {}", pathToTemplate.string()), ErrorCode::invalidPathError);
        }

        if (fs.exist(projectPath))
        {
            return result(std::format("Project already exists at path {}", projectPath.string()), ErrorCode::invalidPathError);
        }

        // Init default assets folder for project
        std::string assetsPath = utf8Path(projectPath / "assets");
        std::replace(assetsPath.begin(), assetsPath.end(), '\\', '/');

        // This is used in StringProcessor to replace regex matches
        NauProjectParams projectParams = {
            {         "ProjectPath",                          args->projectPath},
            {             "Version",                    getVersion().toString()},
            {          "AssetsPath",                                 assetsPath},
            {         "ProjectName",                          args->projectName},
            {        "TemplateName",                         args->templateName},
            {         "CMakePreset",                                     preset},
            {         "ContentOnly",          std::to_string(args->contentOnly)},
            {"GenerateSolutionFile", std::to_string(args->generateSolutionFile)}
        };

        const NauProjectTemplate projectTemplate = NauProjectTemplate(args->templateName, pathToTemplate, projectParams, &fs);

        if (!projectTemplate.isValid())
        {
            return result("Could not intialize project!", ErrorCode::invalidPathError);
        }

        StringProcessor processor;

        std::filesystem::path finalPath = std::filesystem::u8path(args->projectPath) / args->projectName;

        LOG_INFO("Creating project at path {}", utf8Path(finalPath));

        std::error_code error;

        // Load project template files
        for (auto& fileInfo : projectTemplate.getFiles())
        {
            std::filesystem::path to = finalPath / fileInfo.subpath;

            if (!fs.createDirectoryRecursive(to, error))
            {
                return result(error.message(), ErrorCode::internalError);
            }

            std::string finalName;

            if (fileInfo.rename)
            {
                finalName = args->projectName + fileInfo.extension;

                if (fileInfo.lowercase)
                {
                    processor.toLower(finalName);
                }
            }
            else
            {
                finalName = fileInfo.name;
            }

            LOG_INFO("Saving file {} at path {}", finalName, utf8Path(to));

            std::filesystem::path savePath = to / finalName;

            fs.copyFile(fileInfo.path, savePath);

            // Update file content, replace via regex
            if (fileInfo.updateContent)
            {
                std::stringstream content;

                if (!fs.readFile(savePath, content))
                {
                    return result("Could not read saved file!", ErrorCode::internalError);
                }

                // Used to be ensure that all matches are processed
                int counter = 0;

                std::string processedContent;

                try
                {
                    processedContent = processor.processRegexMatches(g_pattern, content.str(), projectParams, counter);
                }
                catch (const std::exception& e)
                {
                    return result(std::format("Could not process regex in file {}\n{}", utf8Path(savePath), e.what()), ErrorCode::internalError);
                }

                if (counter != 0)
                {
                    return result(std::format("Could not process regex in file {}", utf8Path(savePath)), ErrorCode::internalError);
                }

                LOG_INFO("Processed file {}", utf8Path(savePath));

                fs.writeFile(savePath, processedContent);

                LOG_INFO("Saved file {}", utf8Path(savePath));
            }
        }

        // Portable scaffolding needs no compiler or desktop shader tool.
        // Legacy templates retain their existing shader preparation.
        if (!fs.exist(finalPath / "portable_project.json"))
        {
            NAU_ASSERT(nau::util::checkEnvironmentVariables(), "Invalid environment!");
            if (!compileShaders(finalPath / "resources" / "shaders", fs))
                return result("Could not compile legacy shaders", ErrorCode::internalError);
            NAU_ASSERT(nau::util::validateEnvironment(), "Invalid environment!");
        }
        if (args->generateSolutionFile)
        {
            if (configureProject(finalPath, preset, false) != 0)
                return result("Could not configure selected project preset", ErrorCode::internalError);
            if (args->openIde)
                return openIde(finalPath, args->projectName, preset, fs);
        }

        return 0;
    }

    int NauRebuildProjectJob::run(const CommonArguments* const params)
    {
        const RebuildProjectArguments* args = static_cast<const RebuildProjectArguments*>(params);

        FileSystem fs;

        const auto projectPath = std::filesystem::absolute(args->projectPath);
        if (configureProject(projectPath, args->cMakePreset, true) != 0)
            return result("Could not regenerate selected project preset", ErrorCode::internalError);
        if (args->openIde)
            return openIde(projectPath, args->projectName, args->cMakePreset, fs);

        return 0;
    }

    int NauUpgradeProjectJob::run(const CommonArguments* const params)
    {
        const UpgradeProjectArguments* args = static_cast<const UpgradeProjectArguments*>(params);

        FileSystem fs;

        nlohmann::json json;
        std::stringstream ss;

        const std::filesystem::path projectPath = std::filesystem::path(args->projectPath);
        const std::string projectConfig = args->projectName + FileSystemExtensions::g_configExtension;
        const std::filesystem::path configPath = projectPath / projectConfig;

        if (!fs.exist(configPath))
        {
            return result("Could not find project config file!", ErrorCode::invalidPathError);
        }

        fs.readFile(configPath, ss);

        json = nlohmann::json::parse(ss.str());

        if (!json.is_object())
        {
            return result("Could not parse project config file!", ErrorCode::internalError);
        }

        try
        {
            const std::string projectVersion = json["ProjectVersion"].get<std::string>();

            NauVersion currentVersion = NauVersion(projectVersion);
            NauVersion newVersion = NauVersion(args->version);

            LOG_FASSERT(currentVersion >= newVersion, "Current version is greater than new version!");

            json["ProjectVersion"] = args->version;

            LOG_INFO("Upgraded project version from {} to {}", projectVersion, args->version);

            return fs.writeFile(configPath, json.dump(g_intent)) ? ErrorCode::success : ErrorCode::internalError;
        }
        catch (const std::exception& e)
        {
            return result(std::format("Failed to upgrade project config file!\n{}", e.what()), ErrorCode::internalError);
        }

        return 0;
    }

    int NauSaveProjectJob::run(const CommonArguments* const params)
    {
        const SaveProjectArguments* args = static_cast<const SaveProjectArguments*>(params);

        FileSystem fs;

        nlohmann::json newjson, config;
        std::stringstream ss;

        const std::filesystem::path projectPath = std::filesystem::u8path(args->projectPath);
        const std::string projectConfig = args->projectName + FileSystemExtensions::g_configExtension;
        const std::filesystem::path configPath = projectPath / projectConfig;

        if (!fs.exist(configPath))
        {
            return result("Could not find project config file!", ErrorCode::invalidPathError);
        }

        fs.readFile(configPath, ss);

        config = nlohmann::json::parse(ss.str());
        newjson = nlohmann::json::parse(args->config);

        if (!config.is_object() || !newjson.is_object())
        {
            return result("Could not parse project config file!", ErrorCode::internalError);
        }

        if (newjson.contains("Project"))
        {
            LOG_FASSERT(config["Project"] == newjson["Project"], "Project name does not match!");
        }

        bool requiresRecompile = false;

        try
        {
            if (newjson.contains("ProjectVersion"))
            {
                NauVersion currentVersion = NauVersion(config["ProjectVersion"].get<std::string>());
                NauVersion newVersion = NauVersion(newjson["ProjectVersion"].get<std::string>());

                LOG_FASSERT(currentVersion > newVersion, "Current version is greater than new version! Aborting...");

                requiresRecompile = currentVersion < newVersion;

                LOG_COND(requiresRecompile, "Versions do not match, asset compiler will be run...");
            }
        }
        catch (const std::exception& e)
        {
            LOG_ERROR("Failed to parse project version: {}", e.what());
            return ErrorCode::invalidArgumentsError;
        }

        util::mergeJsonRecursive(config, newjson);

        return fs.writeFile(configPath, config.dump(g_intent)) ? ErrorCode::success : ErrorCode::internalError;
    }
}  // namespace nau

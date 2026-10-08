// Copyright 2024 N-GINN LLC. All rights reserved.
// Use of this source code is governed by a BSD-3 Clause license that can be found in the LICENSE file.


#include "nau/asset_tools/asset_api.h"

#include <nau/asset_tools/asset_compiler.h>
#include <nau/module/module_manager.h>

#include "nau/app/application.h"
#include "nau/app/application_services.h"
#include "nau/app/platform_window.h"
#include "nau/app/window_manager.h"
#include "nau/asset_tools/asset_manager.h"
#include "nau/assets/asset_db.h"
#include "nau/io/virtual_file_system.h"
#include "nau/service/service_provider.h"
#include "nau/shared/error_codes.h"
#include "nau/shared/logger.h"
#include "nau/shared/utf8_path.h"
#include "nau/shared/macro.h"

namespace nau
{
    void configureVirtualFileSystem(io::IVirtualFileSystem& vfs, const std::string& projectPath)
    {
        auto contentFs = io::createNativeFileSystem(projectPath);
        vfs.mount("/project", std::move(contentFs)).ignore();
    }

    int importAssets(const ImportAssetsArguments* args)
    {
        std::string projectPath = args->projectPath;

        if (!applicationExists()) {
            auto app = createApplication([projectPath, args]() -> Result<>
            {
                NauCheckResult(loadModulesList(NAU_MODULES_LIST));
                if (!args->projectModules.empty())
                {
                    NauCheckResult(loadModulesList(args->projectModules));
                    const auto database = pathFromUtf8(projectPath) / "assets_database";
                    std::filesystem::create_directories(database);
                    auto& vfs = getServiceProvider().get<io::IVirtualFileSystem>();
                    NauCheckResult(vfs.mount("/content", io::createNativeFileSystem(
                                                             pathToUtf8(pathFromUtf8(projectPath) / "content"))));
                    NauCheckResult(vfs.mount("/assets_database", io::createNativeFileSystem(pathToUtf8(database))));
                    const auto databaseFile = database / "database.db";
                    if (std::filesystem::exists(databaseFile) && std::filesystem::file_size(databaseFile) != 0)
                        getServiceProvider().get<IAssetDB>().addAssetDB("/assets_database/database.db");
                }
                configureVirtualFileSystem(getServiceProvider().get<io::IVirtualFileSystem>(), projectPath);
                return nau::ResultSuccess;
            });
            if (!app)
                return ErrorCode::internalError;
            NAU_RUN_JOB_WITH_APP(NauImportAssetsJob, "Project successfully scanned at path {}", app);
        }

        NAU_RUN_JOB(NauImportAssetsJob, "Project successfully scanned at path {}");
    }

    ASSET_TOOL_API std::string getCompiledTargetExtensionForType(const std::string& type)
    {
        size_t pos = type.find('.');
        const std::string ext = pos != std::string::npos ? type.substr(pos + 1) : type;
        return getTargetExtension(ext);
    }

}  // namespace nau

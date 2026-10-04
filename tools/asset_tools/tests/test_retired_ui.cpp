// Copyright 2024 N-GINN LLC. All rights reserved.
// Use of this source code is governed by a BSD-3 Clause license that can be found in the LICENSE file.

#include <gtest/gtest.h>
#include <pxr/usd/usd/stage.h>

#include <filesystem>
#include <fstream>

#include "nau/asset_tools/compilers/ui_compilers.h"
#include "nau/usd_meta_tools/retired_ui.h"
#include "nau/usd_meta_tools/usd_meta_generator.h"
#include "nau/usd_meta_tools/usd_meta_info.h"

TEST(RetiredUi, RejectsCompilationBeforeOpeningOrWritingAssets)
{
    nau::compilers::UiCompiler compiler;
    nau::UsdMetaInfo info;
    info.assetPath = "missing-legacy-ui.usda";
    auto result = compiler.compile({}, "", "", info, 0);
    ASSERT_TRUE(result.isError());
    EXPECT_NE(result.getError()->getMessage().find("unsupported"), std::string::npos);
}

TEST(RetiredUi, RecognizesAuthoredTypesWithoutGuiPlugin)
{
    auto stage = PXR_NS::UsdStage::CreateInMemory();
    stage->DefinePrim(PXR_NS::SdfPath("/Scene"), PXR_NS::TfToken("Xform"));
    EXPECT_FALSE(nau::containsRetiredUi(stage));
    stage->DefinePrim(PXR_NS::SdfPath("/Scene/Label"), PXR_NS::TfToken("NauGuiLabel"));
    EXPECT_TRUE(nau::containsRetiredUi(stage));
    stage->RemovePrim(PXR_NS::SdfPath("/Scene/Label"));
    stage->DefinePrim(PXR_NS::SdfPath("/Metadata"), PXR_NS::TfToken("NauAssetUI"));
    EXPECT_TRUE(nau::containsRetiredUi(stage));
}

TEST(RetiredUi, PreservesLegacySourceAndCreatesNoCompiledOutput)
{
    const auto directory = std::filesystem::temp_directory_path() / ("nau-retired-ui-" + nau::toString(nau::Uid::generate()));
    std::filesystem::create_directory(directory);
    struct Cleanup
    {
        std::filesystem::path path;
        ~Cleanup()
        {
            std::filesystem::remove_all(path);
        }
    } cleanup{directory};
    const auto source = directory / "legacy.usda";
    const std::string bytes = "#usda 1.0\ndef NauGuiLabel \"Label\" {}\n";
    {
        std::ofstream stream(source, std::ios::binary);
        stream << bytes;
    }
    EXPECT_FALSE(nau::UsdMetaGenerator::instance().generate(source));
    nau::UsdMetaInfo info;
    info.assetPath = source.string();
    nau::compilers::UiCompiler compiler;
    EXPECT_TRUE(compiler.compile({}, (directory / "output").string(), directory.string(), info, 0).isError());
    std::ifstream stream(source, std::ios::binary);
    EXPECT_EQ(std::string(std::istreambuf_iterator<char>(stream), {}), bytes);
    EXPECT_EQ(std::distance(std::filesystem::directory_iterator(directory), std::filesystem::directory_iterator{}), 1);
}

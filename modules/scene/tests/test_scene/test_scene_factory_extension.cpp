// Copyright 2026 N-GINN LLC. All rights reserved.
// Use of this source code is governed by a BSD-3 Clause license that can be found in the LICENSE file.
#include <gtest/gtest.h>

#include "nau/assets/scene_serialization.h"
#include "nau/async/work_queue.h"
#include "nau/diag/logging.h"
#include "nau/io/memory_stream.h"
#include "nau/scene/components/internal/missing_component.h"
#include "nau/scene/scene_object_constructor.h"
#include "nau/serialization/json.h"
#include "nau/service/service_provider.h"
#include "scene_management/scene_factory_impl.h"
#include "scene_management/scene_manager_impl.h"

namespace nau::scene::test
{
    struct UnregisteredRoot
    {
        NAU_TYPEID(nau::scene::test::UnregisteredRoot)
    };
    TEST(SceneFactoryTests, OrdinaryAndMissingRootSceneAndPrefabStreamsWithoutUi)
    {
        setDefaultServiceProvider(createServiceProvider());
        getServiceProvider().addService<SceneFactoryImpl>();
        getServiceProvider().addService<SceneManagerImpl>();
        getServiceProvider().addClass<SceneComponent>();
        auto queue = WorkQueue::create();
        async::Executor::setDefault(queue);
        auto& factory = getServiceProvider().get<ISceneFactory>();
        {
            auto ordinary = factory.createSceneObject();
            EXPECT_EQ(ordinary->getComponents(false).size(), 1);
            auto missing = factory.createSceneObject(&rtti::getTypeInfo<UnregisteredRoot>());
            EXPECT_TRUE(missing->getRootComponent().is<IMissingComponent>());
            ComponentAsset missingData;
            missingData.componentTypeId = rtti::getTypeInfo<UnregisteredRoot>().getHashCode();
            missingData.uid = missing->getRootComponent().getUid();
            missingData.properties = *serialization::jsonParseString(R"({"preserved":17})");
            missing->getRootComponent().as<IMissingComponent&>().setComponentData(missingData);
            auto scene = factory.createEmptyScene();
            scene->getRoot().attachChild(std::move(ordinary));
            for (auto asset : {wrapSceneAsAsset(*scene), wrapSceneObjectAsAsset(*missing)})
            {
                auto stream = io::createMemoryStream(io::AccessMode::Read | io::AccessMode::Write);
                auto builder = createSceneContainerBuilder();
                ASSERT_TRUE(builder->writeAssetToStream(stream, asset));
                stream->setPosition(io::OffsetOrigin::Begin, 0);
                auto loader = createSceneContainerLoader();
                auto task = loader->loadFromStream(stream, AssetContentInfo{});
                for (int i = 0; !task.isReady() && i < 1000; ++i)
                    queue->poll();
                ASSERT_TRUE(task.isReady());
                auto container = *std::move(task);
                auto restoredAsset = container->getAsset("");
                auto& data = restoredAsset->as<SceneAsset&>();
                if (data.getSceneInfo().assetKind == SceneAssetKind::Scene)
                {
                    auto restored = factory.createSceneFromAsset(data);
                    EXPECT_EQ(restored->getRoot().getDirectChildObjects().size(), 1);
                }
                else
                {
                    auto restored = factory.createSceneObjectFromAsset(data);
                    EXPECT_EQ(restored->getComponents(false).size(), 1);
                    ASSERT_TRUE(restored->getRootComponent().is<IMissingComponent>());
                    ComponentAsset restoredData;
                    restored->getRootComponent().as<IMissingComponent&>().fillComponentData(restoredData);
                    EXPECT_EQ(restoredData.componentTypeId, missingData.componentTypeId);
                    auto field = restoredData.properties->as<RuntimeReadonlyDictionary&>().getValue("preserved");
                    EXPECT_EQ(*runtimeValueCast<int>(field), 17);
                }
            }
        }
        setDefaultServiceProvider(nullptr);
        async::Executor::setDefault(nullptr);
    }
}  // namespace nau::scene::test
int main(int argc, char** argv)
{
    nau::diag::setLogger(nau::diag::createLogger());
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

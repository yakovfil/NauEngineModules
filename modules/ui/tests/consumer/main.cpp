// Copyright 2026 N-GINN LLC. All rights reserved.
// Use of this source code is governed by a BSD-3 Clause license that can be found in the LICENSE file.
#include <iostream>

#include "nau/assets/scene_serialization.h"
#include "nau/async/work_queue.h"
#include "nau/diag/logging.h"
#include "nau/io/memory_stream.h"
#include "nau/module/module.h"
#include "nau/scene/scene_factory.h"
#include "nau/service/service_provider.h"
#include "nau/ui/nau_widget.h"
#include "nau/ui/nau_widget_root_component.h"

extern std::shared_ptr<nau::IModule> createModule_CoreScene();
extern std::shared_ptr<nau::IModule> createModule_NauUi();

int main()
{
    using namespace nau;
    diag::setLogger(diag::createLogger());
    setDefaultServiceProvider(createServiceProvider());
    auto queue = WorkQueue::create();
    async::Executor::setDefault(queue);
    createModule_CoreScene()->initialize();
    createModule_NauUi()->initialize();
    auto& factory = getServiceProvider().get<scene::ISceneFactory>();
    auto world = factory.createEmptyScene();
    auto widget = ui::NauWidget::create();
    const Uid uid = widget->getUid();
    widget->setName("Persisted widget");
    widget->setTranslation({3.f, 4.f, 5.f});
    widget->setVisible(false);
    if (!widget->setSize({120.f, 80.f}) || !widget->setPivot({0.f, 1.f}))
        return 1;
    widget->attachChild(factory.createSceneObject());
    world->getRoot().attachChild(std::move(widget));
    auto stream = io::createMemoryStream(io::AccessMode::Read | io::AccessMode::Write);
    auto builder = createSceneContainerBuilder();
    if (!builder->writeAssetToStream(stream, scene::wrapSceneAsAsset(*world)))
        return 2;
    stream->setPosition(io::OffsetOrigin::Begin, 0);
    auto loader = createSceneContainerLoader();
    auto load = loader->loadFromStream(stream, AssetContentInfo{});
    for (int i = 0; !load.isReady() && i < 1000; ++i)
        queue->poll();
    if (!load.isReady())
        return 3;
    auto container = *std::move(load);
    auto asset = container->getAsset("");
    auto restored = factory.createSceneFromAsset(asset->as<SceneAsset&>());
    auto children = restored->getRoot().getDirectChildObjects();
    if (children.size() != 1 || !children.front()->is<ui::NauWidget>())
        return 4;
    auto& copy = children.front()->as<ui::NauWidget&>();
    if (copy.getUid() != uid || copy.getName() != "Persisted widget" || copy.isVisible() ||
        float(copy.size().getX()) != 120.f || float(copy.pivot().getY()) != 1.f ||
        float(copy.getTranslation().getZ()) != 5.f || copy.getDirectChildObjects().size() != 1)
        return 5;
    auto clone = copy.clone();
    if (!clone->is<ui::NauWidget>() || clone->getUid() == uid)
        return 6;
    clone.reset();
    restored.reset();
    world.reset();
    setDefaultServiceProvider(nullptr);
    async::Executor::setDefault(nullptr);
    queue->poll();
    std::cout << "Widget scene-stream round trip and clone passed\n";
    return 0;
}

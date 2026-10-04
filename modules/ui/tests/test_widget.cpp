// Copyright 2026 N-GINN LLC. All rights reserved.
// Use of this source code is governed by a BSD-3 Clause license that can be found in the LICENSE file.
#include <gtest/gtest.h>

#include <limits>
#include <stdexcept>
#include <type_traits>
#include <unordered_set>

#include "nau/assets/scene_serialization.h"
#include "nau/async/work_queue.h"
#include "nau/diag/logging.h"
#include "nau/io/memory_stream.h"
#include "nau/scene/components/component_life_cycle.h"
#include "nau/scene/scene_processor.h"
#include "nau/serialization/json.h"
#include "nau/service/service_provider.h"
#include "nau/ui/nau_widget.h"
#include "nau/ui/nau_widget_root_component.h"
#include "scene_management/scene_factory_impl.h"
#include "scene_management/scene_manager_impl.h"

namespace nau::ui::test
{
    using namespace scene;
    static_assert(std::is_base_of_v<SceneObject, NauWidget>);
    static_assert(!std::is_default_constructible_v<NauWidget>);
    static_assert(!std::is_copy_constructible_v<NauWidget>);

    class WidgetTests : public testing::Test
    {
    protected:
        WorkQueue::Ptr queue;
        void resetServices(bool ui = false)
        {
            setDefaultServiceProvider(nullptr);
            setDefaultServiceProvider(createServiceProvider());
            getServiceProvider().addService<SceneFactoryImpl>();
            getServiceProvider().addService<SceneManagerImpl>();
            getServiceProvider().addClass<SceneComponent>();
            if (ui)
                registerWidgetComponents();
        }
        io::IMemoryStream::Ptr save(SceneAsset::Ptr asset)
        {
            auto stream = io::createMemoryStream(io::AccessMode::Read | io::AccessMode::Write);
            auto builder = createSceneContainerBuilder();
            auto result = builder->writeAssetToStream(stream, asset);
            if (!result)
                throw std::runtime_error(result.getError()->getMessage());
            return stream;
        }
        IAssetContainer::Ptr load(io::IMemoryStream::Ptr stream)
        {
            stream->setPosition(io::OffsetOrigin::Begin, 0);
            auto loader = createSceneContainerLoader();
            auto task = loader->loadFromStream(stream, AssetContentInfo{});
            for (int i = 0; !task.isReady() && i < 1000; ++i)
                queue->poll();
            if (!task.isReady())
                throw std::runtime_error("Scene container load did not complete while pumping the fixture executor");
            return *std::move(task);
        }
        void SetUp() override
        {
            queue = WorkQueue::create();
            async::Executor::setDefault(queue);
            resetServices();
        }
        void TearDown() override
        {
            setDefaultServiceProvider(nullptr);
            async::Executor::setDefault(nullptr);
            queue->poll();
            queue.reset();
        }
    };

    class CustomObject : public SceneObject
    {
        NAU_OBJECT(nau::ui::test::CustomObject, SceneObject)
    public:
        explicit CustomObject(ObjectUniquePtr<SceneComponent>&& root) :
            SceneObject(std::move(root))
        {
        }
    };
    class CustomRoot : public SceneComponent,
                       public ISceneObjectConstructor,
                       public IComponentEvents
    {
        NAU_OBJECT(nau::ui::test::CustomRoot, SceneComponent, ISceneObjectConstructor, IComponentEvents)
        NAU_DECLARE_DYNAMIC_OBJECT
    public:
        int created = 0;
        bool ownershipEstablished = false;
        ObjectUniquePtr<SceneObject> constructSceneObject(ObjectUniquePtr<SceneComponent>&& root) override
        {
            EXPECT_EQ(root.get(), this);
            return NauObject::classCreateInstance<CustomObject>(nullptr, std::move(root));
        }
        void onComponentCreated() override
        {
            ++created;
            ownershipEstablished = getParentObject().is<CustomObject>() && &getParentObject().getRootComponent() == this;
        }
    };
    NAU_IMPLEMENT_DYNAMIC_OBJECT(CustomRoot)

    TEST_F(WidgetTests, GenericConstructionHook)
    {
        getServiceProvider().addClass<CustomRoot>();
        auto object = getServiceProvider().get<ISceneFactory>().createSceneObject<CustomRoot>();
        ASSERT_TRUE(object->is<CustomObject>());
        auto& root = object->getRootComponent().as<CustomRoot&>();
        EXPECT_EQ(root.created, 1);
        EXPECT_TRUE(root.ownershipEstablished);
        EXPECT_EQ(object->getComponents(false).size(), 1);
    }

    TEST_F(WidgetTests, OrdinaryAndUnknownRootFallback)
    {
        auto& factory = getServiceProvider().get<ISceneFactory>();
        auto ordinary = factory.createSceneObject();
        EXPECT_FALSE(ordinary->is<NauWidget>());
        EXPECT_TRUE(ordinary->getRootComponent().is<SceneComponent>());
        auto missing = factory.createSceneObject(&rtti::getTypeInfo<NauWidgetRootComponent>());
        EXPECT_FALSE(missing->is<NauWidget>());
        EXPECT_EQ(missing->getComponents(false).size(), 1);
        auto clone = ordinary->clone();
        EXPECT_FALSE(clone->is<NauWidget>());
        auto scene = factory.createEmptyScene();
        scene->getRoot().attachChild(std::move(ordinary));
        auto asset = wrapSceneAsAsset(*scene);
        auto restored = factory.createSceneFromAsset(*asset);
        EXPECT_EQ(restored->getRoot().getDirectChildObjects().size(), 1);
    }

    TEST_F(WidgetTests, DefaultsIdentityAndFactoryRegistration)
    {
        registerWidgetComponents();
        auto widget = NauWidget::create();
        EXPECT_TRUE(widget->is<SceneObject>());
        EXPECT_TRUE(widget->is<NauObject>());
        EXPECT_EQ(widget->getParentObject(), nullptr);
        EXPECT_EQ(widget->getComponents(false).size(), 1);
        EXPECT_TRUE(widget->isVisible());
        EXPECT_FLOAT_EQ(float(widget->size().getX()), 0.f);
        EXPECT_FLOAT_EQ(float(widget->pivot().getY()), .5f);
        widget->setName("widget");
        EXPECT_EQ(widget->getName(), "widget");
        auto factoryWidget = getServiceProvider().get<ISceneFactory>().createSceneObject<NauWidgetRootComponent>();
        EXPECT_TRUE(factoryWidget->is<NauWidget>());
        EXPECT_NE(widget->getUid(), factoryWidget->getUid());
        EXPECT_EQ(factoryWidget->getComponents(false).size(), 1);
        ObjectUniquePtr<SceneObject> owner = std::move(widget);
        EXPECT_TRUE(owner->is<NauWidget>());
    }

    TEST_F(WidgetTests, MixedHierarchyVisibilityAndGeometry)
    {
        auto root = NauWidget::create();
        auto ordinary = getServiceProvider().get<ISceneFactory>().createSceneObject();
        auto* ordinaryPtr = ordinary.get();
        auto child = NauWidget::create();
        auto* childPtr = child.get();
        child->setTranslation({1.f, 2.f, 3.f});
        ordinary->attachChild(std::move(child));
        root->attachChild(std::move(ordinary));
        EXPECT_EQ(root->getDirectChildObjects().front(), ordinaryPtr);
        EXPECT_EQ(childPtr->getParentObject(), ordinaryPtr);
        root->setVisible(false);
        EXPECT_FALSE(childPtr->isEffectivelyVisible());
        root->setVisible(true);
        EXPECT_TRUE(childPtr->isEffectivelyVisible());
        auto before = childPtr->getWorldTransform().toMatrixWithScale();
        ASSERT_TRUE(root->setSize({100.f, 200.f}));
        ASSERT_TRUE(root->setPivot({-1.f, 2.f}));
        auto after = childPtr->getWorldTransform().toMatrixWithScale();
        EXPECT_FLOAT_EQ(float(before.getElem(3, 0)), float(after.getElem(3, 0)));
        EXPECT_FALSE(root->setSize({-1.f, 2.f}));
        EXPECT_FALSE(root->setPivot({std::numeric_limits<float>::infinity(), 0.f}));
        EXPECT_FLOAT_EQ(float(root->size().getX()), 100.f);
    }

    TEST_F(WidgetTests, ReparentOptionsAndDestroyingRemoval)
    {
        auto a = NauWidget::create();
        auto b = NauWidget::create();
        a->setTranslation({10.f, 0.f, 0.f});
        b->setTranslation({20.f, 0.f, 0.f});
        auto child = NauWidget::create();
        auto weak = child.getRef();
        child->setTranslation({2.f, 0.f, 0.f});
        a->attachChild(std::move(child));
        EXPECT_FLOAT_EQ(float(weak->getWorldTransform().getTranslation().getX()), 12.f);
        weak->setParent(*b);
        EXPECT_FLOAT_EQ(float(weak->getWorldTransform().getTranslation().getX()), 12.f);
        EXPECT_FLOAT_EQ(float(weak->getTranslation().getX()), -8.f);
        weak->setParent(*a, SetParentOpts::DontKeepWorldTransform);
        EXPECT_FLOAT_EQ(float(weak->getTranslation().getX()), -8.f);
        a->removeChild(weak);
        EXPECT_FALSE(weak);
        EXPECT_TRUE(a->getDirectChildObjects().empty());
    }

    TEST_F(WidgetTests, StateRestorationIsAtomicAndStrict)
    {
        auto widget = NauWidget::create();
        auto& root = widget->getRootComponent().as<NauWidgetRootComponent&>();
        ASSERT_TRUE(root.setValue("widgetState", *serialization::jsonParseString(R"({"visible":false,"size":[12,34],"pivot":[-1,2]})")));
        EXPECT_FALSE(widget->isVisible());
        EXPECT_FLOAT_EQ(float(widget->size().getX()), 12.f);
        for (auto json : {R"({"visible":true,"size":[-1,2]})", R"({"visible":1})", R"({"pivot":["1",2]})", R"({"size":null})", R"([])", R"({"size":[1,2,3]})"})
        {
            EXPECT_FALSE(root.setValue("widgetState", *serialization::jsonParseString(json)));
            EXPECT_FALSE(widget->isVisible());
            EXPECT_FLOAT_EQ(float(widget->size().getX()), 12.f);
        }
        ASSERT_TRUE(root.setValue("widgetState", *serialization::jsonParseString("{}")));
        EXPECT_TRUE(widget->isVisible());
        EXPECT_FLOAT_EQ(float(widget->size().getX()), 0.f);
        EXPECT_FLOAT_EQ(float(widget->pivot().getX()), .5f);
    }
    class CountingAllocator final : public IMemAllocator
    {
    public:
        size_t allocations = 0;
        size_t deallocations = 0;
        std::unordered_set<void*> live;

        void* allocate(size_t size) override
        {
            return m_delegate->allocate(size);
        }
        void* reallocate(void* ptr, size_t size) override
        {
            return m_delegate->reallocate(ptr, size);
        }
        void deallocate(void* ptr) override
        {
            m_delegate->deallocate(ptr);
        }
        size_t getSize(const void* ptr) const override
        {
            return m_delegate->getSize(ptr);
        }
        void* allocateAligned(size_t size, size_t alignment) override
        {
            auto* ptr = m_delegate->allocateAligned(size, alignment);
            EXPECT_TRUE(live.insert(ptr).second);
            ++allocations;
            return ptr;
        }
        void* reallocateAligned(void* ptr, size_t size, size_t alignment) override
        {
            return m_delegate->reallocateAligned(ptr, size, alignment);
        }
        void deallocateAligned(void* ptr) override
        {
            EXPECT_EQ(live.erase(ptr), 1);
            ++deallocations;
            m_delegate->deallocateAligned(ptr);
        }
        size_t getSizeAligned(const void* ptr, size_t alignment) const override
        {
            return m_delegate->getSizeAligned(ptr, alignment);
        }
        bool isAligned(const void* ptr) const override
        {
            return m_delegate->isAligned(ptr);
        }
        bool isValid(const void* ptr) const override
        {
            return m_delegate->isValid(ptr);
        }
        const char* getName() const override
        {
            return m_delegate->getName();
        }
        void setName(const char* name) override
        {
            m_delegate->setName(name);
        }

    private:
        IMemAllocator::Ptr m_delegate = getDefaultAllocator();
    };

    TEST_F(WidgetTests, AllocatorAndWeakLifetime)
    {
        CountingAllocator allocator;
        ObjectWeakRef<NauWidget> weak;
        ObjectWeakRef<SceneComponent> rootWeak;
        {
            auto widget = NauWidget::create(&allocator);
            weak = *widget;
            rootWeak = widget->getRootComponent();
            EXPECT_GE(allocator.allocations, 2);
            widget->destroy();
            EXPECT_FALSE(weak);
            EXPECT_FALSE(rootWeak);
            EXPECT_TRUE(widget);
        }
        EXPECT_TRUE(allocator.live.empty());
        EXPECT_EQ(allocator.allocations, allocator.deallocations);
    }
}  // namespace nau::ui::test
int main(int argc, char** argv)
{
    nau::diag::setLogger(nau::diag::createLogger());
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

namespace nau::ui::test
{
    class ReferenceComponent : public SceneComponent
    {
        NAU_OBJECT(nau::ui::test::ReferenceComponent, SceneComponent)
        NAU_DECLARE_DYNAMIC_OBJECT
        NAU_CLASS_FIELDS(CLASS_FIELD(marker), CLASS_FIELD(peer), CLASS_FIELD(peerRoot))
    public:
        int marker = 0;
        ObjectWeakRef<SceneObject> peer;
        ObjectWeakRef<SceneComponent> peerRoot;
    };
    NAU_IMPLEMENT_DYNAMIC_OBJECT(ReferenceComponent)

    TEST_F(WidgetTests, SceneStreamPreservesMixedTreeAndProperties)
    {
        registerWidgetComponents();
        getServiceProvider().addClass<ReferenceComponent>();
        auto& factory = getServiceProvider().get<ISceneFactory>();
        auto scene = factory.createEmptyScene();
        auto widget = NauWidget::create();
        widget->setName("panel");
        widget->setVisible(false);
        ASSERT_TRUE(widget->setSize({40.f, 60.f}));
        ASSERT_TRUE(widget->setPivot({-.5f, 1.5f}));
        widget->setTransform({
            math::Quat::rotationZ(.7f),
            {3.f, 4.f, 5.f},
            {2.f, 3.f, 4.f}
        });
        const auto uid = widget->getUid();
        const auto rootUid = widget->getRootComponent().getUid();
        auto ordinary = factory.createSceneObject();
        ordinary->setName("ordinary");
        auto& link = widget->addComponent<ReferenceComponent>();
        link.marker = 73;
        link.peer = *ordinary;
        link.peerRoot = ordinary->getRootComponent();
        widget->attachChild(std::move(ordinary));
        auto sibling = NauWidget::create();
        sibling->setName("second");
        widget->attachChild(std::move(sibling));
        scene->getRoot().attachChild(std::move(widget));
        auto bytes = save(wrapSceneAsAsset(*scene));
        auto container = load(bytes);
        auto asset = container->getAsset("");
        auto restored = factory.createSceneFromAsset(asset->as<SceneAsset&>());
        auto children = restored->getRoot().getDirectChildObjects();
        ASSERT_EQ(children.size(), 1);
        auto& panel = children.front()->as<NauWidget&>();
        EXPECT_EQ(panel.getUid(), uid);
        EXPECT_EQ(panel.getRootComponent().getUid(), rootUid);
        EXPECT_EQ(panel.getName(), "panel");
        EXPECT_FALSE(panel.isVisible());
        EXPECT_FLOAT_EQ(float(panel.size().getY()), 60.f);
        EXPECT_FLOAT_EQ(float(panel.pivot().getX()), -.5f);
        EXPECT_FLOAT_EQ(float(panel.getScale().getY()), 3.f);
        EXPECT_FLOAT_EQ(float(panel.getTranslation().getZ()), 5.f);
        auto descendants = panel.getDirectChildObjects();
        ASSERT_EQ(descendants.size(), 2);
        EXPECT_EQ(descendants[0]->getName(), "ordinary");
        EXPECT_FALSE(descendants[0]->is<NauWidget>());
        EXPECT_EQ(descendants[1]->getName(), "second");
        EXPECT_TRUE(descendants[1]->is<NauWidget>());
        auto refs = panel.getComponents(false, &rtti::getTypeInfo<ReferenceComponent>());
        ASSERT_EQ(refs.size(), 1);
        auto& restoredRef = refs.front()->as<ReferenceComponent&>();
        EXPECT_EQ(restoredRef.marker, 73);
        EXPECT_EQ(restoredRef.peer.get(), descendants[0]);
        EXPECT_EQ(restoredRef.peerRoot.get(), &descendants[0]->getRootComponent());
        auto recreated = factory.createSceneFromAsset(asset->as<SceneAsset&>(), CreateSceneOption::RecreateUid);
        auto* recreatedWidget = recreated->getRoot().getDirectChildObjects().front();
        EXPECT_NE(recreatedWidget->getUid(), uid);
        EXPECT_NE(recreatedWidget->getRootComponent().getUid(), rootUid);
    }

    TEST_F(WidgetTests, PrefabAndCloneRemapReferencesAndIdentity)
    {
        registerWidgetComponents();
        getServiceProvider().addClass<ReferenceComponent>();
        auto widget = NauWidget::create();
        auto child = NauWidget::create();
        auto& ref = widget->addComponent<ReferenceComponent>();
        ref.peer = *child;
        ref.peerRoot = child->getRootComponent();
        auto originalWeak = child.getRef();
        widget->attachChild(std::move(child));
        auto bytes = save(wrapSceneObjectAsAsset(*widget));
        auto container = load(bytes);
        auto asset = container->getAsset("");
        auto& factory = getServiceProvider().get<ISceneFactory>();
        auto first = factory.createSceneObjectFromAsset(asset->as<SceneAsset&>());
        auto second = factory.createSceneObjectFromAsset(asset->as<SceneAsset&>());
        auto clone = widget->clone();
        EXPECT_NE(first->getUid(), second->getUid());
        EXPECT_NE(first->getRootComponent().getUid(), second->getRootComponent().getUid());
        for (auto* copy : {first.get(), second.get(), clone.get()})
        {
            ASSERT_TRUE(copy->is<NauWidget>());
            EXPECT_NE(copy->getUid(), widget->getUid());
            auto* copiedChild = copy->getDirectChildObjects().front();
            auto& copiedRef = copy->getComponents(false, &rtti::getTypeInfo<ReferenceComponent>()).front()->as<ReferenceComponent&>();
            EXPECT_EQ(copiedRef.peer.get(), copiedChild);
            EXPECT_EQ(copiedRef.peerRoot.get(), &copiedChild->getRootComponent());
            EXPECT_NE(copiedRef.getUid(), ref.getUid());
            EXPECT_NE(copiedChild->getUid(), originalWeak->getUid());
        }
        widget.reset();
        EXPECT_FALSE(originalWeak);
        EXPECT_TRUE(first->getDirectChildObjects().front()->is<NauWidget>());
    }

    TEST_F(WidgetTests, MissingUiResaveRecoversWidgetState)
    {
        resetServices(true);
        auto widget = NauWidget::create();
        widget->setName("recover");
        widget->setVisible(false);
        ASSERT_TRUE(widget->setSize({123.f, 456.f}));
        ASSERT_TRUE(widget->setPivot({-2.f, 3.f}));
        const auto uid = widget->getUid();
        auto bytes = save(wrapSceneObjectAsAsset(*widget));
        widget.reset();
        resetServices(false);
        auto container = load(bytes);
        auto asset = container->getAsset("");
        auto missing = getServiceProvider().get<scene_internal::ISceneFactoryInternal>().createSceneObjectFromAssetWithOptions(asset->as<SceneAsset&>(), {});
        EXPECT_FALSE(missing->is<NauWidget>());
        auto resaved = save(wrapSceneObjectAsAsset(*missing));
        missing.reset();
        resetServices(true);
        container = load(resaved);
        asset = container->getAsset("");
        auto recovered = getServiceProvider().get<scene_internal::ISceneFactoryInternal>().createSceneObjectFromAssetWithOptions(asset->as<SceneAsset&>(), {});
        ASSERT_TRUE(recovered->is<NauWidget>());
        auto& copy = recovered->as<NauWidget&>();
        EXPECT_EQ(copy.getUid(), uid);
        EXPECT_EQ(copy.getName(), "recover");
        EXPECT_FALSE(copy.isVisible());
        EXPECT_FLOAT_EQ(float(copy.size().getY()), 456.f);
        EXPECT_FLOAT_EQ(float(copy.pivot().getX()), -2.f);
    }

    TEST_F(WidgetTests, SceneTransformParityUnderNonuniformScale)
    {
        auto& factory = getServiceProvider().get<ISceneFactory>();
        auto widget = NauWidget::create();
        auto ordinary = factory.createSceneObject();
        const math::Transform parentTransform{
            math::Quat::rotationY(.5f),
            {3.f, 2.f, 1.f},
            {2.f, 3.f, 4.f}
        };
        const math::Transform local{
            math::Quat::rotationZ(.3f),
            {1.f, 2.f, 3.f},
            {4.f, 2.f, 1.f}
        };
        widget->setTransform(parentTransform);
        ordinary->setTransform(parentTransform);
        auto wc = NauWidget::create();
        auto oc = factory.createSceneObject();
        wc->setTransform(local);
        oc->setTransform(local);
        auto* w = wc.get();
        auto* o = oc.get();
        widget->attachChild(std::move(wc));
        ordinary->attachChild(std::move(oc));
        auto wm = w->getWorldTransform().toMatrixWithScale();
        auto om = o->getWorldTransform().toMatrixWithScale();
        for (int c = 0; c < 4; ++c)
            for (int r = 0; r < 4; ++r)
                EXPECT_NEAR(float(wm.getElem(c, r)), float(om.getElem(c, r)), 1.e-5f);
    }
}  // namespace nau::ui::test

namespace nau::ui::test
{
    struct LifetimeState
    {
        int destroyed = 0;
    };
    class LifetimeComponent : public SceneComponent
    {
        NAU_OBJECT(nau::ui::test::LifetimeComponent, SceneComponent)
        NAU_DECLARE_DYNAMIC_OBJECT
    public:
        LifetimeState* state = nullptr;
        ~LifetimeComponent()
        {
            if (state)
                ++state->destroyed;
        }
    };
    NAU_IMPLEMENT_DYNAMIC_OBJECT(LifetimeComponent)

    class DelayedDeactivation : public IComponentsAsyncActivator
    {
        NAU_TYPEID(nau::ui::test::DelayedDeactivation)
        NAU_CLASS_BASE(IComponentsAsyncActivator)
    public:
        async::TaskSource<> release;
        bool pending = false;
        async::Task<> deactivateComponentsAsync(Uid, std::span<const DeactivatedComponentData>) override
        {
            if (pending)
                co_await release.getTask();
        }
    };

    TEST_F(WidgetTests, ActiveDestroyQueriesAndAsyncComponentLifetime)
    {
        registerWidgetComponents();
        getServiceProvider().addClass<LifetimeComponent>();
        getServiceProvider().addService<DelayedDeactivation>();
        auto& factory = getServiceProvider().get<ISceneFactory>();
        auto& manager = getServiceProvider().get<SceneManagerImpl>();
        auto& delayed = getServiceProvider().get<DelayedDeactivation>();
        LifetimeState state;
        auto scene = factory.createEmptyScene();
        auto widget = NauWidget::create();
        auto weak = widget.getRef();
        auto& component = widget->addComponent<LifetimeComponent>();
        component.state = &state;
        ObjectWeakRef<LifetimeComponent> componentWeak = component;
        auto query = createSingleObjectQuery(*widget);
        EXPECT_EQ(query.category, QueryObjectCategory::Object);
        scene->getRoot().attachChild(std::move(widget));
        auto activation = manager.activateScene(std::move(scene));
        for (int i = 0; !activation.isReady() && i < 1000; ++i)
        {
            queue->poll();
            manager.pollShutdown();
        }
        ASSERT_TRUE(activation.isReady());
        auto sceneWeak = *std::move(activation);
        EXPECT_EQ(manager.querySingleObject(query).get(), weak.get());
        delayed.pending = true;
        weak->destroy();
        EXPECT_FALSE(weak);
        EXPECT_FALSE(componentWeak);
        EXPECT_FALSE(manager.querySingleObject(query));
        EXPECT_EQ(state.destroyed, 0);
        delayed.release.resolve();
        for (int i = 0; state.destroyed == 0 && i < 1000; ++i)
        {
            queue->poll();
            manager.pollShutdown();
        }
        EXPECT_EQ(state.destroyed, 1);
        delayed.pending = false;
        manager.deactivateScene(sceneWeak);
        auto shutdown = manager.shutdown();
        for (int i = 0; !shutdown.isReady() && i < 1000; ++i)
        {
            queue->poll();
            manager.pollShutdown();
        }
        EXPECT_TRUE(shutdown.isReady());
    }
}  // namespace nau::ui::test

namespace nau::ui::test
{
    class AlteredWidgetAsset : public SceneAsset
    {
        NAU_CLASS_(nau::ui::test::AlteredWidgetAsset, SceneAsset)
    public:
        AlteredWidgetAsset(SceneAsset::Ptr source, RuntimeValue::Ptr properties) :
            m_source(std::move(source)),
            m_properties(std::move(properties))
        {
        }
        SceneAssetInfo getSceneInfo() const override
        {
            return m_source->getSceneInfo();
        }
        std::optional<Vector<ReferenceField>> getReferencesInfo() const override
        {
            return m_source->getReferencesInfo();
        }
        void visitScene(ISceneAssetVisitor& visitor) const override
        {
            class Rewriter : public ISceneAssetVisitor
            {
            public:
                Rewriter(ISceneAssetVisitor& target, RuntimeValue::Ptr properties) :
                    target(target),
                    properties(std::move(properties))
                {
                }
                bool visitSceneObject(Uid parent, const SceneObjectAsset& source) override
                {
                    auto copy = source;
                    copy.rootComponent.properties = properties;
                    return target.visitSceneObject(parent, copy);
                }
                bool visitSceneComponent(Uid parent, const ComponentAsset& component) override
                {
                    return target.visitSceneComponent(parent, component);
                }
                ISceneAssetVisitor& target;
                RuntimeValue::Ptr properties;
            } rewriter{visitor, m_properties};
            m_source->visitScene(rewriter);
        }

    private:
        SceneAsset::Ptr m_source;
        RuntimeValue::Ptr m_properties;
    };

    TEST_F(WidgetTests, InvalidScenePropertiesLogAndKeepDefaults)
    {
        registerWidgetComponents();
        auto widget = NauWidget::create();
        widget->setVisible(false);
        widget->setTranslation({3.f, 4.f, 5.f});
        for (auto json : {R"({"widgetState":{"visible":true,"size":[-1,2]}})", R"({"widgetState":{"visible":"false"}})", R"({"widgetState":{"pivot":[true,0]}})", R"({"widgetState":{"size":[1e99,2]}})", R"({"widgetState":null})"})
        {
            SCOPED_TRACE(json);
            auto properties = *serialization::jsonParseString(json);
            auto altered = rtti::createInstance<AlteredWidgetAsset>(wrapSceneObjectAsAsset(*widget), properties);
            auto bytes = save(altered);
            auto container = load(bytes);
            // The existing writer omits null dictionary fields. Supply that
            // malformed record directly to exercise validation of explicit null.
            nau::Ptr<> asset = std::string_view(json) == R"({"widgetState":null})" ? nau::Ptr<>{altered} : container->getAsset("");
            int diagnostics = 0;
            auto subscription = diag::getLogger().subscribe([&](const diag::LoggerMessage& message)
            {
                if (message.data.find("Fail to restore component") != std::string::npos)
                    ++diagnostics;
            });
            auto restored = getServiceProvider().get<ISceneFactory>().createSceneObjectFromAsset(asset->as<SceneAsset&>());
            ASSERT_TRUE(restored->is<NauWidget>());
            auto& copy = restored->as<NauWidget&>();
            EXPECT_TRUE(copy.isVisible());
            EXPECT_FLOAT_EQ(float(copy.size().getX()), 0.f);
            EXPECT_FLOAT_EQ(float(copy.pivot().getY()), .5f);
            EXPECT_FLOAT_EQ(float(copy.getTranslation().getZ()), 5.f);
            EXPECT_EQ(diagnostics, 1);
        }
        auto& root = widget->getRootComponent().as<NauWidgetRootComponent&>();
        for (auto json : {R"({})", R"({"widgetState":{}})", R"({"widgetState":{"size":[9,8]}})"})
        {
            auto altered = rtti::createInstance<AlteredWidgetAsset>(wrapSceneObjectAsAsset(*widget), *serialization::jsonParseString(json));
            auto container = load(save(altered));
            auto asset = container->getAsset("");
            auto restored = getServiceProvider().get<ISceneFactory>().createSceneObjectFromAsset(asset->as<SceneAsset&>());
            auto& copy = restored->as<NauWidget&>();
            EXPECT_TRUE(copy.isVisible());
            EXPECT_FLOAT_EQ(float(copy.pivot().getX()), .5f);
            EXPECT_FLOAT_EQ(float(copy.size().getX()), std::string_view(json).find("size") == std::string_view::npos ? 0.f : 9.f);
        }
        WidgetState invalid;
        invalid.size = {std::numeric_limits<float>::quiet_NaN(), 2.f};
        EXPECT_FALSE(root.setValue("widgetState", makeValueCopy(invalid)));
        EXPECT_FALSE(widget->isVisible());
        invalid.size = {1.f, 2.f};
        invalid.pivot = {0.f, std::numeric_limits<float>::infinity()};
        EXPECT_FALSE(root.setValue("widgetState", makeValueCopy(invalid)));
        EXPECT_FALSE(widget->isVisible());
        auto properties = *serialization::jsonParseString(R"({"widgetState":{"visible":true,"size":[-1,2]}})");
        EXPECT_FALSE(RuntimeValue::assign(nau::Ptr<RuntimeValue>{&root}, properties));
        EXPECT_FALSE(widget->isVisible());
        auto snapshot = root.getValue("widgetState");
        ASSERT_TRUE(snapshot->as<RuntimeReadonlyDictionary&>().setValue("visible", makeValueCopy(true)));
        EXPECT_FALSE(widget->isVisible());
    }

    TEST_F(WidgetTests, ActiveAsyncAttachmentUsesSceneOperations)
    {
        registerWidgetComponents();
        auto& factory = getServiceProvider().get<ISceneFactory>();
        auto& manager = getServiceProvider().get<SceneManagerImpl>();
        auto scene = factory.createEmptyScene();
        auto parent = NauWidget::create();
        auto parentRef = parent.getRef();
        scene->getRoot().attachChild(std::move(parent));
        auto activation = manager.activateScene(std::move(scene));
        for (int i = 0; !activation.isReady() && i < 1000; ++i)
        {
            queue->poll();
            manager.pollShutdown();
        }
        ASSERT_TRUE(activation.isReady());
        auto sceneRef = *std::move(activation);
        auto child = NauWidget::create();
        auto childRef = child.getRef();
        auto attachment = parentRef->attachChildAsync(std::move(child));
        for (int i = 0; !attachment.isReady() && i < 1000; ++i)
        {
            queue->poll();
            manager.pollShutdown();
        }
        ASSERT_TRUE(attachment.isReady());
        EXPECT_EQ(childRef->getParentObject(), parentRef.get());
        EXPECT_EQ(childRef->getActivationState(), ActivationState::Active);
        parentRef->setVisible(false);
        EXPECT_FALSE(childRef->isEffectivelyVisible());
        EXPECT_EQ(childRef->getActivationState(), ActivationState::Active);
        manager.deactivateScene(sceneRef);
        EXPECT_FALSE(parentRef);
        EXPECT_FALSE(childRef);
        auto shutdown = manager.shutdown();
        for (int i = 0; !shutdown.isReady() && i < 1000; ++i)
        {
            queue->poll();
            manager.pollShutdown();
        }
        EXPECT_TRUE(shutdown.isReady());
    }
}  // namespace nau::ui::test

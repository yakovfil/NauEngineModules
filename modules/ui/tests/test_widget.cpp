// Copyright 2026 N-GINN LLC. All rights reserved.
// Use of this source code is governed by a BSD-3 Clause license that can be found in the LICENSE file.
#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <limits>
#include <numbers>
#include <type_traits>
#include <unordered_set>
#include <utility>

#include "nau/ui/nau_widget.h"

namespace nau::ui::test
{
    static_assert(!std::is_default_constructible_v<NauWidget>);
    static_assert(!std::is_copy_constructible_v<NauWidget>);
    static_assert(!std::is_copy_assignable_v<NauWidget>);
    static_assert(!std::is_move_constructible_v<NauWidget>);
    static_assert(!std::is_move_assignable_v<NauWidget>);
    static_assert(std::is_same_v<decltype(std::declval<const NauWidget&>().parent()), const NauWidget*>);
    static_assert(std::is_same_v<decltype(std::declval<const NauWidget&>().childAt(0)), const NauWidget*>);
    static_assert(std::is_same_v<decltype(std::declval<NauWidget&>().localTransform()), const math::Transform&>);
    static_assert(std::is_same_v<decltype(std::declval<const NauWidget&>().size()), math::Vector2>);
    static_assert(std::is_same_v<decltype(std::declval<const NauWidget&>().pivot()), math::Vector2>);

    void expectMatrixNear(const math::Matrix4& actual, const math::Matrix4& expected)
    {
        for (int column = 0; column < 4; ++column)
        {
            for (int row = 0; row < 4; ++row)
            {
                EXPECT_NEAR(float(actual.getElem(column, row)), float(expected.getElem(column, row)), 1.e-4f);
            }
        }
    }

    void expectPointNear(const math::Vector4& actual, const math::Vector4& expected)
    {
        for (int i = 0; i < 4; ++i)
        {
            EXPECT_NEAR(float(actual.getElem(i)), float(expected.getElem(i)), 1.e-4f);
        }
    }

    TEST(WidgetTests, SpatialDefaultsAndValidatedUpdates)
    {
        auto widget = NauWidget::create();
        const auto uid = widget->getUid();
        expectMatrixNear(widget->localTransform().toMatrixWithScale(), math::Matrix4::identity());
        expectMatrixNear(widget->accumulatedMatrix(), math::Matrix4::identity());
        EXPECT_FLOAT_EQ(float(widget->size().getX()), 0.f);
        EXPECT_FLOAT_EQ(float(widget->size().getY()), 0.f);
        EXPECT_FLOAT_EQ(float(widget->pivot().getX()), 0.5f);
        EXPECT_FLOAT_EQ(float(widget->pivot().getY()), 0.5f);

        const math::Transform valid{
            math::Quat{0.f, 0.f, 0.f, 1.f},
            {3.f, 4.f, 5.f},
            {-2.f, 0.f, 3.f}
        };
        ASSERT_TRUE(widget->setLocalTransform(valid));
        ASSERT_TRUE(widget->setSize({200.f, 100.f}));
        ASSERT_TRUE(widget->setPivot({2.f, -1.f}));
        EXPECT_FLOAT_EQ(float(widget->pivot().getX()), 2.f);
        EXPECT_FLOAT_EQ(float(widget->pivot().getY()), -1.f);
        const auto expected = math::Matrix4::translation({3.f, 4.f, 5.f}) * math::Matrix4::scale({-2.f, 0.f, 3.f});
        expectMatrixNear(widget->accumulatedMatrix(), expected);
        expectPointNear(widget->accumulatedMatrix() * math::Vector4{1.f, 2.f, 3.f, 1.f}, {1.f, 4.f, 14.f, 1.f});

        for (float invalid : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity()})
        {
            for (int component = 0; component < 3; ++component)
            {
                auto candidate = valid;
                candidate.getTranslation().setElem(component, invalid);
                EXPECT_FALSE(widget->setLocalTransform(candidate));
                candidate = valid;
                candidate.getScale().setElem(component, invalid);
                EXPECT_FALSE(widget->setLocalTransform(candidate));
            }
            for (int component = 0; component < 4; ++component)
            {
                auto candidate = valid;
                candidate.getRotation().setElem(component, invalid);
                EXPECT_FALSE(widget->setLocalTransform(candidate));
            }
            EXPECT_FALSE(widget->setSize({invalid, 10.f}));
            EXPECT_FALSE(widget->setSize({10.f, invalid}));
            EXPECT_FALSE(widget->setPivot({invalid, 0.f}));
            EXPECT_FALSE(widget->setPivot({0.f, invalid}));
        }
        EXPECT_FALSE(widget->setSize({-1.f, 10.f}));
        EXPECT_FALSE(widget->setSize({10.f, -1.f}));
        auto invalidRotation = valid;
        invalidRotation.setRotation({0.f, 0.f, 0.f, 0.f});
        EXPECT_FALSE(widget->setLocalTransform(invalidRotation));
        invalidRotation.setRotation({0.f, 0.f, 0.f, 2.f});
        EXPECT_FALSE(widget->setLocalTransform(invalidRotation));
        expectMatrixNear(widget->localTransform().toMatrixWithScale(), expected);
        EXPECT_FLOAT_EQ(float(widget->size().getX()), 200.f);
        EXPECT_FLOAT_EQ(float(widget->size().getY()), 100.f);
        EXPECT_FLOAT_EQ(float(widget->pivot().getX()), 2.f);
        EXPECT_FLOAT_EQ(float(widget->pivot().getY()), -1.f);
        EXPECT_EQ(widget->getUid(), uid);

        auto nearUnit = valid;
        nearUnit.setRotation({0.f, 0.f, 0.f, 1.0001f});
        ASSERT_TRUE(widget->setLocalTransform(nearUnit));
        EXPECT_FLOAT_EQ(float(widget->localTransform().getRotation().getW()), 1.0001f);
        ASSERT_TRUE(widget->setSize({0.f, 0.f}));
    }

    TEST(WidgetTests, AccumulatedMatricesPreserveShearAndFull3DRotation)
    {
        auto root = NauWidget::create();
        auto child = NauWidget::create();
        auto leaf = NauWidget::create();
        auto* childAddress = child.get();
        auto* leafAddress = leaf.get();
        const float quarterTurn = std::numbers::pi_v<float> / 2.f;
        const float eighthTurn = std::numbers::pi_v<float> / 4.f;
        ASSERT_TRUE(root->setLocalTransform({
            math::Quat::rotationX(quarterTurn),
            {10.f, 20.f, 30.f},
            { 2.f,  3.f,  4.f}
        }));
        ASSERT_TRUE(child->setLocalTransform({
            math::Quat::rotationZ(eighthTurn),
            {1.f, 2.f, 3.f},
            {1.f, 1.f, 1.f}
        }));
        ASSERT_TRUE(leaf->setLocalTransform({
            math::Quat::rotationY(quarterTurn),
            {-1.f, 0.f, 2.f},
            {-1.f, 0.f, 2.f}
        }));
        ASSERT_TRUE(child->addChild(std::move(leaf)));
        ASSERT_TRUE(root->addChild(std::move(child)));
        const auto rootMatrix = math::Matrix4::translation({10.f, 20.f, 30.f}) * math::Matrix4::rotationX(quarterTurn) * math::Matrix4::scale({2.f, 3.f, 4.f});
        const auto childMatrix = math::Matrix4::translation({1.f, 2.f, 3.f}) * math::Matrix4::rotationZ(eighthTurn);
        const auto leafMatrix = math::Matrix4::translation({-1.f, 0.f, 2.f}) * math::Matrix4::rotationY(quarterTurn) * math::Matrix4::scale({-1.f, 0.f, 2.f});
        expectMatrixNear(root->accumulatedMatrix(), rootMatrix);
        expectMatrixNear(childAddress->accumulatedMatrix(), rootMatrix * childMatrix);
        expectMatrixNear(leafAddress->accumulatedMatrix(), rootMatrix * childMatrix * leafMatrix);
        expectPointNear(childAddress->accumulatedMatrix() * math::Vector4{0.f, 0.f, 0.f, 1.f}, {12.f, 8.f, 36.f, 1.f});
        const auto sheared = childAddress->accumulatedMatrix();
        const auto x = sheared.getCol0();
        const auto y = sheared.getCol1();
        // Nonorthogonal basis columns demonstrate shear that TRS decomposition would lose.
        const float basisDot = float(x.getX()) * float(y.getX()) + float(x.getY()) * float(y.getY()) + float(x.getZ()) * float(y.getZ());
        EXPECT_NEAR(basisDot, 2.5f, 1.e-4f);
    }

    TEST(WidgetTests, GeometryAndHierarchyChangesPreserveLocalState)
    {
        auto root = NauWidget::create();
        auto child = NauWidget::create();
        auto leaf = NauWidget::create();
        auto other = NauWidget::create();
        auto* childAddress = child.get();
        auto* leafAddress = leaf.get();
        const auto uid = child->getUid();
        const math::Transform childTransform{
            math::Quat{0.f, 0.f, 0.f, 1.f},
            {20.f, 10.f, 3.f}
        };
        ASSERT_TRUE(child->setLocalTransform(childTransform));
        ASSERT_TRUE(child->setSize({200.f, 100.f}));
        ASSERT_TRUE(leaf->setLocalTransform({
            math::Quat{0.f, 0.f, 0.f, 1.f},
            {2.f, 3.f, 4.f}
        }));
        ASSERT_TRUE(child->addChild(std::move(leaf)));
        ASSERT_TRUE(root->addChild(std::move(child)));
        const auto before = leafAddress->accumulatedMatrix();
        ASSERT_TRUE(root->setSize({400.f, 200.f}));
        ASSERT_TRUE(root->setPivot({2.f, -1.f}));
        ASSERT_TRUE(childAddress->setPivot({0.f, 1.f}));
        expectMatrixNear(leafAddress->accumulatedMatrix(), before);
        expectMatrixNear(childAddress->localTransform().toMatrixWithScale(), childTransform.toMatrixWithScale());
        // Rectangle coordinates are derived from public size/pivot, with no child-origin offset.
        EXPECT_FLOAT_EQ(-float(childAddress->pivot().getX()) * float(childAddress->size().getX()), 0.f);
        EXPECT_FLOAT_EQ(-float(childAddress->pivot().getY()) * float(childAddress->size().getY()), -100.f);
        ASSERT_TRUE(childAddress->setPivot({0.5f, 0.5f}));
        EXPECT_FLOAT_EQ(-float(childAddress->pivot().getX()) * float(childAddress->size().getX()), -100.f);
        EXPECT_FLOAT_EQ((1.f - float(childAddress->pivot().getY())) * float(childAddress->size().getY()), 50.f);
        EXPECT_FLOAT_EQ(-float(root->pivot().getX()) * float(root->size().getX()), -800.f);
        EXPECT_FLOAT_EQ((1.f - float(root->pivot().getY())) * float(root->size().getY()), 400.f);

        ASSERT_TRUE(root->setLocalTransform({
            math::Quat{0.f, 0.f, 0.f, 1.f},
            {100.f, 200.f, 300.f}
        }));
        expectPointNear(leafAddress->accumulatedMatrix() * math::Vector4{0.f, 0.f, 0.f, 1.f}, {122.f, 213.f, 307.f, 1.f});
        const auto rootBeforeRejection = root->accumulatedMatrix();
        const auto leafBeforeRejection = leafAddress->accumulatedMatrix();
        EXPECT_FALSE(leafAddress->addChild(std::move(root)));
        EXPECT_FALSE(root->addChild(std::move(root)));
        EXPECT_FALSE(root->removeChild(*leafAddress));
        expectMatrixNear(root->accumulatedMatrix(), rootBeforeRejection);
        expectMatrixNear(leafAddress->accumulatedMatrix(), leafBeforeRejection);
        child = root->removeChild(*childAddress);
        ASSERT_TRUE(child);
        expectPointNear(leafAddress->accumulatedMatrix() * math::Vector4{0.f, 0.f, 0.f, 1.f}, {22.f, 13.f, 7.f, 1.f});
        ASSERT_TRUE(other->setLocalTransform({
            math::Quat{0.f, 0.f, 0.f, 1.f},
            {-10.f, -20.f, -30.f}
        }));
        ASSERT_TRUE(other->addChild(std::move(child)));
        expectPointNear(leafAddress->accumulatedMatrix() * math::Vector4{0.f, 0.f, 0.f, 1.f}, {12.f, -7.f, -23.f, 1.f});
        expectMatrixNear(childAddress->localTransform().toMatrixWithScale(), childTransform.toMatrixWithScale());
        EXPECT_FLOAT_EQ(float(childAddress->size().getX()), 200.f);
        EXPECT_FLOAT_EQ(float(childAddress->size().getY()), 100.f);
        EXPECT_FLOAT_EQ(float(childAddress->pivot().getX()), 0.5f);
        EXPECT_FLOAT_EQ(float(childAddress->pivot().getY()), 0.5f);
        EXPECT_EQ(childAddress->getUid(), uid);
    }

    TEST(WidgetTests, DefaultsAndIdentity)
    {
        auto firstOwner = NauWidget::create();
        auto secondOwner = NauWidget::create();
        auto& first = *firstOwner;
        auto& second = *secondOwner;
        EXPECT_NE(first.getUid(), second.getUid());
        EXPECT_NE(first.getUid(), NullUid);
        EXPECT_TRUE(first.is<scene::NauObject>());
        EXPECT_EQ(first.as<scene::NauObject*>(), static_cast<scene::NauObject*>(&first));
        EXPECT_TRUE(first.debugName().empty());
        EXPECT_TRUE(first.isVisible());
        EXPECT_TRUE(first.isEffectivelyVisible());
        EXPECT_EQ(first.parent(), nullptr);
        EXPECT_EQ(first.childCount(), 0);
        EXPECT_EQ(first.childAt(0), nullptr);
        first.setDebugName("root");
        EXPECT_EQ(first.debugName(), "root");
    }

    TEST(WidgetTests, OrderedAttachmentAndBorrowedAccess)
    {
        auto parentOwner = NauWidget::create();
        auto& parent = *parentOwner;
        std::array<NauWidget*, 3> children{};
        for (auto& pointer : children)
        {
            auto child = NauWidget::create();
            pointer = child.get();
            ASSERT_TRUE(parent.addChild(std::move(child)));
            EXPECT_FALSE(child);
            EXPECT_EQ(pointer->parent(), &parent);
        }
        const NauWidget& view = parent;
        EXPECT_EQ(view.childCount(), 3);
        for (size_t i = 0; i < children.size(); ++i)
        {
            EXPECT_EQ(parent.childAt(i), children[i]);
            EXPECT_EQ(view.childAt(i), children[i]);
            EXPECT_EQ(std::as_const(*children[i]).parent(), &parent);
        }
        EXPECT_EQ(view.childAt(3), nullptr);
    }

    TEST(WidgetTests, RejectionRetainsOwnershipAndTree)
    {
        auto root = NauWidget::create();
        auto descendant = NauWidget::create();
        auto* rootAddress = root.get();
        auto* descendantAddress = descendant.get();
        ASSERT_TRUE(root->addChild(std::move(descendant)));
        scene::ObjectUniquePtr<NauWidget> empty;
        EXPECT_FALSE(root->addChild(std::move(empty)));
        EXPECT_FALSE(root->addChild(std::move(root)));
        EXPECT_EQ(root.get(), rootAddress);
        EXPECT_FALSE(descendantAddress->addChild(std::move(root)));
        EXPECT_EQ(root.get(), rootAddress);
        EXPECT_EQ(root->parent(), nullptr);
        EXPECT_EQ(root->childCount(), 1);
        EXPECT_EQ(root->childAt(0), descendantAddress);
        EXPECT_EQ(descendantAddress->parent(), rootAddress);
        EXPECT_EQ(descendantAddress->childCount(), 0);
    }

    TEST(WidgetTests, DetachmentAndReattachmentPreserveSubtreeAndState)
    {
        auto firstOwner = NauWidget::create();
        auto secondOwner = NauWidget::create();
        auto& first = *firstOwner;
        auto& second = *secondOwner;
        std::array<NauWidget*, 3> children{};
        for (auto& pointer : children)
        {
            auto child = NauWidget::create();
            pointer = child.get();
            ASSERT_TRUE(first.addChild(std::move(child)));
        }
        auto grandchild = NauWidget::create();
        auto* grandchildAddress = grandchild.get();
        ASSERT_TRUE(children[1]->addChild(std::move(grandchild)));
        children[1]->setDebugName("middle");
        children[1]->setVisible(false);
        const auto identity = children[1]->getUid();
        EXPECT_FALSE(first.removeChild(second));
        EXPECT_FALSE(first.removeChild(*grandchildAddress));
        auto detached = first.removeChild(*children[1]);
        ASSERT_EQ(detached.get(), children[1]);
        EXPECT_EQ(detached->parent(), nullptr);
        EXPECT_EQ(first.childCount(), 2);
        EXPECT_EQ(first.childAt(0), children[0]);
        EXPECT_EQ(first.childAt(1), children[2]);
        EXPECT_EQ(detached->childAt(0), grandchildAddress);
        EXPECT_EQ(grandchildAddress->parent(), detached.get());
        ASSERT_TRUE(second.addChild(std::move(detached)));
        EXPECT_EQ(children[1]->parent(), &second);
        EXPECT_EQ(children[1]->getUid(), identity);
        EXPECT_EQ(children[1]->debugName(), "middle");
        EXPECT_FALSE(children[1]->isVisible());
    }

    class LifetimeProbe final : public NauWidget
    {
        NAU_OBJECT(nau::ui::test::LifetimeProbe, NauWidget)
    public:
        static scene::ObjectUniquePtr<NauWidget> create(int& count, IMemAllocator* allocator = nullptr, scene::ObjectWeakRef<NauWidget>* weak = nullptr)
        {
            return scene::ObjectUniquePtr<NauWidget>{NauObject::classCreateInstance<LifetimeProbe>(allocator, count, weak)};
        }
        explicit LifetimeProbe(int& count, scene::ObjectWeakRef<NauWidget>* weak) :
            NauWidget(constructionKey()),
            m_count(count),
            m_weak(weak)
        {
        }
        ~LifetimeProbe() override
        {
            EXPECT_EQ(parent(), nullptr);
            if (m_weak)
            {
                EXPECT_FALSE(*m_weak);
            }
            ++m_count;
        }

    private:
        int& m_count;
        scene::ObjectWeakRef<NauWidget>* m_weak;
    };

    TEST(WidgetTests, DestructionAndDetachedLifetime)
    {
        int rootCount = 0, childCount = 0, grandchildCount = 0, detachedCount = 0;
        int detachedChildCount = 0;
        scene::ObjectUniquePtr<NauWidget> root = LifetimeProbe::create(rootCount);
        scene::ObjectUniquePtr<NauWidget> child = LifetimeProbe::create(childCount);
        scene::ObjectUniquePtr<NauWidget> grandchild = LifetimeProbe::create(grandchildCount);
        scene::ObjectUniquePtr<NauWidget> detached = LifetimeProbe::create(detachedCount);
        scene::ObjectUniquePtr<NauWidget> detachedChild = LifetimeProbe::create(detachedChildCount);
        auto* detachedChildAddress = detachedChild.get();
        ASSERT_TRUE(detached->addChild(std::move(detachedChild)));
        auto* detachedAddress = detached.get();
        ASSERT_TRUE(child->addChild(std::move(grandchild)));
        ASSERT_TRUE(root->addChild(std::move(child)));
        ASSERT_TRUE(root->addChild(std::move(detached)));
        detached = root->removeChild(*detachedAddress);
        root.reset();
        EXPECT_EQ(rootCount, 1);
        EXPECT_EQ(childCount, 1);
        EXPECT_EQ(grandchildCount, 1);
        EXPECT_EQ(detachedCount, 0);
        EXPECT_EQ(detachedChildCount, 0);
        EXPECT_EQ(detached->parent(), nullptr);
        EXPECT_EQ(detached->childAt(0), detachedChildAddress);
        EXPECT_EQ(detachedChildAddress->parent(), detached.get());
        EXPECT_TRUE(detached->isEffectivelyVisible());
        detached.reset();
        EXPECT_EQ(detachedCount, 1);
        EXPECT_EQ(detachedChildCount, 1);
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

    TEST(WidgetTests, WeakReferencesAndAllocatorLifetime)
    {
        CountingAllocator allocator;
        int rootCount = 0, childCount = 0, detachedCount = 0;
        scene::ObjectWeakRef<NauWidget> rootRef, childRef, detachedRef;
        auto root = LifetimeProbe::create(rootCount, &allocator, &rootRef);
        auto child = LifetimeProbe::create(childCount, &allocator, &childRef);
        auto detached = LifetimeProbe::create(detachedCount, &allocator, &detachedRef);
        rootRef = *root;
        childRef = *child;
        detachedRef = *detached;
        ASSERT_TRUE(child->addChild(std::move(detached)));
        ASSERT_TRUE(root->addChild(std::move(child)));
        EXPECT_EQ(childRef->parent(), root.get());
        detached = childRef->removeChild(*detachedRef);
        EXPECT_TRUE(detachedRef);
        EXPECT_EQ(detachedRef.get(), detached.get());
        ASSERT_TRUE(root->addChild(std::move(detached)));
        EXPECT_TRUE(detachedRef);
        EXPECT_EQ(detachedRef->parent(), root.get());
        detached = root->removeChild(*detachedRef);
        root.reset();
        EXPECT_FALSE(rootRef);
        EXPECT_FALSE(childRef);
        EXPECT_TRUE(detachedRef);
        EXPECT_EQ(rootCount, 1);
        EXPECT_EQ(childCount, 1);
        EXPECT_EQ(detachedCount, 0);
        EXPECT_EQ(allocator.allocations, 3);
        EXPECT_EQ(allocator.deallocations, 2);
        detached.reset();
        EXPECT_FALSE(detachedRef);
        EXPECT_EQ(detachedCount, 1);
        EXPECT_EQ(allocator.deallocations, 3);
        EXPECT_TRUE(allocator.live.empty());

        auto base = NauWidget::create(&allocator);
        auto baseRef = base.getRef();
        base.reset();
        EXPECT_FALSE(baseRef);
        EXPECT_EQ(allocator.allocations, 4);
        EXPECT_EQ(allocator.deallocations, 4);
    }

    TEST(WidgetDeathTests, OwnedBaseDestroyIsRejected)
    {
        auto owner = NauWidget::create();
        auto weak = owner.getRef();
        // The engine's fatal diagnostics need not write to stderr.
        EXPECT_DEATH(static_cast<scene::NauObject&>(*owner).destroy(), "");
        EXPECT_TRUE(weak);
    }

    TEST(WidgetTests, VisibilityFollowsAncestryWithoutChangingLocalFlags)
    {
        auto rootOwner = NauWidget::create();
        auto hiddenParentOwner = NauWidget::create();
        auto& root = *rootOwner;
        auto& hiddenParent = *hiddenParentOwner;
        hiddenParent.setVisible(false);
        auto child = NauWidget::create();
        auto leaf = NauWidget::create();
        auto* childAddress = child.get();
        auto* leafAddress = leaf.get();
        ASSERT_TRUE(child->addChild(std::move(leaf)));
        ASSERT_TRUE(root.addChild(std::move(child)));
        root.setVisible(false);
        EXPECT_TRUE(childAddress->isVisible());
        EXPECT_TRUE(leafAddress->isVisible());
        EXPECT_FALSE(leafAddress->isEffectivelyVisible());
        leafAddress->setVisible(false);
        root.setVisible(true);
        EXPECT_TRUE(childAddress->isEffectivelyVisible());
        EXPECT_FALSE(leafAddress->isEffectivelyVisible());
        leafAddress->setVisible(true);
        child = root.removeChild(*childAddress);
        ASSERT_TRUE(hiddenParent.addChild(std::move(child)));
        EXPECT_FALSE(leafAddress->isEffectivelyVisible());
        child = hiddenParent.removeChild(*childAddress);
        EXPECT_TRUE(childAddress->isEffectivelyVisible());
        EXPECT_TRUE(leafAddress->isEffectivelyVisible());
    }
}  // namespace nau::ui::test

int main(int argc, char** argv)
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

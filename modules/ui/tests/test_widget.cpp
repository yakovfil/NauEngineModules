// Copyright 2026 N-GINN LLC. All rights reserved.
// Use of this source code is governed by a BSD-3 Clause license that can be found in the LICENSE file.
#include <gtest/gtest.h>

#include <array>
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

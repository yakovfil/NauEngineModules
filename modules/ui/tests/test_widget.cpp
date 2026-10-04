// Copyright 2026 N-GINN LLC. All rights reserved.
// Use of this source code is governed by a BSD-3 Clause license that can be found in the LICENSE file.
#include <gtest/gtest.h>

#include <array>
#include <type_traits>
#include <utility>

#include "nau/ui/widget.h"

namespace nau::ui::test
{
    static_assert(!std::is_copy_constructible_v<Widget>);
    static_assert(!std::is_copy_assignable_v<Widget>);
    static_assert(!std::is_move_constructible_v<Widget>);
    static_assert(!std::is_move_assignable_v<Widget>);
    static_assert(std::is_same_v<decltype(std::declval<const Widget&>().parent()), const Widget*>);
    static_assert(std::is_same_v<decltype(std::declval<const Widget&>().childAt(0)), const Widget*>);

    TEST(WidgetTests, DefaultsAndIdentity)
    {
        Widget first, second;
        EXPECT_NE(first.id(), second.id());
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
        Widget parent;
        std::array<Widget*, 3> children{};
        for (auto& pointer : children)
        {
            auto child = std::make_unique<Widget>();
            pointer = child.get();
            ASSERT_TRUE(parent.addChild(std::move(child)));
            EXPECT_EQ(child, nullptr);
            EXPECT_EQ(pointer->parent(), &parent);
        }
        const Widget& view = parent;
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
        auto root = std::make_unique<Widget>();
        auto descendant = std::make_unique<Widget>();
        auto* rootAddress = root.get();
        auto* descendantAddress = descendant.get();
        ASSERT_TRUE(root->addChild(std::move(descendant)));
        std::unique_ptr<Widget> empty;
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
        Widget first, second;
        std::array<Widget*, 3> children{};
        for (auto& pointer : children)
        {
            auto child = std::make_unique<Widget>();
            pointer = child.get();
            ASSERT_TRUE(first.addChild(std::move(child)));
        }
        auto grandchild = std::make_unique<Widget>();
        auto* grandchildAddress = grandchild.get();
        ASSERT_TRUE(children[1]->addChild(std::move(grandchild)));
        children[1]->setDebugName("middle");
        children[1]->setVisible(false);
        const auto identity = children[1]->id();
        EXPECT_EQ(first.removeChild(second), nullptr);
        EXPECT_EQ(first.removeChild(*grandchildAddress), nullptr);
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
        EXPECT_EQ(children[1]->id(), identity);
        EXPECT_EQ(children[1]->debugName(), "middle");
        EXPECT_FALSE(children[1]->isVisible());
    }

    class LifetimeProbe final : public Widget
    {
    public:
        explicit LifetimeProbe(int& count) :
            m_count(count)
        {
        }
        ~LifetimeProbe() override
        {
            EXPECT_EQ(parent(), nullptr);
            ++m_count;
        }

    private:
        int& m_count;
    };

    TEST(WidgetTests, DestructionAndDetachedLifetime)
    {
        int rootCount = 0, childCount = 0, grandchildCount = 0, detachedCount = 0;
        int detachedChildCount = 0;
        std::unique_ptr<Widget> root = std::make_unique<LifetimeProbe>(rootCount);
        std::unique_ptr<Widget> child = std::make_unique<LifetimeProbe>(childCount);
        std::unique_ptr<Widget> grandchild = std::make_unique<LifetimeProbe>(grandchildCount);
        std::unique_ptr<Widget> detached = std::make_unique<LifetimeProbe>(detachedCount);
        std::unique_ptr<Widget> detachedChild = std::make_unique<LifetimeProbe>(detachedChildCount);
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

    TEST(WidgetTests, VisibilityFollowsAncestryWithoutChangingLocalFlags)
    {
        Widget root, hiddenParent;
        hiddenParent.setVisible(false);
        auto child = std::make_unique<Widget>();
        auto leaf = std::make_unique<Widget>();
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

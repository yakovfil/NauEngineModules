// Copyright 2026 N-GINN LLC. All rights reserved.
// Use of this source code is governed by a BSD-3 Clause license that can be found in the LICENSE file.
#include <cmath>
#include <limits>
#include <numbers>
#include <utility>

#include "nau/ui/nau_widget.h"

namespace
{
    bool matrixNear(const nau::math::Matrix4& actual, const nau::math::Matrix4& expected)
    {
        for (int column = 0; column < 4; ++column)
        {
            for (int row = 0; row < 4; ++row)
            {
                const float difference = float(actual.getElem(column, row)) - float(expected.getElem(column, row));
                if (!std::isfinite(difference) || std::abs(difference) > 1.e-4f)
                {
                    return false;
                }
            }
        }
        return true;
    }

    bool checkGeometry()
    {
        using namespace nau;
        auto root = ui::NauWidget::create();
        auto child = ui::NauWidget::create();
        auto* borrowed = child.get();
        if (!matrixNear(root->accumulatedMatrix(), math::Matrix4::identity()) ||
            float(root->size().getX()) != 0.f || float(root->size().getY()) != 0.f ||
            float(root->pivot().getX()) != 0.5f || float(root->pivot().getY()) != 0.5f)
        {
            return false;
        }
        const float angle = std::numbers::pi_v<float> / 4.f;
        if (!root->setLocalTransform({
                math::Quat{0.f, 0.f, 0.f, 1.f},
                {10.f, 20.f, 30.f},
                {2.f, 3.f, 4.f}
        }) ||
            !child->setLocalTransform({math::Quat::rotationZ(angle), {1.f, 2.f, 3.f}}) ||
            !child->setSize({200.f, 100.f}) || !root->addChild(std::move(child)))
        {
            return false;
        }
        const auto local = math::Matrix4::translation({1.f, 2.f, 3.f}) * math::Matrix4::rotationZ(angle);
        const auto expected = math::Matrix4::translation({10.f, 20.f, 30.f}) * math::Matrix4::scale({2.f, 3.f, 4.f}) * local;
        if (!matrixNear(borrowed->accumulatedMatrix(), expected) ||
            !root->setSize({400.f, 200.f}) || !root->setPivot({2.f, -1.f}) ||
            !borrowed->setPivot({0.f, 1.f}) || !matrixNear(borrowed->accumulatedMatrix(), expected))
        {
            return false;
        }
        const float nan = std::numeric_limits<float>::quiet_NaN();
        const float inf = std::numeric_limits<float>::infinity();
        auto invalid = borrowed->localTransform();
        invalid.setTranslation({nan, 0.f, 0.f});
        if (borrowed->setLocalTransform(invalid))
        {
            return false;
        }
        invalid = borrowed->localTransform();
        invalid.setScale({1.f, inf, 1.f});
        if (borrowed->setLocalTransform(invalid))
        {
            return false;
        }
        invalid = borrowed->localTransform();
        invalid.setRotation({0.f, 0.f, 0.f, 2.f});
        if (borrowed->setLocalTransform(invalid) || borrowed->setSize({-1.f, 10.f}) ||
            borrowed->setSize({10.f, inf}) || borrowed->setPivot({nan, 0.f}) ||
            !matrixNear(borrowed->accumulatedMatrix(), expected) ||
            float(borrowed->size().getX()) != 200.f || float(borrowed->size().getY()) != 100.f ||
            float(borrowed->pivot().getX()) != 0.f || float(borrowed->pivot().getY()) != 1.f)
        {
            return false;
        }
        child = root->removeChild(*borrowed);
        if (!child || !matrixNear(child->accumulatedMatrix(), local) || !child->setSize({
                                                                            0.f, 0.f
        }) ||
            !child->setLocalTransform({math::Quat{0.f, 0.f, 0.f, 1.f}, {3.f, 4.f, 5.f}, {-2.f, 0.f, 3.f}}))
        {
            return false;
        }
        return matrixNear(child->accumulatedMatrix(), math::Matrix4::translation({3.f, 4.f, 5.f}) * math::Matrix4::scale({-2.f, 0.f, 3.f}));
    }
}  // namespace

int main()
{
    if (!checkGeometry())
    {
        return 7;
    }
    auto parent = nau::ui::NauWidget::create();
    auto child = nau::ui::NauWidget::create();
    auto parentRef = parent.getRef();
    auto childRef = child.getRef();
    const auto uid = child->getUid();
    if (uid == nau::NullUid || uid == parent->getUid() || !child->is<nau::scene::NauObject>())
    {
        return 4;
    }
    child->setDebugName("child");
    auto* borrowed = child.get();
    if (!parent->addChild(std::move(child)) || child || borrowed->parent() != parent.get())
    {
        return 1;
    }
    parent->setVisible(false);
    if (borrowed->isEffectivelyVisible() || !borrowed->isVisible())
    {
        return 2;
    }
    child = parent->removeChild(*borrowed);
    if (!child || child->parent() || !child->isEffectivelyVisible() || parent->childCount() != 0)
    {
        return 3;
    }
    parent.reset();
    if (parentRef || !childRef || childRef.get() != child.get() || child->getUid() != uid)
    {
        return 5;
    }
    child.reset();
    return childRef ? 6 : 0;
}

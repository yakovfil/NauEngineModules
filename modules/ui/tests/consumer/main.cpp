// Copyright 2026 N-GINN LLC. All rights reserved.
// Use of this source code is governed by a BSD-3 Clause license that can be found in the LICENSE file.
#include <utility>

#include "nau/ui/nau_widget.h"

int main()
{
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

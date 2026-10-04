// Copyright 2026 N-GINN LLC. All rights reserved.
// Use of this source code is governed by a BSD-3 Clause license that can be found in the LICENSE file.
#include <utility>

#include "nau/ui/widget.h"

int main()
{
    nau::ui::Widget parent;
    auto child = std::make_unique<nau::ui::Widget>();
    child->setDebugName("child");
    auto* borrowed = child.get();
    if (!parent.addChild(std::move(child)) || child || borrowed->parent() != &parent)
    {
        return 1;
    }
    parent.setVisible(false);
    if (borrowed->isEffectivelyVisible() || !borrowed->isVisible())
    {
        return 2;
    }
    child = parent.removeChild(*borrowed);
    if (!child || child->parent() || !child->isEffectivelyVisible() || parent.childCount() != 0)
    {
        return 3;
    }
    return 0;
}

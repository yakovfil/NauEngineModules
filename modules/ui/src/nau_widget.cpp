// Copyright 2026 N-GINN LLC. All rights reserved.
// Use of this source code is governed by a BSD-3 Clause license that can be found in the LICENSE file.
#include "nau/ui/nau_widget.h"

#include "nau/ui/nau_widget_root_component.h"

namespace nau::ui
{
    NauWidget::ConstructionKey NauWidget::constructionKey()
    {
        return {};
    }

    scene::ObjectUniquePtr<NauWidget> NauWidget::create(IMemAllocator* allocator)
    {
        scene::ObjectUniquePtr<scene::SceneComponent> root = NauObject::classCreateInstance<NauWidgetRootComponent>(allocator);
        return NauObject::classCreateInstance<NauWidget>(allocator, constructionKey(), std::move(root));
    }

    NauWidget::NauWidget(ConstructionKey, scene::ObjectUniquePtr<scene::SceneComponent>&& root) :
        SceneObject(std::move(root))
    {
        NAU_FATAL(getRootComponent().is<NauWidgetRootComponent>());
    }

    NauWidgetRootComponent& NauWidget::widgetRoot() noexcept
    {
        return getRootComponent().as<NauWidgetRootComponent&>();
    }

    const NauWidgetRootComponent& NauWidget::widgetRoot() const noexcept
    {
        return getRootComponent().as<const NauWidgetRootComponent&>();
    }

    bool NauWidget::isVisible() const noexcept
    {
        return widgetRoot().state().visible;
    }

    void NauWidget::setVisible(bool visible) noexcept
    {
        widgetRoot().setVisible(visible);
    }

    bool NauWidget::isEffectivelyVisible() const noexcept
    {
        for (const scene::SceneObject* object = this; object; object = object->getParentObject())
        {
            if (auto* widget = object->as<const NauWidget*>(); widget && !widget->isVisible())
                return false;
        }
        return true;
    }

    math::Vector2 NauWidget::size() const noexcept
    {
        return widgetRoot().state().size;
    }

    Result<> NauWidget::setSize(math::Vector2 size)
    {
        return widgetRoot().setSize(size);
    }

    math::Vector2 NauWidget::pivot() const noexcept
    {
        return widgetRoot().state().pivot;
    }

    Result<> NauWidget::setPivot(math::Vector2 pivot)
    {
        return widgetRoot().setPivot(pivot);
    }
}  // namespace nau::ui

// Copyright 2026 N-GINN LLC. All rights reserved.
// Use of this source code is governed by a BSD-3 Clause license that can be found in the LICENSE file.
#pragma once

#include "generated_NauUi_config.h"
#include "nau/scene/scene_object.h"

namespace nau::ui
{
    class NauWidgetRootComponent;

    // Uses scene ownership, lifecycle and parent-relative transform behavior.
    // Size and pivot describe a rectangle around the transform origin; changing
    // either does not move this object or any descendant.
    class NAU_NAUUI_EXPORT NauWidget : public scene::SceneObject
    {
        NAU_OBJECT(nau::ui::NauWidget, scene::SceneObject)

    protected:
        class ConstructionKey
        {
            friend class NauWidget;
            ConstructionKey() = default;
        };
        static ConstructionKey constructionKey();

    public:
        static scene::ObjectUniquePtr<NauWidget> create(IMemAllocator* allocator = nullptr);
        NauWidget(ConstructionKey, scene::ObjectUniquePtr<scene::SceneComponent>&& root);
        bool isVisible() const noexcept;
        void setVisible(bool visible) noexcept;
        bool isEffectivelyVisible() const noexcept;
        math::Vector2 size() const noexcept;
        Result<> setSize(math::Vector2 size);
        math::Vector2 pivot() const noexcept;
        Result<> setPivot(math::Vector2 pivot);

    private:
        friend class NauWidgetRootComponent;
        NauWidgetRootComponent& widgetRoot() noexcept;
        const NauWidgetRootComponent& widgetRoot() const noexcept;
    };
}  // namespace nau::ui

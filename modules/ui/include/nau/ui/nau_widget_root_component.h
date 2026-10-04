// Copyright 2026 N-GINN LLC. All rights reserved.
// Use of this source code is governed by a BSD-3 Clause license that can be found in the LICENSE file.
#pragma once

#include "generated_NauUi_config.h"
#include "nau/scene/components/component_property_validator.h"
#include "nau/scene/scene_object_constructor.h"

namespace nau::ui
{
    struct WidgetState
    {
        NAU_CLASS_FIELDS(CLASS_NAMED_FIELD(visible, "visible"), CLASS_NAMED_FIELD(size, "size"), CLASS_NAMED_FIELD(pivot, "pivot"))
        bool visible = true;
        math::Vector2 size = {0.f, 0.f};
        math::Vector2 pivot = {0.5f, 0.5f};
    };

    class NAU_NAUUI_EXPORT NauWidgetRootComponent final : public scene::SceneComponent,
                                                          public scene::ISceneObjectConstructor,
                                                          public scene::IComponentPropertyValidator
    {
        NAU_OBJECT(nau::ui::NauWidgetRootComponent, scene::SceneComponent, scene::ISceneObjectConstructor, scene::IComponentPropertyValidator)
        NAU_DECLARE_DYNAMIC_OBJECT
        NAU_CLASS_FIELDS(CLASS_NAMED_FIELD(m_state, "widgetState"))

    public:
        const WidgetState& state() const noexcept;
        void setVisible(bool visible) noexcept;
        Result<> setSize(math::Vector2 size);
        Result<> setPivot(math::Vector2 pivot);
        RuntimeValue::Ptr getValue(std::string_view key) override;
        Result<> setValue(std::string_view key, const RuntimeValue::Ptr& value) override;
        Result<> validateComponentProperties(const RuntimeValue::Ptr& properties) const override;
        scene::ObjectUniquePtr<scene::SceneObject> constructSceneObject(scene::ObjectUniquePtr<scene::SceneComponent>&& root) override;

    private:
        static Result<WidgetState> readState(const RuntimeValue::Ptr& value);
        WidgetState m_state;
    };

    // Call before the scene factory first enumerates component types.
    NAU_NAUUI_EXPORT void registerWidgetComponents();
}  // namespace nau::ui

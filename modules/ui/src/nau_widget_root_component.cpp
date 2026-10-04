// Copyright 2026 N-GINN LLC. All rights reserved.
// Use of this source code is governed by a BSD-3 Clause license that can be found in the LICENSE file.
#include "nau/ui/nau_widget_root_component.h"

#include <cmath>

#include "nau/service/service_provider.h"
#include "nau/ui/nau_widget.h"

namespace nau::ui
{
    NAU_IMPLEMENT_DYNAMIC_OBJECT(NauWidgetRootComponent)

    namespace
    {
        bool finite(math::Vector2 value)
        {
            return std::isfinite(float(value.getX())) && std::isfinite(float(value.getY()));
        }

        Result<> readVector(const RuntimeValue::Ptr& value, math::Vector2& output)
        {
            auto* collection = value ? value->as<RuntimeReadonlyCollection*>() : nullptr;
            if (!collection || collection->getSize() != 2)
                return NauMakeError("Widget geometry must contain two numeric components");
            math::Vector2 candidate;
            for (size_t i = 0; i < 2; ++i)
            {
                auto element = collection->getAt(i);
                if (!element || (!element->is<RuntimeFloatValue>() && !element->is<RuntimeIntegerValue>()))
                    return NauMakeError("Widget geometry components must be numbers");
                auto number = runtimeValueCast<float>(element);
                NauCheckResult(number);
                candidate.setElem(static_cast<int>(i), *number);
            }
            if (!finite(candidate))
                return NauMakeError("Widget geometry must be finite");
            output = candidate;
            return {};
        }
    }  // namespace

    const WidgetState& NauWidgetRootComponent::state() const noexcept
    {
        return m_state;
    }

    void NauWidgetRootComponent::setVisible(bool visible) noexcept
    {
        m_state.visible = visible;
    }

    Result<> NauWidgetRootComponent::setSize(math::Vector2 size)
    {
        if (!finite(size) || float(size.getX()) < 0.f || float(size.getY()) < 0.f)
            return NauMakeError("Widget size must be finite and nonnegative");
        m_state.size = size;
        return {};
    }

    Result<> NauWidgetRootComponent::setPivot(math::Vector2 pivot)
    {
        if (!finite(pivot))
            return NauMakeError("Widget pivot must be finite");
        m_state.pivot = pivot;
        return {};
    }

    RuntimeValue::Ptr NauWidgetRootComponent::getValue(std::string_view key)
    {
        // Snapshot prevents mutation of individual fields from bypassing validation.
        if (key == "widgetState")
            return makeValueCopy(m_state);
        return SceneComponent::getValue(key);
    }

    Result<> NauWidgetRootComponent::setValue(std::string_view key, const RuntimeValue::Ptr& value)
    {
        if (key != "widgetState")
            return SceneComponent::setValue(key, value);
        auto candidate = readState(value);
        NauCheckResult(candidate);
        m_state = *candidate;
        return {};
    }

    Result<> NauWidgetRootComponent::validateComponentProperties(const RuntimeValue::Ptr& properties) const
    {
        auto* dictionary = properties ? properties->as<RuntimeReadonlyDictionary*>() : nullptr;
        if (!dictionary)
            return NauMakeError("Widget component properties must be an object");
        if (dictionary->containsKey("widgetState"))
        {
            auto candidate = readState(dictionary->getValue("widgetState"));
            NauCheckResult(candidate);
        }
        return {};
    }

    Result<WidgetState> NauWidgetRootComponent::readState(const RuntimeValue::Ptr& value)
    {
        auto* dictionary = value ? value->as<RuntimeReadonlyDictionary*>() : nullptr;
        if (!dictionary)
            return NauMakeError("Widget state must be an object");
        WidgetState candidate;
        if (dictionary->containsKey("visible"))
        {
            auto visible = dictionary->getValue("visible");
            auto* boolean = visible ? visible->as<RuntimeBooleanValue*>() : nullptr;
            if (!boolean)
                return NauMakeError("Widget visibility must be boolean");
            candidate.visible = boolean->getBool();
        }
        if (dictionary->containsKey("size"))
        {
            NauCheckResult(readVector(dictionary->getValue("size"), candidate.size));
            if (float(candidate.size.getX()) < 0.f || float(candidate.size.getY()) < 0.f)
                return NauMakeError("Widget size must be nonnegative");
        }
        if (dictionary->containsKey("pivot"))
        {
            NauCheckResult(readVector(dictionary->getValue("pivot"), candidate.pivot));
        }
        return candidate;
    }

    scene::ObjectUniquePtr<scene::SceneObject> NauWidgetRootComponent::constructSceneObject(scene::ObjectUniquePtr<scene::SceneComponent>&& root)
    {
        return scene::NauObject::classCreateInstance<NauWidget>(nullptr, NauWidget::constructionKey(), std::move(root));
    }

    void registerWidgetComponents()
    {
        getServiceProvider().addClass<NauWidgetRootComponent>();
    }
}  // namespace nau::ui

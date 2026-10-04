// Copyright 2026 N-GINN LLC. All rights reserved.
// Use of this source code is governed by a BSD-3 Clause license that can be found in the LICENSE file.
#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "nau/math/transform.h"
#include "nau/scene/nau_object.h"

namespace nau::ui
{
    // Tree and state access require caller-controlled single-threaded access.
    // Borrowed widget pointers remain valid only while the widget is alive.
    class NauWidget : public scene::NauObject
    {
        NAU_OBJECT(nau::ui::NauWidget, scene::NauObject)

    protected:
        // Only widgets and their subclasses can supply the factory construction key.
        class ConstructionKey
        {
            friend class NauWidget;
            ConstructionKey() = default;
        };
        static ConstructionKey constructionKey();

    public:
        static scene::ObjectUniquePtr<NauWidget> create(IMemAllocator* allocator = nullptr);
        explicit NauWidget(ConstructionKey);
        ~NauWidget() override;
        NauWidget(const NauWidget&) = delete;
        NauWidget& operator=(const NauWidget&) = delete;
        NauWidget(NauWidget&&) = delete;
        NauWidget& operator=(NauWidget&&) = delete;

        std::string_view debugName() const noexcept;
        void setDebugName(std::string_view name);
        bool isVisible() const noexcept;
        void setVisible(bool visible) noexcept;
        bool isEffectivelyVisible() const noexcept;

        // Identity by default. Translation places this pivot relative to the parent's pivot.
        // Applies scale, rotation, then translation; callers edit a copy and submit it.
        const math::Transform& localTransform() const noexcept;
        Result<> setLocalTransform(const math::Transform& transform);
        // Unscaled XY rectangle extent (default zero); finite, nonnegative components.
        math::Vector2 size() const noexcept;
        Result<> setSize(math::Vector2 size);
        // Dimensionless rectangle pivot (default center). Finite external pivots are allowed.
        // Rectangle minimum = -pivot * size; maximum = (1 - pivot) * size; local Z = 0.
        // Size/pivot edits leave this transform and descendant origins unchanged.
        math::Vector2 pivot() const noexcept;
        Result<> setPivot(math::Vector2 pivot);
        // Maps pivot-relative points into coordinates outside the top-level widget.
        // Queries ancestry immediately in O(depth), preserving shear; no scene/screen units.
        math::Matrix4 accumulatedMatrix() const;

        NauWidget* parent() noexcept;
        const NauWidget* parent() const noexcept;
        size_t childCount() const noexcept;
        // Returns nullptr when index is outside the child list.
        NauWidget* childAt(size_t index) noexcept;
        const NauWidget* childAt(size_t index) const noexcept;

        // Moves ownership only on success. Rejection leaves child unchanged.
        // Supply an exclusively owned widget, never a second owner of a borrowed pointer.
        Result<> addChild(scene::ObjectUniquePtr<NauWidget>&& child);
        // Returns ownership of a direct child, or nullptr without side effects.
        scene::ObjectUniquePtr<NauWidget> removeChild(NauWidget& child);

    private:
        // Release the owner instead. Virtual base-interface calls abort in every build.
        void destroy() final;
        std::string m_debugName;
        bool m_visible = true;
        math::Transform m_localTransform;
        math::Vector2 m_size = {0.f, 0.f};
        math::Vector2 m_pivot = {0.5f, 0.5f};
        scene::ObjectWeakRef<NauWidget> m_parent;
        std::vector<scene::ObjectUniquePtr<NauWidget>> m_children;
    };
}  // namespace nau::ui

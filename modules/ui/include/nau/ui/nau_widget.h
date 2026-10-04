// Copyright 2026 N-GINN LLC. All rights reserved.
// Use of this source code is governed by a BSD-3 Clause license that can be found in the LICENSE file.
#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

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
        scene::ObjectWeakRef<NauWidget> m_parent;
        std::vector<scene::ObjectUniquePtr<NauWidget>> m_children;
    };
}  // namespace nau::ui

// Copyright 2026 N-GINN LLC. All rights reserved.
// Use of this source code is governed by a BSD-3 Clause license that can be found in the LICENSE file.
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "nau/utils/result.h"

namespace nau::ui
{
    // Tree and state access require caller-controlled single-threaded access.
    // Borrowed widget pointers remain valid only while the widget is alive.
    class Widget
    {
    public:
        Widget();
        virtual ~Widget();
        Widget(const Widget&) = delete;
        Widget& operator=(const Widget&) = delete;
        Widget(Widget&&) = delete;
        Widget& operator=(Widget&&) = delete;

        uint64_t id() const noexcept;
        std::string_view debugName() const noexcept;
        void setDebugName(std::string_view name);
        bool isVisible() const noexcept;
        void setVisible(bool visible) noexcept;
        bool isEffectivelyVisible() const noexcept;

        Widget* parent() noexcept;
        const Widget* parent() const noexcept;
        size_t childCount() const noexcept;
        // Returns nullptr when index is outside the child list.
        Widget* childAt(size_t index) noexcept;
        const Widget* childAt(size_t index) const noexcept;

        // Moves ownership only on success. Rejection leaves child unchanged.
        // Supply an exclusively owned widget, never a second owner of a borrowed pointer.
        Result<> addChild(std::unique_ptr<Widget>&& child);
        // Returns ownership of a direct child, or nullptr without side effects.
        std::unique_ptr<Widget> removeChild(Widget& child);

    private:
        const uint64_t m_id;
        std::string m_debugName;
        bool m_visible = true;
        Widget* m_parent = nullptr;
        std::vector<std::unique_ptr<Widget>> m_children;
    };
}  // namespace nau::ui

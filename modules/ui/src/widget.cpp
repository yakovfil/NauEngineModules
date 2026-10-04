// Copyright 2026 N-GINN LLC. All rights reserved.
// Use of this source code is governed by a BSD-3 Clause license that can be found in the LICENSE file.
#include "nau/ui/widget.h"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <limits>

namespace nau::ui
{
    namespace
    {
        uint64_t nextId()
        {
            static std::atomic<uint64_t> next{1};
            auto value = next.load(std::memory_order_relaxed);
            for (;;)
            {
                if (value == std::numeric_limits<uint64_t>::max())
                {
                    std::abort();
                }
                if (next.compare_exchange_weak(value, value + 1, std::memory_order_relaxed))
                {
                    return value;
                }
            }
        }
    }  // namespace

    Widget::Widget() :
        m_id(nextId())
    {
    }

    Widget::~Widget()
    {
        for (auto& child : m_children)
        {
            child->m_parent = nullptr;
        }
        m_children.clear();
    }

    uint64_t Widget::id() const noexcept
    {
        return m_id;
    }
    std::string_view Widget::debugName() const noexcept
    {
        return m_debugName;
    }
    void Widget::setDebugName(std::string_view name)
    {
        m_debugName = name;
    }
    bool Widget::isVisible() const noexcept
    {
        return m_visible;
    }
    void Widget::setVisible(bool visible) noexcept
    {
        m_visible = visible;
    }
    Widget* Widget::parent() noexcept
    {
        return m_parent;
    }
    const Widget* Widget::parent() const noexcept
    {
        return m_parent;
    }
    size_t Widget::childCount() const noexcept
    {
        return m_children.size();
    }
    Widget* Widget::childAt(size_t index) noexcept
    {
        return index < m_children.size() ? m_children[index].get() : nullptr;
    }
    const Widget* Widget::childAt(size_t index) const noexcept
    {
        return index < m_children.size() ? m_children[index].get() : nullptr;
    }

    bool Widget::isEffectivelyVisible() const noexcept
    {
        for (auto widget = this; widget; widget = widget->m_parent)
        {
            if (!widget->m_visible)
            {
                return false;
            }
        }
        return true;
    }

    Result<> Widget::addChild(std::unique_ptr<Widget>&& child)
    {
        if (!child)
        {
            return NauMakeError("Cannot attach an empty widget");
        }
        if (child->m_parent)
        {
            return NauMakeError("Widget already has a parent");
        }
        for (auto ancestor = this; ancestor; ancestor = ancestor->m_parent)
        {
            if (ancestor == child.get())
            {
                return NauMakeError("Widget attachment would create a cycle");
            }
        }
        m_children.push_back(std::move(child));
        m_children.back()->m_parent = this;
        return {};
    }

    std::unique_ptr<Widget> Widget::removeChild(Widget& child)
    {
        const auto found = std::find_if(m_children.begin(), m_children.end(), [&child](const auto& owned)
        {
            return owned.get() == &child;
        });
        if (found == m_children.end())
        {
            return nullptr;
        }
        auto owned = std::move(*found);
        m_children.erase(found);
        owned->m_parent = nullptr;
        return owned;
    }
}  // namespace nau::ui

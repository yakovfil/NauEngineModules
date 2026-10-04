// Copyright 2026 N-GINN LLC. All rights reserved.
// Use of this source code is governed by a BSD-3 Clause license that can be found in the LICENSE file.
#include "nau/ui/nau_widget.h"

#include <algorithm>
#include <cstdlib>

namespace nau::ui
{
    NauWidget::ConstructionKey NauWidget::constructionKey()
    {
        return {};
    }

    scene::ObjectUniquePtr<NauWidget> NauWidget::create(IMemAllocator* allocator)
    {
        return NauObject::classCreateInstance<NauWidget>(allocator, constructionKey());
    }

    NauWidget::NauWidget(ConstructionKey)
    {
        setUid(Uid::generate());
    }

    void NauWidget::destroy()
    {
        // Owner release is the only supported destruction path, in every build.
        std::abort();
    }

    NauWidget::~NauWidget()
    {
        for (auto& child : m_children)
        {
            child->m_parent = nullptr;
        }
        m_children.clear();
    }

    std::string_view NauWidget::debugName() const noexcept
    {
        return m_debugName;
    }
    void NauWidget::setDebugName(std::string_view name)
    {
        m_debugName = name;
    }
    bool NauWidget::isVisible() const noexcept
    {
        return m_visible;
    }
    void NauWidget::setVisible(bool visible) noexcept
    {
        m_visible = visible;
    }
    NauWidget* NauWidget::parent() noexcept
    {
        return m_parent.get();
    }
    const NauWidget* NauWidget::parent() const noexcept
    {
        return m_parent.get();
    }
    size_t NauWidget::childCount() const noexcept
    {
        return m_children.size();
    }
    NauWidget* NauWidget::childAt(size_t index) noexcept
    {
        return index < m_children.size() ? m_children[index].get() : nullptr;
    }
    const NauWidget* NauWidget::childAt(size_t index) const noexcept
    {
        return index < m_children.size() ? m_children[index].get() : nullptr;
    }

    bool NauWidget::isEffectivelyVisible() const noexcept
    {
        for (auto widget = this; widget; widget = widget->parent())
        {
            if (!widget->m_visible)
            {
                return false;
            }
        }
        return true;
    }

    Result<> NauWidget::addChild(scene::ObjectUniquePtr<NauWidget>&& child)
    {
        if (!child)
        {
            return NauMakeError("Cannot attach an empty widget");
        }
        if (child->m_parent)
        {
            return NauMakeError("NauWidget already has a parent");
        }
        for (auto ancestor = this; ancestor; ancestor = ancestor->parent())
        {
            if (ancestor == child.get())
            {
                return NauMakeError("NauWidget attachment would create a cycle");
            }
        }
        m_children.push_back(std::move(child));
        m_children.back()->m_parent = *this;
        return {};
    }

    scene::ObjectUniquePtr<NauWidget> NauWidget::removeChild(NauWidget& child)
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

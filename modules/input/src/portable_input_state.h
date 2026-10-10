// Copyright 2026 N-GINN LLC. All rights reserved.
// Use of this source code is governed by a BSD-3 Clause license that can be found in the LICENSE file.
#pragma once

#include <array>
#include <cmath>
#include <mutex>

#include "nau/input.h"

namespace nau::input
{
    // Callbacks change pending state; only the owner publishes a frame. Coalescing
    // bounds memory even when a suspended application receives many pointer events.
    class PortableInputState
    {
    public:
        static constexpr int KeyCount = static_cast<int>(Key::KeyCount_);
        static constexpr int ButtonCount = 21;

        void key(int code, bool down)
        {
            std::lock_guard lock(m_mutex);
            if (code >= 0 && code < KeyCount)
                m_pendingKeys[code] = down;
        }
        void button(int code, bool down)
        {
            std::lock_guard lock(m_mutex);
            if (code >= 0 && code < ButtonCount)
            {
                if (code == 0 && down && !m_pendingButtons[code])
                    m_pendingDx = m_pendingDy = 0;
                m_pendingButtons[code] = down;
            }
        }
        void pointer(float x, float y)
        {
            if (!std::isfinite(x) || !std::isfinite(y))
                return;
            std::lock_guard lock(m_mutex);
            if (m_positionKnown)
            {
                m_pendingDx += x - m_pendingX;
                m_pendingDy += y - m_pendingY;
            }
            m_pendingX = x;
            m_pendingY = y;
            m_positionKnown = true;
        }
        void reset()
        {
            std::lock_guard lock(m_mutex);
            m_pendingKeys.fill(false);
            m_pendingButtons.fill(false);
            m_pendingDx = m_pendingDy = 0;
            m_positionKnown = false;
        }
        void publish(bool discardMotion = false)
        {
            std::lock_guard lock(m_mutex);
            m_previousKeys = m_keys;
            m_previousButtons = m_buttons;
            m_keys = m_pendingKeys;
            m_buttons = m_pendingButtons;
            m_x = m_pendingX;
            m_y = m_pendingY;
            m_dx = discardMotion ? 0 : m_pendingDx;
            m_dy = discardMotion ? 0 : m_pendingDy;
            m_pendingDx = m_pendingDy = 0;
        }
        bool keyHeld(int device, int code) const
        {
            return device == 0 && code >= 0 && code < KeyCount && m_keys[code];
        }
        bool keyPressed(int device, int code) const
        {
            return keyHeld(device, code) && !m_previousKeys[code];
        }
        bool buttonHeld(int device, int code) const
        {
            return device == 0 && code >= 0 && code < ButtonCount && m_buttons[code];
        }
        bool buttonPressed(int device, int code) const
        {
            return buttonHeld(device, code) && !m_previousButtons[code];
        }
        bool buttonReleased(int device, int code) const
        {
            return device == 0 && code >= 0 && code < ButtonCount && !m_buttons[code] && m_previousButtons[code];
        }
        float position(int device, MouseKey axis) const
        {
            return device == 0 ? (axis == MouseKey::AxisX ? m_x : axis == MouseKey::AxisY ? m_y
                                                                                          : 0)
                               : 0;
        }
        float delta(int device, MouseKey axis) const
        {
            return device == 0 ? (axis == MouseKey::AxisX ? m_dx : axis == MouseKey::AxisY ? m_dy
                                                                                           : 0)
                               : 0;
        }

    private:
        std::mutex m_mutex;
        std::array<bool, KeyCount> m_pendingKeys{}, m_keys{}, m_previousKeys{};
        std::array<bool, ButtonCount> m_pendingButtons{}, m_buttons{}, m_previousButtons{};
        float m_pendingX = 0, m_pendingY = 0, m_pendingDx = 0, m_pendingDy = 0;
        float m_x = 0, m_y = 0, m_dx = 0, m_dy = 0;
        bool m_positionKnown = false;
    };
}  // namespace nau::input

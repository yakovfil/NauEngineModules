// Copyright 2026 N-GINN LLC. All rights reserved.
// Use of this source code is governed by a BSD-3 Clause license that can be found in the LICENSE file.
#pragma once
#include <atomic>

#include "input_manager_interface.h"
#include "nau/app/main_loop/game_system.h"
#include "nau/diag/logging.h"
#include "nau/rtti/rtti_impl.h"
#include "nau/service/service.h"
#include "portable_input_state.h"

namespace nau::input
{
    class BrowserInput final : public InputManager,
                               public IGameInputUpdate,
                               public IServiceInitialization,
                               public IServiceShutdown
    {
        NAU_RTTI_CLASS(nau::input::BrowserInput, InputManager, IGameInputUpdate, IServiceInitialization, IServiceShutdown)
    public:
        async::Task<> initService() override;
        async::Task<> shutdownService() override;
        void gameInputUpdate(std::chrono::milliseconds) override;
        void event(int type, int code, float x, float y);
        unsigned keyboardPressCount(int device, Key key) override
        {
            return m_state.keyPressCount(device, static_cast<int>(key));
        }
        unsigned mousePressCount(int device, MouseKey key) override
        {
            return m_state.buttonPressCount(device, static_cast<int>(key));
        }
        unsigned resetGeneration() const override
        {
            return m_state.resetGeneration();
        }
        unsigned frameResetGeneration() const override
        {
            return m_state.frameResetGeneration();
        }
        void discardPendingPresses() override
        {
            m_state.discardPendingPresses();
        }
        void setScreenResolution(int, int) override
        {
        }  // Coordinates are CSS/client pixels.
        void update() override
        {
        }  // Automatic frame advancement belongs to the common host.
        void update(float) override
        {
        }
        bool isKeyboardButtonPressed(int device, Key key) override
        {
            return m_state.keyPressed(device, static_cast<int>(key));
        }
        bool isKeyboardButtonHold(int device, Key key) override
        {
            return m_state.keyHeld(device, static_cast<int>(key));
        }
        bool isMouseButtonPressed(int device, MouseKey key) override
        {
            return m_state.buttonPressed(device, static_cast<int>(key));
        }
        bool isMouseButtonReleased(int device, MouseKey key) override
        {
            return m_state.buttonReleased(device, static_cast<int>(key));
        }
        bool isMouseButtonHold(int device, MouseKey key) override
        {
            return m_state.buttonHeld(device, static_cast<int>(key));
        }
        float getMouseAxisValue(int device, MouseKey key) override
        {
            if (key == MouseKey::Wheel || key == MouseKey::HWheel)
                NAU_LOG_ERROR("Mouse wheel queries are unsupported by browser CoreInput");
            return m_state.position(device, key);
        }
        float getMouseAxisDelta(int device, MouseKey key) override
        {
            if (key == MouseKey::Wheel || key == MouseKey::HWheel)
                NAU_LOG_ERROR("Mouse wheel queries are unsupported by browser CoreInput");
            return m_state.delta(device, key);
        }

    private:
        PortableInputState m_state;
        std::atomic<bool> m_accepting{false};
        bool m_firstFrame = true;
    };
}  // namespace nau::input

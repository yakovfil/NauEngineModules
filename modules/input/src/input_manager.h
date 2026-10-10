// Copyright 2024 N-GINN LLC. All rights reserved.
// Use of this source code is governed by a BSD-3 Clause license that can be found in the LICENSE file.

#pragma once

#include <gainput/gainput.h>

#include <atomic>
#include <vector>

#include "input_manager_interface.h"
#include "nau/app/main_loop/game_system.h"
#include "nau/rtti/rtti_impl.h"
#include "nau/rtti/rtti_object.h"
#include "nau/service/service.h"
#include "portable_input_state.h"

namespace nau::input
{
    struct NAU_ABSTRACT_TYPE GainputAccess
    {
        NAU_TYPEID(nau::input::GainputAccess)

        virtual gainput::InputManager& getGainput() = 0;
    };

    class InputManagerImpl final : public IServiceInitialization,
                                   public IGameInputUpdate,
                                   public GainputAccess,
                                   public InputManager
    {
        NAU_RTTI_CLASS(nau::input::InputManagerImpl, IServiceInitialization, IGameInputUpdate, GainputAccess, InputManager)

    public:
        virtual async::Task<> preInitService() override;
        virtual async::Task<> initService() override;

        virtual gainput::InputManager& getGainput() override;
        virtual void setScreenResolution(int x, int y) override;
        virtual void update() override;
        virtual void update(float dt) override;
        virtual bool isKeyboardButtonPressed(int deviceId, nau::input::Key key) override;
        virtual bool isKeyboardButtonHold(int deviceId, nau::input::Key key) override;
        virtual bool isMouseButtonPressed(int deviceId, nau::input::MouseKey key) override;
        virtual bool isMouseButtonReleased(int deviceId, nau::input::MouseKey key) override;
        virtual bool isMouseButtonHold(int deviceId, nau::input::MouseKey key) override;
        virtual float getMouseAxisValue(int deviceId, nau::input::MouseKey axis) override;
        virtual float getMouseAxisDelta(int deviceId, nau::input::MouseKey axis) override;

        void gameInputUpdate(std::chrono::milliseconds dt) override;
        void handleWindowMessage(void* hwnd, unsigned message, uintptr_t wParam, intptr_t lParam);
        void handleGainputMessage(const MSG& message);

    private:
        PortableInputState m_state;
        std::mutex m_gainputMutex;
        std::atomic<void*> m_window{nullptr};
        unsigned m_pointerButtons = 0;  // Accessed only on the Windows message thread.
        bool m_automatic = false;
        bool m_firstFrame = true;
        bool inited = false;
        gainput::InputManager m_inputManager = {false};
        gainput::InputMap m_inputMap = {m_inputManager};
        gainput::DeviceId m_keyboard;
        gainput::DeviceId m_mouse;
    };
}  // namespace nau::input

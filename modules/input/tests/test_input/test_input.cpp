// Copyright 2024 N-GINN LLC. All rights reserved.
// Use of this source code is governed by a BSD-3 Clause license that can be found in the LICENSE file.

#include "portable_input_state.h"
#if defined(NAU_PLATFORM_WIN32) && defined(NAU_STATIC_RUNTIME)
    #include "input_manager.h"
    #include "input_system_impl.h"
#endif

namespace nau::test
{
#if defined(NAU_PLATFORM_WIN32) && defined(NAU_STATIC_RUNTIME)
    TEST(PortableInput, NativeGainputSignalsRemainAvailable)
    {
        InputSystemImpl system;
        auto* controller = system.getController("keyboard");
        ASSERT_NE(controller, nullptr);
        auto* signal = system.createSignal("pressed");
        ASSERT_NE(signal, nullptr);
        signal->setController(controller);
        signal->Properties().set<std::string>("key", "w");
        int calls = 0;
        auto action = system.addAction("move", IInputAction::Continuous, signal, [&](IInputSignal*)
        {
            ++calls;
        });
        ASSERT_NE(action, nullptr);
        MSG message{};
        message.message = WM_KEYDOWN;
        message.wParam = 'W';
        system.handlePlatformMessage(message);
        system.gameInputUpdate(std::chrono::milliseconds(16));
        EXPECT_EQ(calls, 1);
        system.gameInputUpdate(std::chrono::milliseconds(16));
        EXPECT_EQ(calls, 2);
        message.message = WM_KEYUP;
        system.handlePlatformMessage(message);
        system.gameInputUpdate(std::chrono::milliseconds(16));
        EXPECT_EQ(calls, 2);
    }

    TEST(PortableInput, AutomaticGainputAdvancesOnceInMilliseconds)
    {
        input::InputManagerImpl manager;
        manager.gameInputUpdate(std::chrono::milliseconds(16));
        EXPECT_EQ(manager.getGainput().GetTime(), 16);
        manager.update(.016f);
        manager.update();
        EXPECT_EQ(manager.getGainput().GetTime(), 16);
        manager.gameInputUpdate(std::chrono::milliseconds(16));
        EXPECT_EQ(manager.getGainput().GetTime(), 32);
    }

    TEST(PortableInput, UnmanagedGainputRetainsManualUpdate)
    {
        input::InputManagerImpl manager;
        manager.update(.020f);
        EXPECT_EQ(manager.getGainput().GetTime(), 20);
        manager.gameInputUpdate(std::chrono::milliseconds(16));
        manager.update(.020f);
        EXPECT_EQ(manager.getGainput().GetTime(), 36);
    }
#endif
    TEST(PortableInput, HeldStateAndEdges)
    {
        input::PortableInputState state;
        const int key = static_cast<int>(input::Key::W);
        state.key(key, true);
        state.button(0, true);
        EXPECT_FALSE(state.keyHeld(0, key));
        state.publish();
        EXPECT_TRUE(state.keyHeld(0, key));
        EXPECT_TRUE(state.keyPressed(0, key));
        EXPECT_TRUE(state.buttonPressed(0, 0));
        state.publish();
        EXPECT_TRUE(state.keyHeld(0, key));
        EXPECT_TRUE(state.buttonHeld(0, 0));
        EXPECT_FALSE(state.keyPressed(0, key));
        EXPECT_FALSE(state.buttonPressed(0, 0));
        state.key(key, false);
        state.button(0, false);
        state.publish();
        EXPECT_FALSE(state.keyHeld(0, key));
        EXPECT_TRUE(state.buttonReleased(0, 0));
        state.publish();
        EXPECT_FALSE(state.buttonReleased(0, 0));
    }

    TEST(PortableInput, MotionConsumedOnceAndFocusReset)
    {
        input::PortableInputState state;
        state.pointer(100, 50);
        state.button(0, true);
        state.pointer(120, 45);
        state.publish();
        EXPECT_FLOAT_EQ(state.delta(0, input::MouseKey::AxisX), 20);
        EXPECT_FLOAT_EQ(state.delta(0, input::MouseKey::AxisY), -5);
        EXPECT_FLOAT_EQ(state.position(0, input::MouseKey::AxisX), 120);
        state.publish();
        EXPECT_FLOAT_EQ(state.delta(0, input::MouseKey::AxisX), 0);
        state.key(static_cast<int>(input::Key::W), true);
        state.pointer(300, 100);
        state.reset();
        state.publish();
        EXPECT_FALSE(state.keyHeld(0, static_cast<int>(input::Key::W)));
        EXPECT_FALSE(state.buttonHeld(0, 0));
        EXPECT_FLOAT_EQ(state.delta(0, input::MouseKey::AxisX), 0);
        state.pointer(700, 300);
        state.publish();
        EXPECT_FLOAT_EQ(state.delta(0, input::MouseKey::AxisX), 0);
    }

    TEST(PortableInput, InvalidCodesAndStartupMotion)
    {
        input::PortableInputState state;
        state.key(-1, true);
        state.key(input::PortableInputState::KeyCount, true);
        state.button(-1, true);
        state.button(input::PortableInputState::ButtonCount, true);
        state.pointer(10, 10);
        state.pointer(20, 30);
        state.publish(true);
        EXPECT_FALSE(state.keyHeld(0, -1));
        EXPECT_FALSE(state.keyPressed(1, 0));
        EXPECT_FALSE(state.buttonReleased(0, -1));
        EXPECT_FALSE(state.buttonHeld(0, input::PortableInputState::ButtonCount));
        EXPECT_FLOAT_EQ(state.delta(0, input::MouseKey::AxisX), 0);
        EXPECT_FLOAT_EQ(state.position(1, input::MouseKey::AxisX), 0);
        state.pointer(25, 35);
        state.publish();
        EXPECT_FLOAT_EQ(state.delta(0, input::MouseKey::AxisX), 5);
    }

}  // namespace nau::test

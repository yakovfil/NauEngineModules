// Copyright 2026 N-GINN LLC. All rights reserved.
// Use of this source code is governed by a BSD-3 Clause license that can be found in the LICENSE file.
#pragma once

#include "nau/input.h"
#include "nau/rtti/type_info.h"

namespace nau::input
{
    struct NAU_ABSTRACT_TYPE InputManager
    {
        NAU_TYPEID(nau::input::InputManager)
        virtual void setScreenResolution(int x, int y) = 0;
        virtual void update() = 0;
        virtual void update(float dt) = 0;
        virtual bool isKeyboardButtonPressed(int deviceId, Key key) = 0;
        virtual bool isKeyboardButtonHold(int deviceId, Key key) = 0;
        virtual bool isMouseButtonPressed(int deviceId, MouseKey key) = 0;
        virtual bool isMouseButtonReleased(int deviceId, MouseKey key) = 0;
        virtual bool isMouseButtonHold(int deviceId, MouseKey key) = 0;
        virtual float getMouseAxisValue(int deviceId, MouseKey axis) = 0;
        virtual float getMouseAxisDelta(int deviceId, MouseKey axis) = 0;
    };
}  // namespace nau::input

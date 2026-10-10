// Copyright 2024 N-GINN LLC. All rights reserved.
// Use of this source code is governed by a BSD-3 Clause license that can be found in the LICENSE file.

#include "./input_msg_handler.h"

#include "input_manager.h"
#include "nau/service/service_provider.h"

#if !defined(GAINPUT_PLATFORM_WIN)
    #error Invalid GAINPUT Platform
#endif

namespace nau::input
{
    IWindowsApplicationMessageHandler::PreDispatchMsgResult WindowsInputMsgHandler::preDispatchMsg(MSG& msg)
    {
        return PreDispatchMsgResult::Normal;
    }

    void WindowsInputMsgHandler::postDispatchMsg(const MSG& msg)
    {
        getServiceProvider().get<InputManagerImpl>().handleGainputMessage(msg);
    }

    bool WindowsInputMsgHandler::handleMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
    {
        getServiceProvider().get<InputManagerImpl>().handleWindowMessage(hwnd, message, wParam, lParam);
        return false;
    }
}  // namespace nau::input

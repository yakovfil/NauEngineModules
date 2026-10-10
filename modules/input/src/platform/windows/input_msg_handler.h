// Copyright 2024 N-GINN LLC. All rights reserved.
// Use of this source code is governed by a BSD-3 Clause license that can be found in the LICENSE file.

#include "nau/platform/windows/app/window_message_handler.h"
#include "nau/rtti/rtti_impl.h"
#include "nau/utils/functor.h"

namespace nau::input
{
    class WindowsInputMsgHandler final : public IWindowsApplicationMessageHandler,
                                         public IWindowMessageHandler,
                                         public IRttiObject
    {
        NAU_RTTI_CLASS(WindowsInputMsgHandler, IWindowsApplicationMessageHandler, IWindowMessageHandler, IRttiObject)

    private:
        PreDispatchMsgResult preDispatchMsg(MSG& msg) override;
        void postDispatchMsg(const MSG& msg) override;
        bool handleMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) override;
    };
}  // namespace nau::input

// Copyright 2024 N-GINN LLC. All rights reserved.
// Use of this source code is governed by a BSD-3 Clause license that can be found in the LICENSE file.

#ifdef __EMSCRIPTEN__
    #include "platform/emscripten/browser_input.h"
    #include "platform/emscripten/unsupported_input_system.h"
#else
    #include "input_manager.h"
    #include "input_system_impl.h"
#endif
#include "nau/core_defines.h"
#include "nau/module/module.h"
#include "nau/service/service.h"
#include "nau/service/service_provider.h"

#ifdef NAU_PLATFORM_WIN32
    #include "platform/windows/input_msg_handler.h"
    #include "platform/windows/input_system_impl_win.h"
#endif

struct CoreInputModule : public nau::IModule
{
    // Inherited via IModule
    std::string getModuleName() override
    {
        return nau::strings::toUtf8(u8"CoreInput");
    }

    void initialize() override
    {
#ifdef __EMSCRIPTEN__
        NAU_MODULE_EXPORT_SERVICE(nau::input::BrowserInput);
        NAU_MODULE_EXPORT_SERVICE(nau::input::UnsupportedInputSystem);
#else
        NAU_MODULE_EXPORT_SERVICE(nau::input::InputManagerImpl);
        NAU_MODULE_EXPORT_SERVICE(nau::InputSystemImpl);
#endif

#ifdef NAU_PLATFORM_WIN32
        NAU_MODULE_EXPORT_SERVICE(nau::input::WindowsInputMsgHandler);
        NAU_MODULE_EXPORT_SERVICE(nau::input::InputSystemImplWin);
#endif
    }

    void deinitialize() override
    {
    }

    void postInit() override
    {
    }
};

IMPLEMENT_MODULE(CoreInputModule);

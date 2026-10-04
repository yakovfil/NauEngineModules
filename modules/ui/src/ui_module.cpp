// Copyright 2026 N-GINN LLC. All rights reserved.
// Use of this source code is governed by a BSD-3 Clause license that can be found in the LICENSE file.
#include "nau/module/module.h"
#include "nau/ui/nau_widget_root_component.h"

namespace nau::ui
{
    class UiModule final : public DefaultModuleImpl
    {
        string getModuleName() override
        {
            return "nau.ui";
        }
        void initialize() override
        {
            registerWidgetComponents();
        }
    };
}  // namespace nau::ui
IMPLEMENT_MODULE(nau::ui::UiModule)

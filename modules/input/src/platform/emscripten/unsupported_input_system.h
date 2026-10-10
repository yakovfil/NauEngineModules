// Copyright 2026 N-GINN LLC. All rights reserved.
// Use of this source code is governed by a BSD-3 Clause license that can be found in the LICENSE file.
#pragma once
#include "nau/diag/logging.h"
#include "nau/input_system.h"
#include "nau/rtti/rtti_impl.h"

namespace nau::input
{
    // Publish the legacy interface so requests fail explicitly, rather than
    // exposing permanently empty gainput devices or failing service lookup.
    class UnsupportedInputSystem final : public IInputSystem,
                                         public IRttiObject
    {
        NAU_RTTI_CLASS(nau::input::UnsupportedInputSystem, IInputSystem, IRttiObject)
        static void unsupported()
        {
            NAU_LOG_ERROR("Input actions, signals and gainput devices are unsupported by browser CoreInput; use nau/input.h keyboard and pointer queries");
        }

    public:
        std::shared_ptr<IInputAction> addAction(const std::string&, IInputAction::Type, IInputSignal*, Functor<void(IInputSignal*)>) override
        {
            unsupported();
            return {};
        }
        std::shared_ptr<IInputAction> addAction(const std::string&, Functor<void(IInputSignal*)>) override
        {
            unsupported();
            return {};
        }
        bool removeAction(std::shared_ptr<IInputAction>&&) override
        {
            unsupported();
            return false;
        }
        std::shared_ptr<IInputAction> loadAction(const io::FsPath&, Functor<void(IInputSignal*)>) override
        {
            unsupported();
            return {};
        }
        bool saveAction(const std::shared_ptr<IInputAction>, const std::string&) override
        {
            unsupported();
            return false;
        }
        void getActions(std::vector<std::shared_ptr<IInputAction>>& actions) override
        {
            unsupported();
            actions.clear();
        }
        IInputSignal* createSignal(const std::string&) override
        {
            unsupported();
            return nullptr;
        }
        IInputSignal* createSignal(const std::string&, const std::string&, Functor<void(IInputSignal*)>) override
        {
            unsupported();
            return nullptr;
        }
        void setContext(const std::string&) override
        {
            unsupported();
        }
        void addContext(const std::string&) override
        {
            unsupported();
        }
        void removeContext(const std::string&) override
        {
            unsupported();
        }
        std::vector<IInputDevice*> getDevices() override
        {
            unsupported();
            return {};
        }
        IInputController* getController(const std::string&) override
        {
            unsupported();
            return nullptr;
        }
        void setInputSource(const std::string&) override
        {
            unsupported();
        }
    };
}  // namespace nau::input

// Copyright 2026 N-GINN LLC. All rights reserved.
// Use of this source code is governed by a BSD-3 Clause license that can be found in the LICENSE file.
#include <gtest/gtest.h>

#include "nau/app/application.h"
#include "nau/app/run_application.h"
#include "nau/scene/components/component_life_cycle.h"
#include "nau/scene/scene_factory.h"
#include "nau/scene/scene_manager.h"
#include "nau/service/service_provider.h"

namespace nau::test
{
    struct PhaseState
    {
        std::vector<int> order;
        int steps = 0;
        bool destroyed = false;
    };
    class PhaseComponent final : public scene::SceneComponent,
                                 public scene::IComponentUpdate
    {
        NAU_OBJECT(PhaseComponent, scene::SceneComponent, scene::IComponentUpdate)
        NAU_DECLARE_DYNAMIC_OBJECT
    public:
        PhaseState* state = nullptr;
        void updateComponent(float) override
        {
            EXPECT_EQ(getParentObject().getTranslation().getX(), static_cast<float>(state->steps));
            state->order.push_back(2);
        }
    };
    NAU_IMPLEMENT_DYNAMIC_OBJECT(PhaseComponent)
    class SceneDelegate final : public ApplicationDelegate
    {
    public:
        explicit SceneDelegate(PhaseState& state) :
            m_state(state)
        {
        }
        ~SceneDelegate()
        {
            m_state.destroyed = true;
        }
        Result<> configureApplication() override
        {
            return {};
        }
        std::string getModulesListString() const override
        {
            return NAU_MODULES_LIST;
        }
        Result<> initializeServices() override
        {
            getServiceProvider().addClass<PhaseComponent>();
            return {};
        }
        void onApplicationInitialized() override
        {
        }
        async::Task<> startupApplication() override
        {
            auto& factory = getServiceProvider().get<scene::ISceneFactory>();
            auto scene = factory.createEmptyScene();
            auto& object = scene->getRoot().attachChild(factory.createSceneObject<PhaseComponent>());
            object.getRootComponent<PhaseComponent>().state = &m_state;
            m_object = object;
            co_await getServiceProvider().get<scene::ISceneManager>().activateScene(std::move(scene));
        }
        void onApplicationPreUpdate(std::chrono::milliseconds) override
        {
            m_state.order.push_back(1);
            m_object->setTranslation({static_cast<float>(++m_state.steps), 0, 0});
        }
        void onApplicationPostUpdate(std::chrono::milliseconds) override
        {
            m_state.order.push_back(3);
            if (m_state.steps == 3)
                getApplication().stop();
        }

    private:
        PhaseState& m_state;
        scene::ObjectWeakRef<scene::SceneObject> m_object;
    };
    TEST(ApplicationHostScene, PreUpdateEnclosesRealSceneProcessing)
    {
        PhaseState state;
        EXPECT_EQ(runApplication(std::make_unique<SceneDelegate>(state)), 0);
        EXPECT_EQ(state.order, (std::vector<int>{1, 2, 3, 1, 2, 3, 1, 2, 3}));
        EXPECT_TRUE(state.destroyed);
    }
}  // namespace nau::test

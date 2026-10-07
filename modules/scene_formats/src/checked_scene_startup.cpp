// Copyright 2026 N-GINN LLC. All rights reserved.
// Use of this source code is governed by a BSD-3 Clause license that can be found in the LICENSE file.

#include "nau/scene/checked_scene_startup.h"

#include <algorithm>

#include "nau/app/application.h"
#include "nau/assets/asset_manager.h"
#include "nau/assets/asset_ref.h"
#include "nau/io/virtual_file_system.h"
#include "nau/scene/components/component_property_validator.h"
#include "nau/scene/scene_factory.h"
#include "nau/service/service_provider.h"

namespace nau::scene
{
    namespace
    {
        Result<> validateReference(const RuntimeValue::Ptr& source)
        {
            auto* text = source ? source->as<RuntimeStringValue*>() : nullptr;
            if (!text)
                return NauMakeError("Asset reference must be a string");
            const std::string value = text->getString();
            if (value.empty())
                return ResultSuccess;
            if (!AssetPath::isValid(value))
                return NauMakeError("Invalid required asset reference {}", value);
            auto resolved = getServiceProvider().get<IAssetManager>().resolvePath(AssetPath{value});
            if (!resolved)
                return NauMakeError("Unresolved required asset reference {}", value);
            auto file = getServiceProvider().get<io::IVirtualFileSystem>().openFile(
                resolved->getContainerPath(), io::AccessMode::Read, io::OpenFileMode::OpenExisting);
            if (!file || file->getSize() == 0)
                return NauMakeError("Missing or empty required asset reference {}", value);
            io::IStreamReader::Ptr stream = file->createStream();
            if (!stream)
                return NauMakeError("Unreadable required asset reference {}", value);
            std::byte byte;
            auto read = stream->read(&byte, 1);
            if (!read || *read != 1)
                return NauMakeError("Unreadable required asset reference {}", value);
            return ResultSuccess;
        }

        Result<> validateProperties(const RuntimeValue::Ptr& target, const RuntimeValue::Ptr& source, size_t depth = 0)
        {
            if (depth > 256)
                return NauMakeError("Component property nesting exceeds limit");
            if (!target || !source)
                return ResultSuccess;
            if (target->is<RuntimeAssetRefValue>())
                return validateReference(source);
            if (target->is<RuntimeFloatValue>() && !source->is<RuntimeFloatValue>() && !source->is<RuntimeIntegerValue>())
                return NauMakeError("Numeric component property requires a number");
            if (target->is<RuntimeIntegerValue>() && !source->is<RuntimeIntegerValue>())
                return NauMakeError("Integer component property requires an integer");
            if (target->is<RuntimeBooleanValue>() && !source->is<RuntimeBooleanValue>())
                return NauMakeError("Boolean component property requires a boolean");
            if (target->is<RuntimeStringValue>() && !source->is<RuntimeStringValue>())
                return NauMakeError("String component property requires a string");
            if (auto* optional = target->as<RuntimeOptionalValue*>())
                return optional->hasValue() ? validateProperties(optional->getValue(), source, depth + 1) : ResultSuccess;
            if (auto* sourceCollection = source->as<RuntimeReadonlyCollection*>())
            {
                if (auto* targetCollection = target->as<RuntimeReadonlyCollection*>())
                {
                    const auto count = std::min(sourceCollection->getSize(), targetCollection->getSize());
                    for (size_t index = 0; index < count; ++index)
                        NauCheckResult(validateProperties(targetCollection->getAt(index), sourceCollection->getAt(index), depth + 1));
                }
            }
            if (auto* dictionary = source->as<RuntimeReadonlyDictionary*>())
            {
                auto* targetDictionary = target->as<RuntimeReadonlyDictionary*>();
                if (!targetDictionary)
                    return NauMakeError("Component property dictionary mismatch");
                for (size_t index = 0; index < dictionary->getSize(); ++index)
                {
                    auto key = dictionary->getKey(index);
                    auto value = targetDictionary->getValue(key);
                    if (!value)
                        return NauMakeError("Unknown component property {}", key);
                    NauCheckResult(validateProperties(value, dictionary->getValue(key), depth + 1));
                }
            }
            return ResultSuccess;
        }

        class SceneValidation final : public ISceneAssetVisitor
        {
        public:
            explicit SceneValidation(std::span<const std::string> supported) :
                m_supported(supported)
            {
            }

            Result<> result;

            bool visitSceneObject(Uid, const SceneObjectAsset& object) override
            {
                validate(object.uid, object.rootComponent, true);
                return static_cast<bool>(result);
            }

            bool visitSceneComponent(Uid object, const ComponentAsset& component) override
            {
                validate(object, component, false);
                return static_cast<bool>(result);
            }

        private:
            void validate(Uid object, const ComponentAsset& component, bool root)
            {
                if (!result)
                    return;
                auto checked = validateComponent(component, root);
                if (!checked)
                    result = NauMakeError("Scene object {}, component {} ({}): {}", toString(object),
                                          component.componentTypeName, toString(component.uid), checked.getError()->getDiagMessage());
            }

            Result<> validateComponent(const ComponentAsset& component, bool root)
            {
                if (component.componentTypeName.empty())
                    return NauMakeError("Portable scene requires a component type name; recook with current tools");
                const auto types = getServiceProvider().findClasses<Component>();
                auto entry = std::find_if(types.begin(), types.end(), [&](const auto& type)
                {
                    return type->getClassTypeInfo() == component.getComponentType();
                });
                if (entry == types.end())
                    return NauMakeError("Unknown component type");
                if (std::find(m_supported.begin(), m_supported.end(), component.componentTypeName) == m_supported.end())
                    return NauMakeError("Unsupported component: excluded service requirements");
                if (root && !(*entry)->hasInterface<SceneComponent>())
                    return NauMakeError("Root component must supply a scene transform");
                const auto* constructor = (*entry)->getConstructor();
                if (!constructor)
                    return NauMakeError("Component factory has no constructor");
                auto constructed = constructor->invoke(nullptr, {});
                NauCheckResult(constructed);
                if (!*constructed || !(*constructed)->is<Component>())
                    return NauMakeError("Invalid component factory");
                ObjectUniquePtr<Component> instance{(*constructed)->as<Component*>()};
                if (component.properties)
                {
                    RuntimeValue::Ptr target{instance->as<RuntimeValue*>()};
                    NauCheckResult(validateProperties(target, component.properties));
                    if (auto* validator = instance->as<IComponentPropertyValidator*>())
                        NauCheckResult(validator->validateComponentProperties(component.properties));
                    NauCheckResult(RuntimeValue::assign(target, component.properties));
                    NauCheckResult(validateProperties(target, component.properties));
                }
                return ResultSuccess;
            }

            std::span<const std::string> m_supported;
        };
    }  // namespace

    async::Task<IScene::Ptr> loadStartupSceneChecked(const std::string& path, std::span<const std::string> supportedTypes)
    {
        auto descriptor = getServiceProvider().get<IAssetManager>().openAsset(AssetPath{path});
        if (!descriptor)
            co_await async::Task<>::makeRejected(NauMakeError("Required startup scene cannot be resolved: {}", path));
        auto raw = co_await descriptor->getRawAsset();
        auto* asset = raw ? raw->as<SceneAsset*>() : nullptr;
        if (!asset || asset->getSceneInfo().assetKind != SceneAssetKind::Scene)
            co_await async::Task<>::makeRejected(NauMakeError("Required startup scene cannot be loaded: {}", path));
        co_await getApplication().getExecutor();
        SceneValidation validation{supportedTypes};
        asset->visitScene(validation);
        co_await validation.result;
        auto scene = getServiceProvider().get<ISceneFactory>().createSceneFromAsset(*asset);
        if (!scene)
            co_await async::Task<>::makeRejected(NauMakeError("Required startup scene could not be constructed: {}", path));
        co_return scene;
    }
}  // namespace nau::scene

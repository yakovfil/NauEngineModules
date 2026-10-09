// Copyright 2024 N-GINN LLC. All rights reserved.
// Use of this source code is governed by a BSD-3 Clause license that can be found in the LICENSE file.

#include "usd_component_adapter.h"

#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usdGeom/primvarsAPI.h>
#include <pxr/usd/usdGeom/xformCache.h>

#include "nau/diag/logging.h"
#include "nau/scene/components/billboard_component.h"
#include "nau/scene/components/internal/missing_component.h"
#include "nau/scene/components/scene_component.h"
#include "nau/usd_wrapper/usd_attribute_wrapper.h"
#include "usd_prim_translator.h"

namespace UsdTranslator
{
    namespace
    {
        constexpr std::string_view g_typeName = "NauComponent";
    }

    ComponentAdapter::ComponentAdapter(PXR_NS::UsdPrim prim) :
        IPrimAdapter(prim)
    {
    }

    ComponentAdapter::~ComponentAdapter() = default;

    std::string_view ComponentAdapter::getType() const
    {
        return g_typeName;
    }

    nau::async::Task<nau::scene::ObjectWeakRef<nau::scene::SceneObject>> ComponentAdapter::initializeSceneObject(nau::scene::ObjectWeakRef<nau::scene::SceneObject> dest)
    {
        m_obj = dest;
        m_obj->addComponent(getComponentTypeFromPrim(), [this](nau::scene::Component& component)
        {
            m_component = component;
            applyAttributesToComponent();
        });

        co_return m_obj;
    }

    nau::async::Task<> ComponentAdapter::update()
    {
        if (!m_component)
        {
            co_return;
        }

        applyAttributesToComponent();
    }

    bool ComponentAdapter::isValid() const
    {
        return m_obj && m_component;
    }

    void ComponentAdapter::destroySceneObject()
    {
        if (isValid())
        {
            m_obj->removeComponent(m_component);
            // it is expected that all references to the removed component should be invalidated immediately
            NAU_ASSERT(!m_component);
        }

        m_obj = nullptr;
    }

    void ComponentAdapter::applyAttributesToComponent()
    {
        if (!m_component || m_component->is<nau::scene::IMissingComponent>())
        {
            NAU_LOG_WARNING("Trying to apply attributes to invalid component");
            setError("Specified component type not registered");
            getPrim().SetKind("InvalidComponent"_tftoken);
            return;
        }
        else
        {
            clearError();
        }

        auto componentAsRuntimeObject = m_component->as<nau::DynamicObject*>();
        // Billboard textures are authored as USD metadata references. The
        // generic string assignment cannot turn an SdfAssetPath into AssetRef.
        auto* billboard = m_component->as<nau::scene::BillboardComponent*>();
        if (billboard)
        {
            PXR_NS::SdfAssetPath path;
            auto attribute = getPrim().GetAttribute(PXR_NS::TfToken("texture"));
            if (attribute && attribute.Get(&path) && !path.GetResolvedPath().empty())
            {
                auto metadata = PXR_NS::UsdStage::Open(path.GetResolvedPath());
                std::string identity;
                if (metadata)
                    for (auto prim : metadata->Traverse())
                        if (prim.GetTypeName() == PXR_NS::TfToken("NauAssetTexture"))
                            prim.GetAttribute(PXR_NS::TfToken("uid")).Get(&identity);
                if (!identity.empty())
                    billboard->setTextureRef(nau::TextureAssetRef{"uid:" + identity});
                else
                    NAU_LOG_ERROR("Billboard {} cannot resolve texture metadata {}", getPrim().GetPath().GetString(), path.GetAssetPath());
            }
        }
        // NauComponent can also represent a scene component (for example the
        // editor's camera). Restore its local transform, not its owner's.
        if (auto* sceneComponent = m_component->as<nau::scene::SceneComponent*>())
        {
            PXR_NS::UsdGeomXformCache cache;
            bool resets = false;
            auto transform = cache.GetLocalTransformation(getPrim(), &resets);
            const auto translation = transform.ExtractTranslation();
            const PXR_NS::GfVec3d scale(transform.GetRow3(0).GetLength(), transform.GetRow3(1).GetLength(), transform.GetRow3(2).GetLength());
            transform.Orthonormalize(false);
            const auto rotation = transform.ExtractRotationQuat();
            const auto xyz = rotation.GetImaginary();
            sceneComponent->setTranslation({float(translation[0]), float(translation[1]), float(translation[2])});
            sceneComponent->setScale({float(scale[0]), float(scale[1]), float(scale[2])});
            sceneComponent->setRotation({float(xyz[0]), float(xyz[1]), float(xyz[2]), float(rotation.GetReal())});
        }
        auto componentAttributes = getPrim().GetAttributes();
        for (auto attribute : componentAttributes)
        {
            std::string attributeName = attribute.GetName();
            if (billboard && attributeName == "texture" && attribute.GetTypeName() == PXR_NS::SdfValueTypeNames->Asset)
                continue;
            if (!componentAsRuntimeObject->containsKey(attributeName))
            {
                continue;
            }

            auto runtimeValue = nau::attributeAsRuntimeValue(attribute);
            if (!runtimeValue)
            {
                NAU_LOG_WARNING("Can't apply runtime value ({})", attributeName);
                continue;
            }
            auto result = componentAsRuntimeObject->setValue(attributeName, runtimeValue);
            if (!result)
            {
                NAU_LOG_WARNING("Can't apply runtime value ({})", attributeName);
            }
        }

        for (size_t i = 0; i < componentAsRuntimeObject->getSize(); ++i)
        {
            std::string_view keyValue = componentAsRuntimeObject->getKey(i);
            if (billboard && keyValue == "texture")
                continue;
            const nau::RuntimeValue::Ptr& value = componentAsRuntimeObject->getValue(keyValue);
            if (auto res = createAttributeByValue(getPrim(), PXR_NS::TfToken(std::string(keyValue)), value); !res)
            {
                NAU_LOG_WARNING("Can't create attribute by value({})", keyValue);
            }
        }
    }

    nau::scene::ObjectWeakRef<nau::scene::SceneObject> ComponentAdapter::getSceneObject() const
    {
        return m_obj;
    }

    nau::scene::ObjectWeakRef<nau::scene::Component> ComponentAdapter::getComponent() const
    {
        return m_component;
    }

    nau::rtti::TypeInfo ComponentAdapter::getComponentTypeFromPrim() const
    {
        PXR_NS::UsdAttribute componentTypeNameAttribute = getPrim().GetAttribute("componentTypeName"_tftoken);
        NAU_ASSERT(componentTypeNameAttribute);

        PXR_NS::VtValue componentTypeNameValue;
        componentTypeNameAttribute.Get(&componentTypeNameValue);
        NAU_ASSERT(componentTypeNameValue.IsHolding<std::string>());

        std::string componentTypeName = componentTypeNameValue.Get<std::string>();
        return nau::rtti::makeTypeInfoFromName(componentTypeName.c_str());
    }

    DEFINE_TRANSLATOR(ComponentAdapter, "NauComponent"_tftoken);
}  // namespace UsdTranslator

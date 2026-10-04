// Copyright 2024 N-GINN LLC. All rights reserved.
// Use of this source code is governed by a BSD-3 Clause license that can be found in the LICENSE file.

#pragma once 

#include "usd_translator/usd_translator_api.h"
#include "usd_translator/usd_prim_adapter.h"

#include <pxr/usd/usd/stage.h>
#include <nau/scene/scene.h>
#include <functional>

namespace UsdTranslator
{
    class USD_TRANSLATOR_API PrimTranslator final
    {
    public:
        using AdapterFactory = std::function<IPrimAdapter::Ptr(PXR_NS::UsdPrim)>;

        static PrimTranslator& instance();

        bool registerAdapter(const PXR_NS::TfToken& typeName, AdapterFactory adapterFactory, bool doOverride = false);

        IPrimAdapter::Ptr createAdapter(PXR_NS::UsdPrim prim) const;
        PXR_NS::TfToken findAdapterType(PXR_NS::UsdPrim prim) const;
        std::vector<PXR_NS::TfToken> registeredAdapters() const;

        ~PrimTranslator();

    private:
        PrimTranslator() = default;
        PrimTranslator(const PrimTranslator&) = default;
        PrimTranslator(PrimTranslator&&) = default;
        PrimTranslator& operator=(const PrimTranslator&) = default;
        PrimTranslator& operator=(PrimTranslator&&) = default;

        PXR_NS::TfHashMap<PXR_NS::TfToken, AdapterFactory, PXR_NS::TfHash> m_adapters;
    };

    USD_TRANSLATOR_API void translateWorldTransform(PXR_NS::UsdPrim fromPrim, nau::scene::SceneObject& toObject);
}

#define DEFINE_TRANSLATOR(ClassName, PrimType) bool ClassName##AdapterIsRegestred =             \
UsdTranslator::PrimTranslator::instance().registerAdapter(PrimType, [](PXR_NS::UsdPrim prim)    \
    {                                                                                           \
        return std::make_shared<ClassName>(prim);                                               \
    })

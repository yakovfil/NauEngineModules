// Copyright 2024 N-GINN LLC. All rights reserved.
// Use of this source code is governed by a BSD-3 Clause license that can be found in the LICENSE file.

#include "scene_asset_container.h"

#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "nau/async/task.h"
#include "nau/diag/logging.h"
#include "nau/io/memory_stream.h"
#include "nau/io/nau_container.h"
#include "nau/memory/bytes_buffer.h"
#include "nau/memory/stack_allocator.h"
#include "nau/memory/standard_aliases.h"
#include "nau/rtti/rtti_impl.h"
#include "nau/rtti/weak_ptr.h"
#include "nau/serialization/json.h"
#include "scene_serialization.h"

namespace nau
{
    static Result<> validateLocalIds(const RuntimeValue::Ptr& value)
    {
        if (!value)
            return NauMakeError("Missing compiled scene local identity");
        if (auto* collection = value->as<RuntimeReadonlyCollection*>())
        {
            for (size_t index = 0; index < collection->getSize(); ++index)
                NauCheckResult(validateLocalIds(collection->getAt(index)));
            return ResultSuccess;
        }
        auto* integer = value->as<RuntimeIntegerValue*>();
        if (!integer || (integer->isSigned() && integer->getInt64() < 0) ||
            integer->getUint64() > std::numeric_limits<unsigned>::max())
            return NauMakeError("Invalid compiled scene local identity");
        return ResultSuccess;
    }

    // TODO: check what restrictions there are for the SceneAssetContainer to be also SceneAsset.
    class SceneDefaultAsset final : public SceneAsset
    {
        NAU_CLASS_(nau::SceneDefaultAsset, SceneAsset)

    public:
        SceneDefaultAsset(SceneAssetContainer& container) :
            m_container(nau::Ptr{&container})
        {
        }

    private:
        SceneAssetInfo getSceneInfo() const override
        {
            const auto container = m_container.lock();
            NAU_ASSERT(container, "Access to asset whose container is invalid");
            if (!container)
            {
                return {};
            }

            const auto& header = container->getSceneHeader();

            SceneAssetInfo info{
                .assetKind = header.assetKind,
                .name = header.name};

            return info;
        }

        std::optional<Vector<ReferenceField>> getReferencesInfo() const override
        {
            const auto container = m_container.lock();
            NAU_ASSERT(container, "Access to asset whose container is invalid");
            if (!container)
            {
                return std::nullopt;
            }

            const auto& header = container->getSceneHeader();
            return header.referencesInfo;
        }

        void visitScene(ISceneAssetVisitor& visitor) const override
        {
            auto container = m_container.lock();
            NAU_ASSERT(container, "Access to asset whose container is invalid");
            if (!container)
            {
                return;
            }

            const auto& allObjects = container->getSceneObjects();
            const auto& header = container->getSceneHeader();

            for (const unsigned objectId : header.topLevelObjectIds)
            {
                if (auto object = allObjects.find(objectId); object != allObjects.end())
                {
                    visitSceneObjectRecursive(allObjects, nullptr, object->second, visitor);
                }
            }
        }

        void visitSceneObjectRecursive(const SceneAssetContainer::ObjectsMap& allObjects, const SceneObjectAsset* parentObject, const SerializedSceneObject& object, ISceneAssetVisitor& visitor) const
        {
            const Uid parentUid = parentObject ? parentObject->uid : (object.isSceneRoot ? SceneObjectAsset::SceneVirtualRootUid : NullUid);

            visitor.visitSceneObject(parentUid, object);

            for (const auto& component : object.additionalComponents)
            {
                visitor.visitSceneComponent(object.uid, component);
            }

            for (const unsigned childId : object.childLocalIds)
            {
                auto child = allObjects.find(childId);
                if (child != allObjects.end())
                {
                    const SerializedSceneObject& childObject = child->second;
                    visitSceneObjectRecursive(allObjects, &object, childObject, visitor);
                }
                else
                {
                    NAU_LOG_WARNING("Invalid child object id:({})", childId);
                }
            }
        }

        mutable WeakPtr<SceneAssetContainer> m_container;
    };

    SceneAssetContainer::SceneAssetContainer(SceneHeader&& sceneHeader, SceneAssetContainer::ObjectsMap&& sceneObjects) :
        m_sceneHeader(std::move(sceneHeader)),
        m_sceneObjects(std::move(sceneObjects))
    {
    }

    const SceneAssetContainer::ObjectsMap& SceneAssetContainer::getSceneObjects() const
    {
        return m_sceneObjects;
    }

    const SceneHeader& SceneAssetContainer::getSceneHeader() const
    {
        return m_sceneHeader;
    }

    nau::Ptr<> SceneAssetContainer::getAsset([[maybe_unused]] std::string_view path)
    {
        NAU_ASSERT(path.empty(), "Currently only default asset is valid");
        if (!path.empty())
        {
            return nullptr;
        }

        return rtti::createInstance<SceneDefaultAsset>(*this);
    }

    std::vector<std::string> SceneAssetContainer::getContent() const
    {
        return {
            std::string{}};
    }

    std::vector<std::string_view> SceneAssetLoader::getSupportedAssetKind() const
    {
        return {"scene/nscene", "nscene", "nprefab", "scene/prefab"};
    }

    async::Task<IAssetContainer::Ptr> SceneAssetLoader::loadFromStream(io::IStreamReader::Ptr stream, AssetContentInfo info)
    {
        using namespace nau::async;
        using ObjectsMap = SceneAssetContainer::ObjectsMap;

        NAU_FATAL(stream);

        auto headerResult = io::readContainerHeader(stream);
        if (!headerResult)
            co_await Task<>::makeRejected(headerResult.getError());
        auto [headerValue, dataOffset] = *headerResult;
        auto* headerDictionary = headerValue ? headerValue->as<RuntimeReadonlyDictionary*>() : nullptr;
        if (!headerDictionary)
            co_await Task<>::makeRejected(NauMakeError("Compiled scene header must be an object"));
        co_await validateLocalIds(headerDictionary->getValue("topLevelObjectIds"));
        SceneHeader header;
        co_await runtimeValueApply(header, headerValue);
        if ((header.assetKind != SceneAssetKind::Scene && header.assetKind != SceneAssetKind::Prefab) ||
            header.version != "1.0.0" || header.objectsContentFormat != "application/json" ||
            header.objects.empty() || header.objects.size() > 4096 || header.topLevelObjectIds.empty())
            co_await Task<>::makeRejected(NauMakeError("Invalid compiled scene header"));

        const size_t fileSize = stream->setPosition(io::OffsetOrigin::End, 0);
        ObjectsMap allSceneObjects;
        size_t totalBytes = 0;
        for (const ObjectsBlockInfo& block : header.objects)
        {
            if (dataOffset > fileSize || block.offset > fileSize - dataOffset ||
                block.size == 0 || block.size > fileSize - dataOffset - block.offset ||
                block.size > 64 * 1024 * 1024 - totalBytes)
                co_await Task<>::makeRejected(NauMakeError("Invalid or truncated compiled scene object block"));
            totalBytes += block.size;
            stream->setPosition(io::OffsetOrigin::Begin, dataOffset + block.offset);
            BytesBuffer buffer{static_cast<size_t>(block.size)};
            auto read = stream->read(buffer.data(), buffer.size());
            if (!read || *read != buffer.size())
                co_await Task<>::makeRejected(NauMakeError("Could not read compiled scene object block"));
            auto parsed = serialization::jsonParseString(std::string_view(
                reinterpret_cast<const char*>(buffer.data()), buffer.size()));
            if (!parsed)
                co_await Task<>::makeRejected(parsed.getError());
            auto* records = *parsed ? (*parsed)->as<RuntimeReadonlyCollection*>() : nullptr;
            if (!records)
                co_await Task<>::makeRejected(NauMakeError("Compiled scene object block must be an array"));
            for (size_t index = 0; index < records->getSize(); ++index)
            {
                auto record = records->getAt(index);
                auto* dictionary = record ? record->as<RuntimeReadonlyDictionary*>() : nullptr;
                if (!dictionary)
                    co_await Task<>::makeRejected(NauMakeError("Compiled scene object must be an object"));
                for (const auto* field : {"localId", "parentLocalId", "childLocalIds"})
                    co_await validateLocalIds(dictionary->getValue(field));
            }
            std::vector<SerializedSceneObject> objects;
            co_await runtimeValueApply(objects, *parsed);
            if (objects.size() > 100000 - allSceneObjects.size())
                co_await Task<>::makeRejected(NauMakeError("Compiled scene exceeds object limit"));
            for (auto& object : objects)
            {
                if (object.localId == 0 || object.uid == NullUid ||
                    !allSceneObjects.emplace(object.localId, std::move(object)).second)
                    co_await Task<>::makeRejected(NauMakeError("Invalid or duplicate compiled scene object identity"));
            }
            co_await Executor::getDefault();
        }

        // Validate the complete tree before recursive visitation can touch it.
        std::unordered_set<unsigned> visited;
        std::unordered_set<Uid> identities;
        size_t sceneRoots = 0;
        const auto validate = [&](auto&& self, unsigned id, unsigned parent, size_t depth) -> Result<>
        {
            auto entry = allSceneObjects.find(id);
            if (depth > 512 || entry == allSceneObjects.end() || !visited.insert(id).second)
                return NauMakeError("Invalid compiled scene hierarchy at object {}", id);
            const auto& object = entry->second;
            if (object.parentLocalId != parent || !identities.insert(object.uid).second ||
                object.childCount != object.childLocalIds.size())
                return NauMakeError("Invalid compiled scene object {}", id);
            if (object.isSceneRoot && (parent != 0 || ++sceneRoots > 1))
                return NauMakeError("Invalid compiled scene root {}", id);
            for (unsigned child : object.childLocalIds)
                NauCheckResult(self(self, child, id, depth + 1));
            return ResultSuccess;
        };
        for (unsigned id : header.topLevelObjectIds)
            co_await validate(validate, id, 0, 0);
        if (visited.size() != allSceneObjects.size() ||
            (header.assetKind == SceneAssetKind::Scene && sceneRoots != 1))
            co_await Task<>::makeRejected(NauMakeError("Compiled scene has orphaned objects or no scene root"));

        co_return rtti::createInstance<SceneAssetContainer>(std::move(header), std::move(allSceneObjects));
    }

    RuntimeReadonlyDictionary::Ptr SceneAssetLoader::getDefaultImportSettings() const
    {
        return nullptr;
    }

}  // namespace nau

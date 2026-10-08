// Copyright 2026 N-GINN LLC. All rights reserved.
// Use of this source code is governed by a BSD-3 Clause license that can be found in the LICENSE file.
#include "rendering_policy.h"

#include <cmath>
#include <set>

#include "nau/io/virtual_file_system.h"
#include "nau/serialization/json.h"
#include "nau/service/service_provider.h"
#include "nau/utils/uid.h"

namespace nau::render
{
    namespace
    {
        Result<> fields(RuntimeReadonlyDictionary* object, const std::set<std::string_view>& required, const std::set<std::string_view>& optional = {})
        {
            if (!object)
                return NauMakeError("Rendering JSON must be an object");
            for (auto key : required)
                if (!object->containsKey(key))
                    return NauMakeError("Missing rendering field {}", key);
            for (size_t i = 0; i < object->getSize(); ++i)
                if (!required.contains(object->getKey(i)) && !optional.contains(object->getKey(i)))
                    return NauMakeError("Unsupported rendering field {}", object->getKey(i));
            return {};
        }
        Result<std::array<float, 3>> vector(RuntimeValue::Ptr value, bool color)
        {
            auto* list = value ? value->as<RuntimeReadonlyCollection*>() : nullptr;
            if (!list || list->getSize() != 3)
                return NauMakeError("Rendering vector requires three finite numbers");
            std::array<float, 3> result;
            for (unsigned i = 0; i < 3; ++i)
            {
                auto component = list->getAt(i);
                if (auto* real = component->as<RuntimeFloatValue*>())
                    result[i] = real->getSingle();
                else if (auto* integer = component->as<RuntimeIntegerValue*>())
                    result[i] = integer->isSigned() ? float(integer->getInt64()) : float(integer->getUint64());
                else
                    return NauMakeError("Rendering vector component must be numeric");
                if (!std::isfinite(result[i]) || (color && (result[i] < 0 || result[i] > 1)))
                    return NauMakeError("Rendering vector is outside the supported range");
            }
            return result;
        }
        Result<std::string> materialPath(RuntimeValue::Ptr value)
        {
            auto* text = value ? value->as<RuntimeStringValue*>() : nullptr;
            if (!text)
                return NauMakeError("Portable material path must be a string");
            std::string path{text->getString()};
            if (!path.starts_with("materials/") || !path.ends_with(".json") || path.size() <= 15 || path.find('.') != path.size() - 5 || io::FsPath{path}.getString() != path)
                return NauMakeError("Invalid portable material path {}", path);
            for (char c : std::string_view{path}.substr(0, path.size() - 5))
                if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '/'))
                    return NauMakeError("Invalid portable material path {}", path);
            return path;
        }
    }  // namespace
    Result<RuntimeValue::Ptr> readRenderingJson(const std::string& path)
    {
        auto file = getServiceProvider().get<io::IVirtualFileSystem>().openFile(path, io::AccessMode::Read, io::OpenFileMode::OpenExisting);
        io::IStreamReader::Ptr stream = file ? file->createStream() : nullptr;
        if (!file || !stream || file->getSize() == 0 || file->getSize() > 16 * 1024 * 1024)
            return NauMakeError("Missing, unreadable or oversized rendering JSON {}", path);
        auto value = serialization::jsonParse(*stream);
        if (!value)
            return NauMakeError("Malformed rendering JSON {}: {}", path, value.getError()->getDiagMessage());
        return *value;
    }
    Result<RenderingPolicy> parseRenderingPolicy(RuntimeValue::Ptr value)
    {
        auto* object = value ? value->as<RuntimeReadonlyDictionary*>() : nullptr;
        NauCheckResult(fields(object, {"materials", "lighting"}, {"defaultMaterial"}));
        RenderingPolicy result;
        auto mapping = object->getValue("materials");
        auto* materials = mapping ? mapping->as<RuntimeReadonlyDictionary*>() : nullptr;
        if (!materials)
            return NauMakeError("Portable material mappings must be an object");
        for (size_t i = 0; i < materials->getSize(); ++i)
        {
            const std::string identity{materials->getKey(i)};
            auto uid = Uid::parseString(identity.c_str());
            if (!uid || toString(*uid) != identity)
                return NauMakeError("Invalid portable material identity {}", identity);
            auto path = materialPath(materials->getValue(identity));
            if (!path)
                return path.getError();
            result.materials.emplace(identity, *path);
        }
        if (object->containsKey("defaultMaterial"))
        {
            auto path = materialPath(object->getValue("defaultMaterial"));
            if (!path)
                return path.getError();
            result.defaultMaterial = *path;
        }
        auto lightingValue = object->getValue("lighting");
        auto* lighting = lightingValue ? lightingValue->as<RuntimeReadonlyDictionary*>() : nullptr;
        NauCheckResult(fields(lighting, {"direction", "ambient", "directional"}));
        auto direction = vector(lighting->getValue("direction"), false);
        auto ambient = vector(lighting->getValue("ambient"), true);
        auto directional = vector(lighting->getValue("directional"), true);
        if (!direction)
            return direction.getError();
        if (!ambient)
            return ambient.getError();
        if (!directional)
            return directional.getError();
        float squaredLength = 0;
        for (float component : *direction)
            squaredLength += component * component;
        if (!std::isfinite(squaredLength) || std::abs(squaredLength - 1) > .001f)
            return NauMakeError("Rendering light direction must be normalized");
        result.direction = *direction;
        result.ambient = *ambient;
        result.directional = *directional;
        return result;
    }
    Result<std::array<float, 3>> parseSimpleMaterial(RuntimeValue::Ptr value)
    {
        auto* object = value ? value->as<RuntimeReadonlyDictionary*>() : nullptr;
        NauCheckResult(fields(object, {"version", "baseColor"}));
        auto versionValue = object->getValue("version");
        auto* version = versionValue ? versionValue->as<RuntimeIntegerValue*>() : nullptr;
        if (!version || (version->isSigned() ? version->getInt64() != 1 : version->getUint64() != 1))
            return NauMakeError("Unsupported portable material version");
        return vector(object->getValue("baseColor"), true);
    }
}  // namespace nau::render

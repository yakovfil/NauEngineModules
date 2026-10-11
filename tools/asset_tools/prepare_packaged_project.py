"""Prepare the non-rendering startup asset closure with native USD/asset tools."""
import argparse
from collections import Counter
import json
import math
import pathlib
import shutil
import subprocess
import sys
import uuid
from packaged_project import declaration, RENDERING_PROFILE, simple_material
from portable_render_assets import normalize_mesh
from portable_texture import normalize_texture
from portable_scene_identity import stable_scene


def contained(root, path):
    path = path.resolve()
    if not path.is_relative_to(root):
        raise RuntimeError(f"Project asset escapes content directory: {path}")
    if not path.is_file():
        raise RuntimeError(f"Required project asset is missing: {path}")
    return path


def prepare(args):
    # Import after host environment validation in the caller. USD never enters
    # the browser graph; its recorded plugins must be provisioned explicitly.
    from pxr import Sdf, Usd

    project = args.project.resolve()
    destination = args.output.resolve()
    build_root = (project / "build").resolve()
    if not destination.is_relative_to(build_root) or destination == build_root:
        raise RuntimeError("Preparation output must be a child of the project's build directory")
    for required in (args.asset_tool, args.pack_tool, args.module):
        if not required.is_file():
            raise RuntimeError(f"Missing native preparation prerequisite: {required}")
    content = (project / "content").resolve()
    policy = declaration(args.declaration) if getattr(args, "declaration", None) else None
    config_name = pathlib.PurePosixPath(policy["startupConfiguration"]).name if policy else "scene.json"
    config = json.loads((project / "config" / config_name).read_text())
    startup = config["scene"]["startupScene"]
    prefix = "asset:/content/"
    if not startup.startswith(prefix):
        raise RuntimeError(f"Unsupported startup scene path: {startup}")
    main = contained(content, content / startup[len(prefix):])
    scenes = [main] + [contained(content, content / path) for path in args.scene]
    supported = set(policy["supportedComponents"] if policy else args.supported_component)
    if policy and args.supported_component:
        raise RuntimeError("Do not override supported components when a declaration is selected")
    if not supported:
        raise RuntimeError("Preparation requires an explicit supported component list")

    # A unique scratch directory prevents old compiled output from concealing a
    # failed conversion. Authored files are copied, never modified by importers.
    scratch = destination / ("work-" + uuid.uuid4().hex)
    scratch.mkdir(parents=True)
    shutil.copytree(content, scratch / "content")
    (scratch / "assets_database").mkdir()
    mesh_metadata = set()
    passive = set()
    seen = set()
    billboard_textures = {}

    def inspect(path):
        path = contained(content, path)
        if path in seen:
            return
        seen.add(path)
        stage = Usd.Stage.Open(str(path))
        if not stage:
            raise RuntimeError(f"Could not open authored USD asset: {path}")
        for layer in stage.GetUsedLayers():
            if layer.realPath:
                for reference in layer.GetExternalReferences():
                    referenced = contained(content, pathlib.Path(Sdf.ComputeAssetPathRelativeToLayer(layer, reference)))
                    if referenced != path:
                        inspect(referenced)
                layer_path = contained(content, pathlib.Path(layer.realPath))
                if layer_path != path:
                    inspect(layer_path)
        for prim in stage.Traverse():
            component = prim.GetAttribute("componentTypeName")
            if component and component.Get() not in supported:
                raise RuntimeError(f"Unsupported component {component.Get()} on {prim.GetPath()} in {path}")
            if component and component.Get() == "DemoBattlezone":
                from portable_vector_game import validate_game
                game_path = prim.GetAttribute("gameContent").Get()
                if not isinstance(game_path, str) or not game_path.startswith("/content/"):
                    raise RuntimeError(f"Invalid game content reference in {path}")
                source = contained(content, content / game_path[len("/content/"):])
                try:
                    validate_game(json.loads(source.read_text()))
                except (ValueError, KeyError, TypeError) as error:
                    raise RuntimeError(f"Invalid vector game data {source}: {error}") from error
                passive.add(source)
            if policy and policy["profile"] == RENDERING_PROFILE and component and component.Get() == "nau::scene::BillboardComponent":
                size = prim.GetAttribute("screenPercentageSize")
                if size and size.Get() is not None and (not math.isfinite(size.Get()) or size.Get() <= 0):
                    raise RuntimeError(f"Invalid billboard size on {prim.GetPath()} in {path}")
                texture = prim.GetAttribute("texture")
                asset = texture.Get() if texture else None
                if not asset or not getattr(asset, "path", ""):
                    raise RuntimeError(f"Missing billboard texture on {prim.GetPath()} in {path}")
                texture_path = contained(content, content / asset.path[len(prefix):] if asset.path.startswith(prefix) else pathlib.Path(Sdf.ComputeAssetPathRelativeToLayer(texture.GetPropertyStack()[0].layer, asset.path)))
                billboard_textures[str(prim.GetAttribute("uid").Get())] = texture_path
            if str(prim.GetTypeName()) == "NauAssetMesh":
                for spec in prim.GetPrimStack():
                    if spec.layer.realPath and spec.layer.realPath.endswith(".nausd"):
                        mesh_metadata.add(contained(content, pathlib.Path(spec.layer.realPath)))
            for attribute in prim.GetAttributes():
                if str(attribute.GetTypeName()) != "asset":
                    continue
                asset = attribute.Get()
                if not asset or not asset.path:
                    continue
                if asset.path.startswith(prefix):
                    referenced = content / asset.path[len(prefix):]
                else:
                    stack = attribute.GetPropertyStack()
                    referenced = pathlib.Path(Sdf.ComputeAssetPathRelativeToLayer(stack[0].layer, asset.path))
                referenced = contained(content, referenced)
                if referenced.suffix in (".nausd", ".usda", ".usd", ".nausd_scene"):
                    inspect(referenced)
                if referenced.suffix == ".nausd" and referenced not in mesh_metadata:
                    passive.add(referenced)
                elif referenced.suffix not in (".nausd", ".usda", ".usd", ".nausd_scene"):
                    passive.add(referenced)

    for scene in scenes:
        inspect(scene)
        contained(content, pathlib.Path(str(scene) + ".nausd"))

    def compile_asset(metadata):
        copied = scratch / "content" / metadata.relative_to(content)
        subprocess.run([str(args.asset_tool.resolve()), "import", "--project", str(scratch),
                        "--file", str(copied), "--modules", str(args.module.resolve())],
                       cwd=scratch, check=True)

    for metadata in sorted(mesh_metadata):
        compile_asset(metadata)
    database_file = scratch / "assets_database/database.db"
    if policy and policy["profile"] == RENDERING_PROFILE:
        # AssetRef serialization cannot preserve a material UID unless the host
        # resolver knows it while the scene is translated. Register passive
        # metadata before cooking the scene, not only before packaging.
        initial_database = json.loads(database_file.read_text()) if database_file.exists() else {"content": []}
        initial_entries = initial_database.setdefault("content", [])
        for source in sorted(passive - mesh_metadata):
            if source.suffix != ".nausd":
                continue
            stage = Usd.Stage.Open(str(source))
            for prim in stage.Traverse():
                uid_attribute = prim.GetAttribute("uid")
                if not uid_attribute or not uid_attribute.Get():
                    continue
                identity = str(uuid.UUID(uid_attribute.Get()))
                if any(entry["uid"] == identity for entry in initial_entries):
                    continue
                relative = source.relative_to(content).as_posix()
                payload = f"passive/{identity}.nausd"
                output = scratch / "assets_database" / payload
                output.parent.mkdir(exist_ok=True)
                shutil.copy2(source, output)
                initial_entries.append(dict(uid=identity, dbPath=payload, kind="PassiveData",
                                            sourceType="nausd", sourcePath=relative[:-6], nausdPath=relative))
        database_file.write_text(json.dumps(initial_database))
    if billboard_textures:
        texture_database = json.loads(database_file.read_text()) if database_file.exists() else {"content": []}
        texture_entries = texture_database.setdefault("content", [])
        for metadata in sorted(set(billboard_textures.values())):
            texture_stage = Usd.Stage.Open(str(metadata))
            texture_prims = [prim for prim in texture_stage.Traverse() if str(prim.GetTypeName()) == "NauAssetTexture"]
            if len(texture_prims) != 1:
                raise RuntimeError(f"Missing or ambiguous billboard texture metadata: {metadata}")
            prim = texture_prims[0]
            identity = str(uuid.UUID(prim.GetAttribute("uid").Get()))
            source_attribute = prim.GetAttribute("path")
            source = contained(content, pathlib.Path(Sdf.ComputeAssetPathRelativeToLayer(source_attribute.GetPropertyStack()[0].layer, source_attribute.Get().path)))
            payload = normalize_texture(source)
            relative = metadata.relative_to(content).as_posix()
            output_relative = f"textures/{identity}.ntex"
            output = scratch / "assets_database" / output_relative
            output.parent.mkdir(exist_ok=True)
            output.write_bytes(payload)
            existing = next((entry for entry in texture_entries if entry["uid"] == identity), None)
            record = dict(uid=identity, dbPath=output_relative, kind="Texture", sourceType=source.suffix[1:], sourcePath=source.relative_to(content).with_suffix("").as_posix(), nausdPath=relative)
            if existing:
                existing.update(record)
            else:
                texture_entries.append(record)
            seen.update((metadata, source))
        database_file.write_text(json.dumps(texture_database))
    for scene in scenes:
        compile_asset(pathlib.Path(str(scene) + ".nausd"))

    database = json.loads(database_file.read_text())
    entries = database.get("content", [])
    if not entries:
        raise RuntimeError("Native preparation produced an empty asset database")

    # Passive material/texture payloads remain data in this profile. Their
    # authored UID mapping is retained; no GPU format/shader preparation runs.
    for source in sorted(passive - mesh_metadata):
        if source.suffix != ".nausd":
            continue
        stage = Usd.Stage.Open(str(source))
        for prim in stage.Traverse():
            uid_attribute = prim.GetAttribute("uid")
            if not uid_attribute or not uid_attribute.Get():
                continue
            identity = str(uuid.UUID(uid_attribute.Get()))
            if any(entry["uid"] == identity for entry in entries):
                continue
            relative = source.relative_to(content).as_posix()
            payload = f"passive/{identity}.nausd"
            output = scratch / "assets_database" / payload
            output.parent.mkdir(exist_ok=True)
            shutil.copy2(source, output)
            entries.append(dict(uid=identity, dbPath=payload, kind="PassiveData",
                                sourceType="nausd", sourcePath=relative[:-6], nausdPath=relative))
    for entry in entries:
        entry["dbPath"] = entry["dbPath"].replace("\\", "/")
        if not (scratch / "assets_database" / entry["dbPath"]).is_file():
            raise RuntimeError(f"Missing prepared asset {entry['uid']}: {entry['dbPath']}")
        if entry["dbPath"].endswith(".gltf"):
            model_file = scratch / "assets_database" / entry["dbPath"]
            model = json.loads(model_file.read_text())
            if policy and policy["profile"] == RENDERING_PROFILE:
                buffers = model.get("buffers", [])
                if len(buffers) != 1:
                    raise RuntimeError(f"Unsupported geometry buffers: {entry['uid']}")
                uri = buffers[0].get("uri", "")
                if not isinstance(uri, str) or not uri or any(character not in "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-" for character in uri) or uri in (".", ".."):
                    raise RuntimeError(f"Invalid geometry buffer URI: {entry['uid']}")
                buffer_file = contained((scratch / "assets_database").resolve(), model_file.parent / uri)
                try:
                    model, payload = normalize_mesh(model, buffer_file.read_bytes())
                except (ValueError, KeyError, TypeError, OverflowError) as error:
                    raise RuntimeError(f"Unsupported geometry {entry['uid']} ({model_file}): {error}") from error
                model_file.write_text(json.dumps(model))
                buffer_file.write_bytes(payload)
            for dependency in model.get("buffers", []) + model.get("images", []):
                uri = dependency.get("uri", "")
                if uri and not uri.startswith("data:"):
                    payload = contained((scratch / "assets_database").resolve(), model_file.parent / uri)
                    if "byteLength" in dependency and payload.stat().st_size < dependency["byteLength"]:
                        raise RuntimeError(f"Truncated prepared model dependency: {payload}")

    # Check authored components against the real compiler output, before any
    # package can be published. A translator fallback must not erase content.
    for source in scenes:
        stage = Usd.Stage.Open(str(source))
        relative = source.relative_to(content).as_posix()
        record = next((entry for entry in entries if entry["sourcePath"] + "." + entry["sourceType"] == relative), None)
        if not record:
            raise RuntimeError(f"No compiled scene mapping for {relative}")
        data = (scratch / "assets_database" / record["dbPath"]).read_bytes()
        offset = data.index(b"\n\n") + 3
        length = int(next(line.split(b":", 1)[1] for line in data[:offset].splitlines() if line.startswith(b"Content-Length:")))
        header = json.loads(data[offset:offset + length])
        objects = []
        for block in header["objects"]:
            start = offset + length + block["offset"]
            objects.extend(json.loads(data[start:start + block["size"]]))
        components = [component for obj in objects for component in [obj["rootComponent"]] + obj["additionalComponents"]]
        expected_materials = Counter()
        for prim in stage.Traverse():
            type_attribute = prim.GetAttribute("componentTypeName")
            if type_attribute:
                identity = prim.GetAttribute("uid").Get()
                component = next((item for item in components if item["uid"] == identity), None)
                if not component or component.get("componentTypeName") != type_attribute.Get():
                    raise RuntimeError(f"Native compiler lost component {type_attribute.Get()} on {prim.GetPath()}")
                if str(identity) in billboard_textures:
                    metadata = billboard_textures[str(identity)]
                    texture_stage = Usd.Stage.Open(str(metadata))
                    texture_uid = next(str(uuid.UUID(value.Get())) for target in texture_stage.Traverse() if (value := target.GetAttribute("uid")) and value.Get())
                    if component["properties"].get("texture") != "uid:" + texture_uid:
                        raise RuntimeError(f"Native compiler lost billboard texture on {prim.GetPath()}")
                for attribute in prim.GetAttributes():
                    name = str(attribute.GetName())
                    authored = attribute.Get()
                    if name not in component["properties"] or not isinstance(authored, (str, float, int, bool)):
                        continue
                    restored = component["properties"][name]
                    equal = math.isclose(authored, restored, rel_tol=1e-6, abs_tol=1e-6) if isinstance(authored, float) else authored == restored
                    if not equal:
                        raise RuntimeError(f"Native compiler changed {prim.GetPath()}.{name}: {authored!r} -> {restored!r}")
            if str(prim.GetTypeName()) == "NauAssetMesh":
                identity = prim.GetAttribute("uid").Get()
                if not any(str(item["properties"].get("geometry", "")).startswith("uid:" + identity) for item in components):
                    raise RuntimeError(f"Native compiler lost required mesh reference {identity} on {prim.GetPath()}")
                if policy and policy["profile"] == RENDERING_PROFILE:
                    binding = prim.GetAttribute("Material:assign")
                    material_asset = binding.Get() if binding else None
                    if material_asset and material_asset.path:
                        material_source = contained(content, pathlib.Path(Sdf.ComputeAssetPathRelativeToLayer(binding.GetPropertyStack()[0].layer, material_asset.path)))
                        material_stage = Usd.Stage.Open(str(material_source))
                        identities = [str(uuid.UUID(value.Get())) for item in material_stage.Traverse() if (value := item.GetAttribute("uid")) and value.Get()]
                        if len(identities) != 1 or identities[0] not in policy["rendering"]["materials"]:
                            raise RuntimeError(f"Missing or ambiguous portable material mapping on {prim.GetPath()}")
                        expected_materials["uid:" + identities[0]] += 1
        if policy and policy["profile"] == RENDERING_PROFILE:
            actual_materials = Counter(str(item["properties"].get("material", "")) for item in components if item.get("componentTypeName") == "nau::scene::StaticMeshComponent" and item["properties"].get("material"))
            if expected_materials != actual_materials:
                raise RuntimeError(f"Native compiler changed required material references in {source}: {expected_materials} -> {actual_materials}")
            for item in components:
                if item.get("componentTypeName") == "nau::scene::StaticMeshComponent" and not item["properties"].get("material") and "defaultMaterial" not in policy["rendering"]:
                    raise RuntimeError(f"Missing explicit default material for component {item['uid']} in {source}")
    if policy and policy["profile"] == RENDERING_PROFILE:
        for source in scenes:
            relative = source.relative_to(content).as_posix()
            record = next(entry for entry in entries if entry["sourcePath"] + "." + entry["sourceType"] == relative)
            stage = Usd.Stage.Open(str(source))
            authored = {str(value.Get()) for prim in stage.Traverse() if (value := prim.GetAttribute("uid")) and value.Get()}
            cooked = scratch / "assets_database" / record["dbPath"]
            cooked.write_bytes(stable_scene(cooked.read_bytes(), record["uid"], authored))
    database_file.write_text(json.dumps(database, indent=2))

    packaged_config = scratch / "config"
    packaged_config.mkdir()
    material_files = []
    if policy and policy["profile"] == RENDERING_PROFILE:
        settings = policy["rendering"]
        for relative in sorted(set(settings["materials"].values()) | ({settings["defaultMaterial"]} if "defaultMaterial" in settings else set())):
            source = contained(content, content / relative)
            try:
                material = simple_material(json.loads(source.read_text()))
            except (ValueError, KeyError, TypeError) as error:
                raise RuntimeError(f"Invalid portable material {relative}: {error}") from error
            target = scratch / "portable_materials" / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text(json.dumps(material))
            material_files.append(dict(source=str(target), path=relative))
            seen.add(source)
        (packaged_config / "rendering.json").write_text(json.dumps(settings))
    application = json.loads((project / "config/app.json").read_text())
    application.get("app", {}).pop("log", None)
    (packaged_config / "app.json").write_text(json.dumps(application))
    (packaged_config / config_name).write_text(json.dumps(config))
    for scene in scenes[1:]:
        name = scene.stem
        (packaged_config / (name + ".json")).write_text(json.dumps(
            {"scene": {"startupScene": prefix + scene.relative_to(content).as_posix()}}))

    files = material_files.copy()
    if policy and "audio" in policy:
        from portable_audio import validate_wav
        for relative in sorted(set(policy["audio"].values())):
            source = contained(content, content / relative)
            try:
                validate_wav(source.read_bytes())
            except ValueError as error:
                raise RuntimeError(f"Invalid required audio {relative}: {error}") from error
            seen.add(source)
            files.append(dict(source=str(source), path=relative))
        (packaged_config / "audio.json").write_text(json.dumps(policy["audio"]))
    for directory, archive_root in ((packaged_config, "config"), (scratch / "assets_database", "assets_db")):
        for file in sorted(directory.rglob("*")):
            if file.is_file():
                files.append(dict(source=str(file), path=archive_root + "/" + file.relative_to(directory).as_posix()))
    # Retain opaque payload dependencies used by passive material records.
    for file in sorted(passive - mesh_metadata):
        if file.suffix != ".nausd":
            files.append(dict(source=str(file), path="content/" + file.relative_to(content).as_posix()))
    required_manifest = packaged_config / "assets.json"
    required_manifest.write_text(json.dumps({"profile": policy["profile"] if policy else "non-rendering-scene", "requiredFiles": [file["path"] for file in files]}, indent=2))
    files.append(dict(source=str(required_manifest), path="config/assets.json"))
    manifest = scratch / "manifest.json"
    manifest.write_text(json.dumps({"description": "Packaged editor project startup", "files": files}, indent=2))
    archive = scratch / "content.nau_pack"
    subprocess.run([str(args.pack_tool.resolve()), "--manifest", str(manifest), "--output", str(archive)], check=True)
    # Publish only after every native operation succeeds.
    destination.mkdir(exist_ok=True)
    shutil.copy2(archive, destination / "content.nau_pack")
    (destination / "preparation.json").write_text(json.dumps({
        "startup": startup, "scenes": [str(scene.relative_to(content)) for scene in scenes],
        "sourceInputs": [str(file.relative_to(content)) for file in sorted(seen | passive)],
        "files": files, "work": str(scratch)}, indent=2))
    print(f"Prepared {destination / 'content.nau_pack'}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--project", type=pathlib.Path, required=True)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    parser.add_argument("--asset-tool", type=pathlib.Path, required=True)
    parser.add_argument("--pack-tool", type=pathlib.Path, required=True)
    parser.add_argument("--module", type=pathlib.Path, required=True)
    parser.add_argument("--scene", action="append", default=[])
    parser.add_argument("--supported-component", action="append", default=[])
    parser.add_argument("--declaration", type=pathlib.Path)
    try:
        prepare(parser.parse_args())
    except Exception as error:
        print(f"Packaged asset preparation failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

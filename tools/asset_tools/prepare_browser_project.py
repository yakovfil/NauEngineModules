"""Prepare the non-rendering startup asset closure with native USD/asset tools."""
import argparse
import json
import math
import pathlib
import shutil
import subprocess
import sys
import uuid


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
    config = json.loads((project / "config/scene.json").read_text())
    startup = config["scene"]["startupScene"]
    prefix = "asset:/content/"
    if not startup.startswith(prefix):
        raise RuntimeError(f"Unsupported startup scene path: {startup}")
    main = contained(content, content / startup[len(prefix):])
    scenes = [main] + [contained(content, content / path) for path in args.scene]
    supported = set(args.supported_component)
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
    for scene in scenes:
        compile_asset(pathlib.Path(str(scene) + ".nausd"))

    database_file = scratch / "assets_database/database.db"
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
        for prim in stage.Traverse():
            type_attribute = prim.GetAttribute("componentTypeName")
            if type_attribute:
                identity = prim.GetAttribute("uid").Get()
                component = next((item for item in components if item["uid"] == identity), None)
                if not component or component.get("componentTypeName") != type_attribute.Get():
                    raise RuntimeError(f"Native compiler lost component {type_attribute.Get()} on {prim.GetPath()}")
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
    database_file.write_text(json.dumps(database, indent=2))

    packaged_config = scratch / "config"
    packaged_config.mkdir()
    application = json.loads((project / "config/app.json").read_text())
    application.get("app", {}).pop("log", None)
    (packaged_config / "app.json").write_text(json.dumps(application))
    (packaged_config / "scene.json").write_text(json.dumps(config))
    for scene in scenes[1:]:
        name = scene.stem
        (packaged_config / (name + ".json")).write_text(json.dumps(
            {"scene": {"startupScene": prefix + scene.relative_to(content).as_posix()}}))

    files = []
    for directory, archive_root in ((packaged_config, "config"), (scratch / "assets_database", "assets_db")):
        for file in sorted(directory.rglob("*")):
            if file.is_file():
                files.append(dict(source=str(file), path=archive_root + "/" + file.relative_to(directory).as_posix()))
    # Retain opaque payload dependencies used by passive material records.
    for file in sorted(passive - mesh_metadata):
        if file.suffix != ".nausd":
            files.append(dict(source=str(file), path="content/" + file.relative_to(content).as_posix()))
    required_manifest = packaged_config / "assets.json"
    required_manifest.write_text(json.dumps({"profile": "non-rendering-scene", "requiredFiles": [file["path"] for file in files]}, indent=2))
    files.append(dict(source=str(required_manifest), path="config/assets.json"))
    manifest = scratch / "manifest.json"
    manifest.write_text(json.dumps({"description": "Non-rendering editor project startup", "files": files}, indent=2))
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
    try:
        prepare(parser.parse_args())
    except Exception as error:
        print(f"Browser asset preparation failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

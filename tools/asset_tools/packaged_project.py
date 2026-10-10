"""Portable project declaration and prepared archive validation (no USD imports)."""
import argparse
import json
import math
import pathlib
import re
import sys

MODULES = {"PlatformApp", "CoreAssets", "CoreScene", "CoreSceneFormats"}
RENDERING_PROFILE = "basic-rendering-scene"
RENDERING_MODULES = MODULES | {"SceneRenderer"}


def render_configuration(data):
    """Validate the target-independent authored rendering policy."""
    if not isinstance(data, dict) or set(data) - {"materials", "defaultMaterial", "lighting"} or not {"materials", "lighting"} <= set(data):
        raise ValueError("Invalid rendering settings: expected materials and lighting")
    materials = data["materials"]
    if not isinstance(materials, dict):
        raise ValueError("Rendering materials must map source UUIDs to material paths")
    for identity, path in materials.items():
        if not re.fullmatch(r"[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}", identity):
            raise ValueError(f"Invalid rendering material UUID: {identity}")
        material_path(path)
    if "defaultMaterial" in data:
        material_path(data["defaultMaterial"])
    lighting = data["lighting"]
    if not isinstance(lighting, dict) or set(lighting) != {"direction", "ambient", "directional"}:
        raise ValueError("Invalid rendering lighting fields")
    for field, values in lighting.items():
        if not isinstance(values, list) or len(values) != 3 or any(type(value) not in (int, float) or not math.isfinite(value) for value in values):
            raise ValueError(f"Rendering lighting {field} requires three finite numbers")
        if field != "direction" and any(value < 0 or value > 1 for value in values):
            raise ValueError(f"Rendering lighting {field} must be in [0, 1]")
    if abs(sum(value * value for value in lighting["direction"]) - 1) > 0.001:
        raise ValueError("Rendering light direction must be normalized")
    return data


def material_path(value):
    if not isinstance(value, str) or not re.fullmatch(r"materials/[A-Za-z0-9_/-]+\.json", value) or any(part in ("", ".", "..") for part in value.split("/")):
        raise ValueError(f"Invalid portable material path: {value}")
    return value


def simple_material(data):
    if not isinstance(data, dict) or set(data) != {"version", "baseColor"} or type(data["version"]) is not int or data["version"] != 1:
        raise ValueError("Unsupported portable material fields or version")
    color = data["baseColor"]
    if not isinstance(color, list) or len(color) != 3 or any(type(value) not in (int, float) or not math.isfinite(value) or value < 0 or value > 1 for value in color):
        raise ValueError("Portable material baseColor requires three finite linear values in [0, 1]")
    return data


def declaration(path):
    data = json.loads(pathlib.Path(path).read_text(encoding="utf-8-sig"))
    if not isinstance(data, dict) or type(data.get("version")) is not int or data["version"] != 1:
        raise ValueError("Unsupported portable project declaration version")
    if data.get("profile") not in ("non-rendering-scene", RENDERING_PROFILE):
        raise ValueError("Unsupported portable project profile")
    for field in ("modules", "supportedComponents"):
        values = data.get(field)
        if not isinstance(values, list) or not values or any(not isinstance(value, str) or not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_:]*", value) for value in values):
            raise ValueError(f"Invalid portable declaration {field}")
        if len(set(values)) != len(values):
            raise ValueError(f"Duplicate portable declaration {field}")
    required = RENDERING_MODULES if data["profile"] == RENDERING_PROFILE else MODULES
    allowed = required | {"CoreInput", "Audio"} if data["profile"] == RENDERING_PROFILE else required
    if not required <= set(data["modules"]) <= allowed:
        raise ValueError(f"Unsupported or missing portable modules: {data['modules']}")
    if not isinstance(data.get("projectModule"), str) or not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", data["projectModule"]):
        raise ValueError("Invalid portable project module")
    if not isinstance(data.get("startupConfiguration"), str) or not re.fullmatch(r"/config/[A-Za-z0-9_-]+\.json", data["startupConfiguration"]):
        raise ValueError("Invalid portable startup configuration")
    if data["profile"] == RENDERING_PROFILE:
        render_configuration(data.get("rendering"))
    elif "rendering" in data:
        raise ValueError("Rendering settings require the basic-rendering-scene profile")
    if "audio" in data:
        audio = data["audio"]
        if "Audio" not in data["modules"] or not isinstance(audio, dict) or set(audio) != {"click"}:
            raise ValueError("Audio mapping requires Audio module and exactly one click source")
        if not isinstance(audio["click"], str) or not re.fullmatch(r"audio/[A-Za-z0-9_-]+\.wav", audio["click"]):
            raise ValueError("Invalid portable click WAV path")
    return data


def unpack(path):
    raw = pathlib.Path(path).read_bytes()
    separator = raw.find(b"\n\n\n", 0, 4096)
    if separator < 0 or not raw.startswith(b"NauContent-Kind: nau-vfs-pack\n"):
        raise ValueError("Invalid prepared archive header")
    length = next((line.partition(b":")[2].strip() for line in raw[:separator].splitlines() if line.startswith(b"Content-Length:")), b"")
    if not length.isdigit() or int(length) > 16 * 1024 * 1024:
        raise ValueError("Invalid prepared archive header length")
    start = separator + 3
    header = json.loads(raw[start:start + int(length)])
    payload = raw[start + int(length):]
    if header.get("version") != "0.1" or not isinstance(header.get("content"), list):
        raise ValueError("Unsupported prepared archive")
    files = {}
    for entry in header["content"]:
        name = entry["filePath"]
        if not isinstance(name, str) or not name or name.startswith("/") or "\\" in name or any(part in ("", ".", "..") for part in name.split("/")) or name in files:
            raise ValueError(f"Invalid prepared archive path: {name}")
        offset, size = entry["blobData"]["offset"], entry["blobData"]["size"]
        if type(offset) is not int or type(size) is not int or min(offset, size) < 0 or offset + size > len(payload) or entry.get("contentCompression") or entry.get("clientSize") != size:
            raise ValueError(f"Invalid prepared archive entry: {name}")
        files[name] = payload[offset:offset + size]
    return files


def validate_archive(path, policy):
    files = unpack(path)
    inputs = json.loads(files["config/assets.json"])
    if inputs.get("profile") != policy["profile"] or not inputs.get("requiredFiles"):
        raise ValueError("Prepared archive profile mismatch")
    for name in inputs["requiredFiles"]:
        if name not in files or not files[name]:
            raise ValueError(f"Missing required prepared entry: {name}")
    for name in (policy["startupConfiguration"].lstrip("/"), "config/app.json", "assets_db/database.db"):
        if not isinstance(json.loads(files[name]), dict):
            raise ValueError(f"Invalid prepared JSON: {name}")
    if "audio" in policy:
        from portable_audio import validate_wav
        if json.loads(files["config/audio.json"]) != policy["audio"]:
            raise ValueError("Prepared audio mapping mismatch")
        validate_wav(files[policy["audio"]["click"]])
    if policy["profile"] == RENDERING_PROFILE:
        from portable_render_assets import normalize_mesh
        settings = render_configuration(json.loads(files["config/rendering.json"]))
        if settings != policy["rendering"]:
            raise ValueError("Prepared rendering settings do not match the declaration")
        for name in set(settings["materials"].values()) | ({settings["defaultMaterial"]} if "defaultMaterial" in settings else set()):
            simple_material(json.loads(files[name]))
        database = json.loads(files["assets_db/database.db"])
        for entry in database.get("content", []):
            if not entry["dbPath"].endswith(".gltf"):
                continue
            name = "assets_db/" + entry["dbPath"]
            model = json.loads(files[name])
            buffers = model.get("buffers", [])
            if len(buffers) != 1:
                raise ValueError(f"Invalid prepared geometry buffers: {name}")
            uri = buffers[0].get("uri", "")
            if not isinstance(uri, str) or not re.fullmatch(r"[A-Za-z0-9_.-]+", uri) or uri in (".", ".."):
                raise ValueError(f"Invalid prepared geometry buffer URI: {name}")
            binary = str(pathlib.PurePosixPath(name).parent / uri)
            if model["accessors"][model["meshes"][0]["primitives"][0]["indices"]]["componentType"] != 5123:
                raise ValueError(f"Prepared geometry requires uint16 indices: {name}")
            normalize_mesh(model, files[binary])


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--declaration", required=True)
    parser.add_argument("--archive")
    args = parser.parse_args()
    try:
        policy = declaration(args.declaration)
        if args.archive:
            validate_archive(args.archive, policy)
        print(json.dumps(policy))
    except (ValueError, KeyError, TypeError, OSError) as error:
        print(f"Portable project validation failed: {error}", file=sys.stderr)
        sys.exit(1)

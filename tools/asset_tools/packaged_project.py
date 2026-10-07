"""Portable project declaration and prepared archive validation (no USD imports)."""
import argparse
import json
import pathlib
import re
import sys

MODULES = {"PlatformApp", "CoreAssets", "CoreScene", "CoreSceneFormats"}


def declaration(path):
    data = json.loads(pathlib.Path(path).read_text(encoding="utf-8-sig"))
    if not isinstance(data, dict) or type(data.get("version")) is not int or data["version"] != 1:
        raise ValueError("Unsupported portable project declaration version")
    if data.get("profile") != "non-rendering-scene":
        raise ValueError("Unsupported portable project profile")
    for field in ("modules", "supportedComponents"):
        values = data.get(field)
        if not isinstance(values, list) or not values or any(not isinstance(value, str) or not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_:]*", value) for value in values):
            raise ValueError(f"Invalid portable declaration {field}")
        if len(set(values)) != len(values):
            raise ValueError(f"Duplicate portable declaration {field}")
    if set(data["modules"]) != MODULES:
        raise ValueError(f"Unsupported or missing portable modules: {data['modules']}")
    if not isinstance(data.get("projectModule"), str) or not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", data["projectModule"]):
        raise ValueError("Invalid portable project module")
    if not isinstance(data.get("startupConfiguration"), str) or not re.fullmatch(r"/config/[A-Za-z0-9_-]+\.json", data["startupConfiguration"]):
        raise ValueError("Invalid portable startup configuration")
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

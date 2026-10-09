"""Stabilize generated cooked scene identities without rewriting authored UIDs."""
import json
import re
import uuid

UUID_PATTERN = re.compile(r"[0-9a-fA-F]{8}-(?:[0-9a-fA-F]{4}-){3}[0-9a-fA-F]{12}")


def read_scene(data):
    offset = data.index(b"\n\n") + 3
    length = int(next(line.split(b":", 1)[1] for line in data[:offset].splitlines() if line.startswith(b"Content-Length:")))
    header = json.loads(data[offset:offset + length])
    payload = data[offset + length:]
    objects = []
    for block in header["objects"]:
        objects.extend(json.loads(payload[block["offset"]:block["offset"] + block["size"]]))
    return header, objects


def stable_scene(data, scene_uid, authored_uids):
    header, objects = read_scene(data)
    namespace = uuid.UUID(scene_uid)
    authored = {str(uuid.UUID(value)) for value in authored_uids}
    identities, local_ids = {}, set()
    original_ids = set()
    for obj in objects:
        local_id = obj["localId"]
        if local_id in local_ids:
            raise ValueError(f"Duplicate prepared scene local ID: {local_id}")
        local_ids.add(local_id)
        members = [(obj, f"object/{local_id}")]
        members += [(component, f"object/{local_id}/component/{slot}") for slot, component in enumerate([obj["rootComponent"]] + obj["additionalComponents"])]
        for member, name in members:
            identity = str(uuid.UUID(member["uid"]))
            if identity in original_ids:
                raise ValueError(f"Duplicate prepared scene identity: {identity}")
            original_ids.add(identity)
            identities[identity] = identity if identity in authored else str(uuid.uuid5(namespace, name))
    if len(set(identities.values())) != len(identities):
        raise ValueError("Stable prepared scene identity collision")
    for obj in objects:
        if obj["parentLocalId"] and obj["parentLocalId"] not in local_ids or any(child not in local_ids for child in obj["childLocalIds"]):
            raise ValueError("Prepared scene contains an unresolved hierarchy link")

    def remap(value):
        if isinstance(value, str):
            return UUID_PATTERN.sub(lambda match: identities.get(match.group().lower(), match.group()), value)
        if isinstance(value, list):
            return [remap(item) for item in value]
        if isinstance(value, dict):
            return {remap(key): remap(item) for key, item in value.items()}
        return value

    objects = sorted(remap(objects), key=lambda obj: obj["localId"])
    header = remap(header)
    chunks, blocks, offset = [], [], 0
    for start in range(0, len(objects), 20):
        raw = json.dumps(objects[start:start + 20], separators=(",", ":"), sort_keys=True).encode()
        chunks.append(raw)
        blocks.append(dict(offset=offset, size=len(raw)))
        offset += len(raw)
    header["objects"] = blocks
    raw = json.dumps(header, separators=(",", ":"), sort_keys=True).encode()
    return f"NauContent-Kind: nau-scene\nContent-Type: application/json\nContent-Length: {len(raw)}\n\n\n".encode() + raw + b"".join(chunks)

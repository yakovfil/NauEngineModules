"""Checked normalization of the basic rendering profile's static mesh subset."""
import copy
import math
import struct


def geometry_transform(model):
    identity = [1., 0., 0., 0., 0., 1., 0., 0., 0., 0., 1., 0., 0., 0., 0., 1.]
    nodes = model.get("nodes", [])
    if not nodes:
        return identity
    def multiply(a, b):
        return [sum(a[k*4+row]*b[column*4+k] for k in range(4)) for column in range(4) for row in range(4)]
    def vector(node, field, default):
        values = node.get(field, default)
        if not isinstance(values, list) or len(values) != len(default) or any(type(value) not in (int, float) or not math.isfinite(value) for value in values):
            raise ValueError(f"Invalid portable mesh node {field}")
        return values
    parents = set()
    for node in nodes:
        if not isinstance(node, dict) or any(field in node for field in ("skin", "extensions", "weights", "camera")):
            raise ValueError("Unsupported portable mesh node")
        for child in node.get("children", []):
            if type(child) is not int or not 0 <= child < len(nodes) or child in parents:
                raise ValueError("Invalid or shared portable mesh node")
            parents.add(child)
    roots = [index for index in range(len(nodes)) if index not in parents]
    if not roots:
        raise ValueError("Portable mesh node hierarchy has a cycle")
    instances = []
    visited = set()
    def visit(index, parent):
        if index in visited:
            raise ValueError("Portable mesh node hierarchy has a cycle")
        visited.add(index)
        node = nodes[index]
        if "matrix" in node:
            if any(field in node for field in ("rotation", "scale", "translation")):
                raise ValueError("Portable mesh node mixes matrix and TRS")
            local = vector(node, "matrix", identity)
            if local[3] != 0 or local[7] != 0 or local[11] != 0 or local[15] != 1:
                raise ValueError("Portable mesh requires an affine node matrix")
        else:
            x, y, z, w = vector(node, "rotation", [0, 0, 0, 1])
            if abs(x*x+y*y+z*z+w*w-1) > .001:
                raise ValueError("Portable mesh node quaternion is not normalized")
            sx, sy, sz = vector(node, "scale", [1, 1, 1])
            if min(sx, sy, sz) <= 0:
                raise ValueError("Portable mesh node scale must be positive")
            tx, ty, tz = vector(node, "translation", [0, 0, 0])
            local = [(1-2*(y*y+z*z))*sx, 2*(x*y+z*w)*sx, 2*(x*z-y*w)*sx, 0,
                     2*(x*y-z*w)*sy, (1-2*(x*x+z*z))*sy, 2*(y*z+x*w)*sy, 0,
                     2*(x*z+y*w)*sz, 2*(y*z-x*w)*sz, (1-2*(x*x+y*y))*sz, 0, tx, ty, tz, 1]
        world = multiply(parent, local)
        if "mesh" in node:
            if type(node["mesh"]) is not int or node["mesh"] != 0:
                raise ValueError("Portable mesh node references an unsupported mesh")
            instances.append(world)
        for child in node.get("children", []):
            visit(child, world)
    for root in roots:
        visit(root, identity)
    if len(visited) != len(nodes) or len(instances) != 1:
        raise ValueError("Portable geometry requires one reachable mesh instance")
    return instances[0]


def normalize_mesh(model, payload):
    """Return packed static triangles with the geometry hierarchy baked once."""
    if not isinstance(model, dict) or model.get("asset", {}).get("version") != "2.0":
        raise ValueError("Portable mesh requires glTF 2.0")
    for field in ("extensionsRequired", "extensionsUsed", "animations", "skins", "materials"):
        if field in model:
            raise ValueError(f"Unsupported portable mesh field: {field}")
    if len(model.get("meshes", [])) != 1 or len(model.get("buffers", [])) != 1:
        raise ValueError("Portable mesh requires one mesh and one external buffer")
    buffer = model["buffers"][0]
    if type(buffer.get("byteLength")) is not int or not 0 < buffer["byteLength"] <= 16*1024*1024 or len(payload) != buffer["byteLength"]:
        raise ValueError("Invalid portable mesh buffer length")
    mesh = model["meshes"][0]
    if "extensions" in mesh or len(mesh.get("primitives", [])) != 1:
        raise ValueError("Portable mesh requires one primitive without extensions")
    primitive = mesh["primitives"][0]
    if primitive.get("mode", 4) != 4 or any(field in primitive for field in ("targets", "extensions", "material")):
        raise ValueError("Unsupported portable mesh primitive")
    attributes = primitive.get("attributes", {})
    if not {"POSITION", "NORMAL"} <= set(attributes) or set(attributes) - {"POSITION", "NORMAL", "TEXCOORD_0", "TANGENT"}:
        raise ValueError("Unsupported portable mesh vertex attributes")
    accessors = model.get("accessors", [])
    views = model.get("bufferViews", [])
    if not 0 < len(accessors) <= 64 or not 0 < len(views) <= 64:
        raise ValueError("Invalid portable mesh accessor/view count")

    def read(index, expected_type, component_types):
        if type(index) is not int or not 0 <= index < len(accessors):
            raise ValueError("Portable mesh accessor index is out of range")
        accessor = accessors[index]
        if accessor.get("type") != expected_type or accessor.get("componentType") not in component_types or any(field in accessor for field in ("sparse", "normalized", "extensions")):
            raise ValueError("Unsupported portable mesh accessor")
        count = accessor.get("count")
        if type(count) is not int or not 0 < count <= 3*65535:
            raise ValueError("Invalid portable mesh accessor count")
        view_index = accessor.get("bufferView")
        if type(view_index) is not int or not 0 <= view_index < len(views):
            raise ValueError("Portable mesh buffer view index is out of range")
        view = views[view_index]
        if view.get("buffer") != 0 or "extensions" in view:
            raise ValueError("Unsupported portable mesh buffer view")
        components = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4}[expected_type]
        format_code = {5126: "f", 5123: "H", 5125: "I"}[accessor["componentType"]]
        size = struct.calcsize("<" + format_code*components)
        offset = accessor.get("byteOffset", 0)
        start = view.get("byteOffset", 0)
        length = view.get("byteLength")
        stride = view.get("byteStride", size)
        if any(type(value) is not int for value in (offset, start, length, stride)) or min(offset, start, length) < 0 or stride < size or stride % struct.calcsize("<"+format_code) or start + length > len(payload) or offset + (count-1)*stride + size > length:
            raise ValueError("Invalid portable mesh buffer range or stride")
        values = [struct.unpack_from("<" + format_code*components, payload, start+offset+i*stride) for i in range(count)]
        if any(not math.isfinite(value) for row in values for value in row):
            raise ValueError("Non-finite portable mesh attribute")
        return values

    positions = read(attributes["POSITION"], "VEC3", {5126})
    normals = read(attributes["NORMAL"], "VEC3", {5126})
    if len(positions) > 65535 or len(normals) != len(positions) or any(sum(value*value for value in normal) < 1e-8 for normal in normals):
        raise ValueError("Invalid portable mesh position/normal count or normal length")
    for name, kind in (("TEXCOORD_0", "VEC2"), ("TANGENT", "VEC4")):
        if name in attributes and len(read(attributes[name], kind, {5126})) != len(positions):
            raise ValueError(f"Mismatched portable mesh {name} count")
    indices = read(primitive.get("indices"), "SCALAR", {5123, 5125})
    if len(indices) % 3 or any(index[0] >= len(positions) for index in indices):
        raise ValueError("Invalid portable mesh triangle indices")
    transform = geometry_transform(model)
    a, b, c = transform[0:3], transform[4:7], transform[8:11]
    def cross(x, y):
        return [x[1]*y[2]-x[2]*y[1], x[2]*y[0]-x[0]*y[2], x[0]*y[1]-x[1]*y[0]]
    cofactor = [cross(b, c), cross(c, a), cross(a, b)]
    determinant = sum(a[i]*cofactor[0][i] for i in range(3))
    if not math.isfinite(determinant) or determinant <= 1e-8:
        raise ValueError("Portable geometry has a singular or reflected node transform")
    baked_positions = [tuple(sum(transform[j*4+i]*p[j] for j in range(3))+transform[12+i] for i in range(3)) for p in positions]
    baked_normals = []
    for normal in normals:
        value = [sum(cofactor[j][i]*normal[j] for j in range(3))/determinant for i in range(3)]
        length = math.sqrt(sum(component*component for component in value))
        if not math.isfinite(length) or length <= 1e-8:
            raise ValueError("Portable geometry has an invalid transformed normal")
        baked_normals.append(tuple(component/length for component in value))
    # Attributes within the supported subset may be unused by this pass but
    # remain in the normalized asset for downstream readers.
    result = copy.deepcopy(model)
    if "nodes" in result:
        result["nodes"] = [dict(mesh=0)]
        result["scenes"] = [dict(nodes=[0])]
        result["scene"] = 0
    result["accessors"] = []
    result["bufferViews"] = []
    result["meshes"][0]["primitives"][0].pop("mode", None)
    normalized_primitive = result["meshes"][0]["primitives"][0]
    normalized_primitive["attributes"] = {}
    output = bytearray()
    def append(values, kind, component_type):
        code = "H" if component_type == 5123 else "f"
        components = len(values[0])
        start = len(output)
        for row in values:
            output.extend(struct.pack("<" + code*components, *row))
        index = len(result["accessors"])
        result["accessors"].append(dict(bufferView=index, componentType=component_type, count=len(values), type=kind))
        result["bufferViews"].append(dict(buffer=0, byteOffset=start, byteLength=len(output)-start))
        return index
    for name, kind in (("POSITION", "VEC3"), ("NORMAL", "VEC3"), ("TEXCOORD_0", "VEC2"), ("TANGENT", "VEC4")):
        if name in attributes:
            values = baked_positions if name == "POSITION" else baked_normals if name == "NORMAL" else read(attributes[name], kind, {5126})
            if name == "TANGENT" and transform != [1., 0., 0., 0., 0., 1., 0., 0., 0., 0., 1., 0., 0., 0., 0., 1.]:
                # Tangent-frame transformation is outside this profile.
                raise ValueError("Transformed tangent data is unsupported")
            normalized_primitive["attributes"][name] = append(values, kind, 5126)
    normalized_primitive["indices"] = append(indices, "SCALAR", 5123)
    result["buffers"][0]["byteLength"] = len(output)
    return result, bytes(output)

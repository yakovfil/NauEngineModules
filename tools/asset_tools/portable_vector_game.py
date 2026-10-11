"""Validate small packaged vector-game assets before native scene cooking."""
import math


def validate_game(data):
    if not isinstance(data, dict) or set(data) != {"version", "geometry", "obstacles"} or type(data["version"]) is not int or data["version"] != 1:
        raise ValueError("Invalid vector game version/fields")
    shapes = data["geometry"]
    if not isinstance(shapes, dict) or not {"tank", "box", "pyramid"} <= shapes.keys() or not 1 <= len(shapes) <= 64:
        raise ValueError("Invalid vector game geometry")
    for name, shape in shapes.items():
        if not isinstance(shape, dict) or set(shape) != {"vertices", "edges"}:
            raise ValueError(f"Invalid vector shape fields: {name}")
        vertices, edges = shape["vertices"], shape["edges"]
        if not 1 <= len(vertices) <= 4096 or not 1 <= len(edges) <= 16384:
            raise ValueError(f"Vector geometry capacity: {name}")
        for vertex in vertices:
            if len(vertex) != 3 or any(type(v) not in (float, int) or not math.isfinite(v) or abs(v) > 10000 for v in vertex):
                raise ValueError(f"Invalid vector vertex: {name}")
        for edge in edges:
            if len(edge) != 2 or any(type(i) is not int or not 0 <= i < len(vertices) for i in edge):
                raise ValueError(f"Invalid vector edge: {name}")
    if not isinstance(data["obstacles"], list) or len(data["obstacles"]) > 128:
        raise ValueError("Invalid obstacle capacity")
    for obstacle in data["obstacles"]:
        if not isinstance(obstacle, dict) or set(obstacle) != {"x", "z", "radius", "pyramid"}:
            raise ValueError("Invalid obstacle fields")
        x, z, r = obstacle["x"], obstacle["z"], obstacle["radius"]
        if any(type(v) not in (float, int) or not math.isfinite(v) for v in (x, z, r)) or abs(x)>90 or abs(z)>90 or not 1<=r<=8 or obstacle["pyramid"] not in (0,1) or math.hypot(x,z)<r+4 or math.hypot(x,z+38)<r+4:
            raise ValueError("Invalid obstacle or occupied opening spawn")
    return data

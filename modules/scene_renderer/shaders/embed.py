"""Embed renderer implementation shaders without modifying project archives."""
import argparse
import json
import pathlib

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--output", type=pathlib.Path, required=True)
parser.add_argument("--vertex", type=pathlib.Path, required=True)
parser.add_argument("--fragment", type=pathlib.Path, required=True)
parser.add_argument("--native-vertex", type=pathlib.Path)
parser.add_argument("--native-fragment", type=pathlib.Path)
parser.add_argument("--billboard-vertex", type=pathlib.Path)
parser.add_argument("--billboard-fragment", type=pathlib.Path)
parser.add_argument("--native-billboard-vertex", type=pathlib.Path)
parser.add_argument("--native-billboard-fragment", type=pathlib.Path)
args = parser.parse_args()
text = "#pragma once\nnamespace nau::render::shaders {\n"
for name, path in (("VertexSource", args.vertex), ("FragmentSource", args.fragment), ("BillboardVertex", args.billboard_vertex), ("BillboardFragment", args.billboard_fragment)):
    if not path:
        continue
    source = path.read_text(encoding="utf-8")
    if not source:
        raise ValueError(f"Empty shader source: {path}")
    text += f"inline constexpr char {name}[] = {json.dumps(source)};\n"
for name, path in (("NativeVertex", args.native_vertex), ("NativeFragment", args.native_fragment), ("NativeBillboardVertex", args.native_billboard_vertex), ("NativeBillboardFragment", args.native_billboard_fragment)):
    if path:
        data = path.read_bytes()
        if not data:
            raise ValueError(f"Empty native shader: {path}")
        text += f"inline constexpr unsigned char {name}[] = {{" + ",".join(str(value) for value in data) + "};\n"
text += "}\n"
args.output.parent.mkdir(parents=True, exist_ok=True)
args.output.write_text(text, encoding="utf-8")

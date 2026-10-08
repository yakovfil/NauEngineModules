import copy
import struct
import unittest

from portable_render_assets import normalize_mesh


class MeshNormalizationTests(unittest.TestCase):
    def mesh(self):
        payload = struct.pack("<18f3I", -1, 0, 0, 0, 1, 0, 1, 0, 0,
                              0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 1, 2)
        model = dict(asset=dict(version="2.0"), buffers=[dict(uri="triangle.bin", byteLength=len(payload))],
                     bufferViews=[dict(buffer=0, byteLength=36), dict(buffer=0, byteOffset=36, byteLength=36),
                                  dict(buffer=0, byteOffset=72, byteLength=12)],
                     accessors=[dict(bufferView=0, componentType=5126, count=3, type="VEC3"),
                                dict(bufferView=1, componentType=5126, count=3, type="VEC3"),
                                dict(bufferView=2, componentType=5125, count=3, type="SCALAR")],
                     meshes=[dict(primitives=[dict(attributes=dict(POSITION=0, NORMAL=1), indices=2, mode=4)])])
        return model, payload

    def test_normalizes_uint32_triangles(self):
        model, payload = self.mesh()
        original = copy.deepcopy(model)
        normalized, packed = normalize_mesh(model, payload)
        self.assertEqual(model, original)
        self.assertEqual(normalized["accessors"][2]["componentType"], 5123)
        self.assertEqual(normalized["bufferViews"][0]["byteOffset"], 0)
        self.assertNotIn("mode", normalized["meshes"][0]["primitives"][0])
        self.assertEqual(packed[:72], payload[:72])
        self.assertEqual(struct.unpack_from("<3H", packed, 72), (0, 1, 2))

    def test_rejects_unsupported_and_corrupt_geometry(self):
        edits = [lambda m: m.update(skins=[]), lambda m: m.update(materials=[]),
                 lambda m: m["meshes"][0]["primitives"][0].update(mode=1),
                 lambda m: m["meshes"][0]["primitives"][0]["attributes"].update(COLOR_0=0),
                 lambda m: m["accessors"][0].update(count=4),
                 lambda m: m["accessors"][0].update(bufferView=99),
                 lambda m: m["bufferViews"][0].update(byteOffset=-1),
                 lambda m: m["bufferViews"][0].update(byteStride=2),
                 lambda m: m["accessors"][2].update(normalized=True)]
        for edit in edits:
            model, payload = self.mesh()
            edit(model)
            with self.subTest(model=model), self.assertRaises(ValueError):
                normalize_mesh(model, payload)
        model, payload = self.mesh()
        for damaged in (payload[:-1], payload[:-4] + struct.pack("<I", 99), struct.pack("<f", float("nan")) + payload[4:]):
            with self.subTest(payload=damaged), self.assertRaises(ValueError):
                normalize_mesh(model, damaged)

    def test_bakes_geometry_transform_once(self):
        model, payload = self.mesh()
        model["nodes"] = [dict(translation=[2, 3, 4], children=[1]), dict(mesh=0, scale=[2, 3, 4])]
        normalized, packed = normalize_mesh(model, payload)
        self.assertEqual(struct.unpack_from("<3f", packed), (0, 3, 4))
        self.assertEqual(struct.unpack_from("<3f", packed, 36), (0, 0, 1))
        self.assertEqual(normalized["nodes"], [dict(mesh=0)])
        model["nodes"][1]["scale"] = [0, 1, 1]
        with self.assertRaises(ValueError):
            normalize_mesh(model, payload)


if __name__ == "__main__":
    unittest.main()

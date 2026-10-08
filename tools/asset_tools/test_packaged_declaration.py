"""Check common Python/CMake profile policy without native cooking prerequisites."""
import copy
import json
import pathlib
import subprocess
import tempfile
import unittest

from packaged_project import declaration, simple_material


class DeclarationTests(unittest.TestCase):
    def test_simple_material(self):
        self.assertEqual(simple_material(dict(version=1, baseColor=[0.1, 0.5, 1]))["baseColor"], [0.1, 0.5, 1])
        for data in (dict(version=2, baseColor=[0, 0, 0]), dict(version=1, baseColor=[0, 0]),
                     dict(version=1, baseColor=[True, 0, 0]), dict(version=1, baseColor=[float("inf"), 0, 0]),
                     dict(version=1, baseColor=[-0.1, 0, 0]), dict(version=1, baseColor=[1.1, 0, 0]),
                     dict(version=1, baseColor=[0, 0, 0], texture="ignored.png")):
            with self.subTest(data=data), self.assertRaises(ValueError):
                simple_material(data)

    def test_target_policy_agreement(self):
        base = dict(version=1, profile="non-rendering-scene", projectModule="Game",
                    startupConfiguration="/config/scene.json",
                    modules=["PlatformApp", "CoreAssets", "CoreScene", "CoreSceneFormats"],
                    supportedComponents=["nau::scene::SceneComponent"])
        rendering = copy.deepcopy(base)
        rendering.update(profile="basic-rendering-scene", modules=base["modules"] + ["SceneRenderer"],
                         rendering=dict(materials={"12345678-1234-1234-1234-123456789abc": "materials/blue.json"},
                                        lighting=dict(direction=[0, 1, 0], ambient=[0.1]*3, directional=[0.7]*3)))
        cases = [("non-rendering", base, True), ("rendering", rendering, True)]
        def invalid(name, edit):
            policy = copy.deepcopy(rendering)
            edit(policy)
            cases.append((name, policy, False))
        invalid("missing-rendering", lambda p: p.pop("rendering"))
        invalid("bad-version", lambda p: p.update(version=2))
        invalid("excluded-module", lambda p: p["modules"].append("Physics"))
        invalid("missing-module", lambda p: p["modules"].remove("SceneRenderer"))
        invalid("duplicate", lambda p: p["supportedComponents"].append(p["supportedComponents"][0]))
        invalid("unknown-field", lambda p: p["rendering"].update(shadows=True))
        invalid("path-traversal", lambda p: p["rendering"].update(defaultMaterial="materials/../blue.json"))
        invalid("bad-uuid", lambda p: p["rendering"].update(materials={"bad": "materials/blue.json"}))
        invalid("non-unit-light", lambda p: p["rendering"]["lighting"].update(direction=[0, 2, 0]))
        invalid("invalid-color", lambda p: p["rendering"]["lighting"].update(ambient=[-1, 0, 0]))
        invalid("boolean-color", lambda p: p["rendering"]["lighting"].update(ambient=[True, 0, 0]))
        invalid("nan", lambda p: p["rendering"]["lighting"].update(direction=[float("nan"), 1, 0]))
        misplaced = copy.deepcopy(rendering)
        misplaced.update(profile="non-rendering-scene", modules=base["modules"])
        cases.append(("misplaced-rendering", misplaced, False))
        validator = pathlib.Path(__file__).resolve().parents[2] / "cmake/NauPackagedProjectDeclaration.cmake"
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            script = root / "validate.cmake"
            script.write_text('cmake_minimum_required(VERSION 3.20)\ninclude("' + validator.as_posix() + '")\nnau_read_packaged_project("${DECLARATION}")\n')
            for name, policy, expected in cases:
                with self.subTest(name=name):
                    source = root / "declaration.json"
                    source.write_text(json.dumps(policy))
                    try:
                        declaration(source)
                        python_valid = True
                    except (ValueError, TypeError):
                        python_valid = False
                    result = subprocess.run(["cmake", f"-DDECLARATION={source.as_posix()}", "-P", str(script)],
                                            cwd=root, capture_output=True, text=True)
                    self.assertEqual(python_valid, expected)
                    self.assertEqual(result.returncode == 0, expected, result.stderr)
                    if expected:
                        header = (root / "generated/nau_project_policy.h").read_text()
                        self.assertIn(policy["profile"], header)
                        self.assertIn("RenderingConfiguration", header)


if __name__ == "__main__":
    unittest.main()

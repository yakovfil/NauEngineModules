"""Exercise the production shader CMake helper with isolated faulty inputs."""
import argparse
import json
import pathlib
import shutil
import subprocess
import tempfile


def verify(output):
    modules = pathlib.Path(__file__).resolve().parents[3]
    helper = modules / "cmake/NauSceneRendererShaders.cmake"
    results = []
    with tempfile.TemporaryDirectory(prefix="nau-shader-delivery-") as temporary:
        root = pathlib.Path(temporary)
        source = root / "source"
        shaders = source / "modules/scene_renderer/shaders"
        shutil.copytree(modules / "modules/scene_renderer/shaders", shaders)
        (source / "CMakeLists.txt").write_text('cmake_minimum_required(VERSION 4.2)\nproject(ShaderCheck LANGUAGES NONE)\nset(NAU_MODULES_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")\ninclude("' + helper.as_posix() + '")\n')
        def command(name, arguments, expected=None):
            completed = subprocess.run(arguments, capture_output=True, text=True)
            text = completed.stdout + completed.stderr
            (output / (name + ".log")).write_text(text)
            assert (completed.returncode != 0) == bool(expected), text
            if expected:
                assert expected.lower() in text.lower(), text
            results.append(dict(name=name, exitCode=completed.returncode))
        configure = ["cmake", "-S", str(source), "-B", str(root / "build"), "-G", "Visual Studio 18 2026", "-A", "x64"]
        command("missing-dxc", configure + ["-DNAU_DXC_EXECUTABLE=" + str(root / "missing-dxc.exe")], "requires native DXC")
        command("valid-configure", configure + ["-U", "NAU_DXC_EXECUTABLE"])
        command("valid-shaders", ["cmake", "--build", str(root / "build"), "--target", "NauSceneShaders", "--config", "Debug"])
        (shaders / "scene.hlsl").write_text("invalid HLSL source")
        command("invalid-hlsl", ["cmake", "--build", str(root / "build"), "--target", "NauSceneShaders", "--config", "Debug"], "error")
        command("missing-glsl", ["python", str(shaders / "embed.py"), "--output", str(root / "unused.h"), "--vertex", str(root / "missing.vert"), "--fragment", str(shaders / "scene.frag")], "missing.vert")
    (output / "results.json").write_text(json.dumps(results, indent=2))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    destination = parser.parse_args().output.resolve()
    destination.mkdir(parents=True, exist_ok=True)
    verify(destination)

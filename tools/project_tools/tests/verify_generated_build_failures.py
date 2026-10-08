"""Exercise generated project freshness and script/preset failure propagation."""
import argparse
import hashlib
import json
import pathlib
import subprocess


def run(command, succeeds=True):
    result = subprocess.run(command, capture_output=True, text=True)
    assert (result.returncode == 0) == succeeds, (result.returncode, result.stdout, result.stderr)
    return result


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def verify(project):
    output = project / "build/acceptance/failures"
    output.mkdir(parents=True, exist_ok=True)
    command = ["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", str(project / "build.ps1"),
               "-Platform", "Windows", "-Configuration", "Debug"]
    archive = project / "build/basic-rendering-scene/windows-debug/package/content.nau_pack"
    material = project / "content/materials/portable_blue.json"
    original = material.read_bytes()
    authored = {p: digest(p) for folder in ["source", "content", "config"] for p in (project / folder).rglob("*") if p.is_file()}
    before = digest(archive)
    try:
        record = json.loads(original)
        record["baseColor"] = [0.12, 0.3, 0.7]
        material.write_text(json.dumps(record))
        result = run(command)
        (output / "changed-material.log").write_text(result.stdout + result.stderr)
        assert digest(archive) != before, "Changed material did not invalidate package"
        # Failed preparation must leave accepted delivery bytes intact and never
        # turn an old package into a successful new build.
        accepted = digest(archive)
        material.write_text('{"version":1,"baseColor":[-1,0,0]}')
        for name, invocation in [("script", command), ("preset", ["cmake", "--build", str(project / "build/presets/portable-windows-debug")])]:
            result = run(invocation, succeeds=False)
            (output / (name + "-invalid-material.log")).write_text(result.stdout + result.stderr)
            assert digest(archive) == accepted
        material.write_bytes(original)
        missing = project / "content/meshes/portable_cube.usda"
        saved = missing.read_bytes()
        missing.unlink()
        try:
            result = run(command, succeeds=False)
            (output / "missing-geometry.log").write_text(result.stdout + result.stderr)
            assert digest(archive) == accepted
        finally:
            missing.write_bytes(saved)
    finally:
        material.write_bytes(original)
    result = run(command)
    (output / "restored.log").write_text(result.stdout + result.stderr)
    assert all(digest(p) == value for p, value in authored.items()), "Preparation changed authored inputs"
    alternate = json.loads((project / "portable_project.json").read_text())
    alternate["profile"] = "non-rendering-scene"
    alternate["modules"].remove("SceneRenderer")
    del alternate["rendering"]
    declaration = output / "non-rendering.json"
    declaration.write_text(json.dumps(alternate))
    result = run(command + ["-Declaration", str(declaration), "-PreparedArchive", str(archive)], succeeds=False)
    (output / "archive-profile-mismatch.log").write_text(result.stdout + result.stderr)
    assert "profile" in (result.stdout + result.stderr).lower()
    (output / "results.json").write_text(json.dumps({"passed": ["material-freshness", "script-invalid-material",
        "preset-invalid-material", "missing-geometry", "authored-inputs-unchanged", "archive-profile-mismatch"]}, indent=2))
    print("PASS six freshness/failure checks")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--project", required=True, type=pathlib.Path)
    verify(parser.parse_args().project.resolve())

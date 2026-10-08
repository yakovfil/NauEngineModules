"""Exercise deployed ProjectTool generation and selected-preset cleanup."""
import argparse
import json
import os
import pathlib
import subprocess
import tempfile


def run(command, *, succeeds=True, env=None):
    result = subprocess.run(command, capture_output=True, text=True, env=env)
    if (result.returncode == 0) != succeeds:
        raise AssertionError((command, result.returncode, result.stdout, result.stderr))
    return result


def verify(tools):
    executable = tools / "ProjectToolCmd.exe"
    with tempfile.TemporaryDirectory(prefix="Nau generated project ") as temporary:
        parent = pathlib.Path(temporary)
        project = parent / "IndependentGame"
        run([str(executable), "--tools", str(tools), "init", "--template", "empty",
             "--project", str(parent), "--name", project.name])
        expected = ["build.ps1", "run_windows.bat", "run_web.bat", "source/portable_main.cpp",
                    "portable_project.json", "CMakePresets.json", ".gitignore", "README.md"]
        for name in expected:
            assert (project / name).is_file(), name
        assert "IndependentGameMain" in (project / "build.ps1").read_text()
        assert "%%(" not in (project / "CMakeLists.txt").read_text()
        assert json.loads((project / "portable_project.json").read_text())["profile"] == "basic-rendering-scene"
        assert "NauDemo" not in (project / "build.ps1").read_text()
        generation_env = os.environ.copy()
        for variable in ["NAU_ENGINE_SDK_DIR", "NAU_ENGINE_SOURCE_DIR", "NAU_MODULES_SOURCE_DIR", "EMSDK"]:
            generation_env.pop(variable, None)
        run([str(executable), "--tools", str(tools), "init", "--template", "empty",
             "--project", str(parent), "--name", "ScaffoldOnly"], env=generation_env)
        run([str(executable), "--tools", str(tools), "init", "--template", "empty",
             "--project", str(parent), "--name", "DefaultConfigured", "--generate"])
        assert (parent / "DefaultConfigured/build/presets/portable-windows-debug/CMakeCache.txt").exists()
        run([str(executable), "--tools", str(tools), "init", "--template", "empty",
             "--project", str(parent), "--name", "WebConfigured", "--generate", "--preset", "portable-web-release"])
        run([str(executable), "--tools", str(tools), "init", "--template", "empty",
             "--project", str(parent), "--name", "UnsupportedIde", "--generate", "--openIde",
             "--preset", "portable-web-release"], succeeds=False)
        run(["cmake", "-S", str(project), "--preset", "portable-windows-debug"])
        run(["cmake", "-S", str(project), "--preset", "portable-web-release"])
        policy = json.loads((project / "portable_project.json").read_text())
        policy["profile"] = "non-rendering-scene"
        policy["modules"].remove("SceneRenderer")
        del policy["rendering"]
        alternate = project / "alternate.json"
        alternate.write_text(json.dumps(policy))
        command = ["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", str(project / "build.ps1")]
        selected = run(command + ["-Platform", "Windows", "-Declaration", str(alternate), "-CleanOnly"])
        assert "non-rendering-scene" in selected.stdout
        run(["cmake", "-S", str(project), "--preset", "portable-windows-release",
             "-DNAU_PROJECT_DECLARATION=" + str(alternate)])
        alternate.write_text('{"profile":"invalid"}')
        run(command + ["-Declaration", str(alternate), "-CleanOnly"], succeeds=False)
        # Both configured outputs and shared inputs must survive selected cleanup.
        preserved = ["build/presets/portable-web-release/sentinel", "build/web-release/sentinel",
                     "build/portable-host-components/sentinel", "build/prepared-debug/sentinel",
                     "build/basic-rendering-scene/prepared-debug/sentinel", "content/sentinel"]
        for name in preserved:
            path = project / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text("preserve")
        target = project / "build/basic-rendering-scene/windows-debug/sentinel"
        target.parent.mkdir(parents=True)
        target.write_text("remove")
        run([str(executable), "clean_rebuild", "--project", str(project), "--name", project.name,
             "--preset", "portable-windows-debug"])
        assert not target.exists()
        for name in preserved:
            assert (project / name).read_text() == "preserve", name
        redirected = parent / "external-cache"
        redirected.mkdir()
        (redirected / "sentinel").write_text("preserve")
        target.parent.mkdir(parents=True, exist_ok=True)
        junction = target.parent / "redirected"
        run(["cmd", "/d", "/c", "mklink", "/J", str(junction), str(redirected)])
        try:
            run(command + ["-Platform", "Windows", "-CleanOnly"], succeeds=False)
            assert (redirected / "sentinel").read_text() == "preserve"
        finally:
            os.rmdir(junction)
        document = json.loads((project / "CMakePresets.json").read_text())
        document["configurePresets"].extend([
            {"name": "unsafe-root", "generator": "Ninja", "binaryDir": "${sourceDir}/build"},
            {"name": "unsafe-outside", "generator": "Ninja", "binaryDir": str(parent)},
            {"name": "unsafe-host", "generator": "Ninja", "binaryDir": "${sourceDir}/build/portable-host-components"},
            {"name": "unsafe-prepared", "generator": "Ninja", "binaryDir": "${sourceDir}/build/basic-rendering-scene/prepared-debug"},
        ])
        (project / "CMakePresets.json").write_text(json.dumps(document))
        for preset in ["unsafe-root", "unsafe-outside", "unsafe-host", "unsafe-prepared"]:
            run([str(executable), "clean_rebuild", "--project", str(project), "--name", project.name,
                 "--preset", preset], succeeds=False)
        for name in preserved:
            assert (project / name).read_text() == "preserve", name
        print("PASS deployed generation, default/alternate policy, invalid declaration, selected cleanup, four unsafe paths and redirected-child rejection")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tools", required=True, type=pathlib.Path)
    verify(parser.parse_args().tools.resolve())

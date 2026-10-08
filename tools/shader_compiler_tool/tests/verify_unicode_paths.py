"""Compile real legacy shaders through native Unicode input and output paths."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--tool", type=Path, required=True)
    parser.add_argument("--sdk", type=Path, required=True)
    args = parser.parse_args()
    tool = args.tool.resolve()
    sdk = args.sdk.resolve()
    template = Path(__file__).resolve().parents[3] / "project_templates/empty/resources/shaders"
    environment = os.environ.copy()
    environment["PATH"] = str(tool.parent) + os.pathsep + environment["PATH"]
    with tempfile.TemporaryDirectory(prefix="Nau shader Unicode ") as scratch:
        root = Path(scratch) / "\u0448\u0435\u0439\u0434\u0435\u0440\u044b spaces"
        shutil.copytree(template, root)
        output = root / "\u043a\u044d\u0448"
        output.mkdir()
        arguments = [str(tool), "-o", str(output), "-s", str(root / "src"),
                     "-m", str(root / "meta"), "-i", str(root / "include"),
                     str(sdk / "include/core/modules/render/include/nau/shaders"),
                     "-c", "\u043a\u044d\u0448.nsbc"]
        result = subprocess.run(arguments, env=environment, capture_output=True, timeout=120)
        assert result.returncode == 0, (result.returncode, result.stdout, result.stderr)
        cache = output / "\u043a\u044d\u0448.nsbc"
        assert cache.stat().st_size > 1024
        print("PASS: real Unicode shader input, include, cache-name and output paths")


if __name__ == "__main__":
    main()

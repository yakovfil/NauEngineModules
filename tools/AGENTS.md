# Tools guide

Read [the module repository guidelines](../AGENTS.md) before changing tools.
This directory contains host executables and supporting libraries. Keep runtime
package reading in NauEngineLite and module-dependent asset processing here.
Follow the engine's C++20 conventions and `.clang-format`; keep generated assets,
packages, build trees and logs out of commits.

## Choose the tool

| Directory | Targets | Use |
| --- | --- | --- |
| `asset_tools` | `AssetTool`, `AssetToolCmd` | Import/compile project assets, process metadata and update the asset database. |
| `asset_pack_tool` | `AssetPackTool`, `AssetPackToolCmd` | Bundle explicitly listed files into deterministic, uncompressed `.nau_pack` archives without importing or cooking. |
| `build_tools` | `BuildTool`, `BuildToolCmd` | Coordinate project compilation, asset compilation, packaging and output deployment. |
| `project_tools` | `ProjectTool`, `ProjectToolCmd` | Create projects from templates, regenerate CMake projects, upgrade versions and merge `.nauproject` settings. |
| `shader_compiler_tool` | `ShaderCompilerTool` | Compile HLSL plus `.blk` metadata into shader caches with bytecode and reflection. |
| `material_creation_tool` | `MaterialCreationTool` | Generate material JSON from compiled shader reflection; edit initial property values afterward. |
| `usd_tools` | USD libraries and plugins | Translate USD scenes, expose prim/property proxies, process metadata and provide Nau schemas and asset resolvers. |
| `shared` | `Shared` | Logging, filesystem, process execution, arguments, versions and error codes shared by tools. |
| `doc` | None | Tool coding-style documentation. |

`AssetTool` prepares assets, `AssetPackTool` bundles their bytes, and `BuildTool`
coordinates the application distribution. Do not treat these as interchangeable.

## Build host tools

Use VS 2026 C++ tools (v145), CMake 4.2+, Python 3.10 and bootstrapped vcpkg with
`VCPKG_ROOT` set. Initialize recorded submodules. Full tools require the modules
hook, `NAU_CORE_TOOLS=ON` and `BUILD_SHARED_LIBS=ON`; the asset, build, project and
USD tool directories are gated on shared builds. Shader compilation uses DXC.

For an already configured editor Release tree, build selected tools from
NauEngineModules:

```powershell
$repo = (Resolve-Path ..).Path.Replace('\', '/')
$build = "$repo/NauEditorPublic/build/latest-release"
cmake --build $build --config Release --target AssetToolCmd AssetPackToolCmd BuildToolCmd ProjectToolCmd ShaderCompilerTool MaterialCreationTool --parallel 8
$tools = "$build/bin/Release"
```

Configure that tree first using the [editor build instructions](../../NauEditorPublic/README.md#release-with-the-locally-built-openusd-sdk).
Those instructions attach NauEngineModules and NauSamples and select the local
OpenUSD SDK. They also describe Python packages and Qt setup. Qt is required by
that editor build route, not by the focused asset packer below. The local SDK
helper builds Release; do not mix its libraries with Debug binaries.

When running USD-dependent tools directly from `bin/Release`, set the runtime
search paths in the same PowerShell session:

```powershell
$usdSdk = "$repo/NauEngineLite/engine/3rdparty_libs/build/openusd/install"
$pythonRoot = py -3.10 -c 'import sys; print(sys.prefix)'
$env:PATH = "$tools;$pythonRoot;$pythonRoot/Scripts;$usdSdk/bin;$usdSdk/lib;$env:PATH"
$env:PYTHONPATH = "$usdSdk/lib/python"
$env:PXR_PLUGINPATH_NAME = "$tools/plugins;$usdSdk/plugin/usd"
```

Preserve the matching DLLs and plugin metadata. Missing `usdGltf_plugin` requires
building the Adobe plugin through the [SDK helper](../../NauEngineLite/doc/openusd.md),
then rebuilding/deploying the tools. Do not substitute unrelated `fmt.dll` or USD
binaries. Close running tools before rebuilding files they have loaded.

## Use the command-line tools

Examples below reuse `$repo` and `$tools` from above. Replace application paths,
shader names and pipeline names with real inputs. Run commands individually and
check `$LASTEXITCODE`; an existing output does not prove the last command succeeded.
Use the executable's `--help` and current `cmd/src/main.cpp` for accepted flags;
some older command documentation still names VS 2022 presets.

```powershell
$project = 'C:/path/to/MyGame'
& "$tools/ProjectToolCmd.exe" --tools "$repo/NauEngineModules" init --template empty --project $project --name MyGame
& "$tools/AssetToolCmd.exe" import --project $project
& "$tools/BuildToolCmd.exe" build --project $project --config Release --preset win_vs2026_x64_dll --targetDir 'C:/path/to/MyGame-output'
```

`ProjectToolCmd --tools` points to the directory containing `project_templates`,
not the templates directory itself. `init --generate` also generates the code
solution; generated projects need their SDK/toolchain environment provisioned.
`clean_rebuild`, `upgrade` and `save` modify an existing project. Asset importing
can target one file with `--file`; omission processes the project's assets.
Build output must be separate from the source project.

For shared non-rendering Windows/web startup, use
[`asset_tools/prepare_packaged_project.py`](asset_tools/prepare_packaged_project.py)
and the [portable project integration guide](../cmake/packaged_project/README.md).
It accepts native asset/pack tools and a project registration DLL, prepares a fresh
scratch copy of the startup dependency closure, validates cooked components and
packages configuration, mappings and required payloads. Its outputs stay under
the consumer project's ignored `build/` directory; authored content is unchanged.
`AssetToolCmd import --modules <absolute-project-module.dll>` registers project
component factories before cooking. Single-file compilation failures return a
failing exit code. USD/Python/plugins remain native prerequisites.

```powershell
& "$tools/ShaderCompilerTool.exe" -o 'C:/path/to/cache' -s 'C:/path/to/shaders' -m 'C:/path/to/metafiles' -c shader_cache.nsbc
& "$tools/MaterialCreationTool.exe" -o 'C:/path/to/material.json' -c 'C:/path/to/cache/shader_cache.nsbc' -p Main VertexShader PixelShader
```

Each shader needs corresponding `.blk` metadata describing stages and
permutations. Material creation consumes names in the compiled cache, not HLSL
filenames; it initializes reflected properties and does not validate the material.
See the [shader guide](shader_compiler_tool/README.md),
[material guide](material_creation_tool/README.md), and command references for
[assets](asset_tools/cmd/doc/cmd.md), [projects](project_tools/cmd/doc/cmd.md) and
[builds](build_tools/cmd/doc/cmd.md).

## Build and use only the asset packer

This focused native graph needs the sibling engine and recorded googletest
sources, but not Qt, USD, graphics or the application framework. From NauEngineModules:

```powershell
cmake -S cmake/probes/asset_pack -B build/asset-pack-host -G "Visual Studio 18 2026" -A x64
cmake --build build/asset-pack-host --config Release --target AssetPackToolCmd --parallel 8
& ./build/asset-pack-host/bin/Release/AssetPackToolCmd.exe --manifest manifest.json --output content.nau_pack
```

Example `manifest.json`:

```json
{"description":"Application assets","files":[{"source":"assets/cube.bin","path":"demo/cube.bin"}]}
```

Source paths are relative to the manifest directory. Archive paths are canonical
relative paths with forward slashes; traversal, duplicate entries and file/directory
conflicts are rejected. Inspect an existing `<output>.staging` file before deciding
how to recover from a previous failure. For browser builds, run this native host
executable separately; never execute a WebAssembly target as a build tool.
See the [package guide](asset_pack_tool/README.md).

## USD support and validation

- `usd_translator`: USD-to-Nau scene/component adapters.
- `usd_proxy`: prim/property wrappers, decorators and stage-change watching.
- `usd_helpers`: reading/writing USD attributes through Nau runtime values.
- `usd_meta_tools`: metadata processors and required-plugin loading.
- `plugins`: Nau file formats/resolvers, UID lookup, translator wrapper, and
  component, asset metadata, animation, audio, input, physics and VFX schemas.

The retired Cocos UI implementation is unsupported. Do not restore it through
asset compilers, scene translators or schema integration.

For tool changes, build the affected executable/library and run its focused
tests. Full tool tests require a tools-enabled shared build with
`NAU_CORE_TESTS=ON`; the editor build disables core tests. Test executables under
`tests/` are not all necessarily enabled or registered: inspect their CMake and
use `ctest --test-dir <build> -C Release -N` before choosing a test filter.

For the focused asset pack graph, build all test targets before running CTest:

```powershell
cmake --build build/asset-pack-host --config Release --parallel 8
ctest --test-dir build/asset-pack-host -C Release --output-on-failure
```

Repeat relevant tests for Debug when changing portable package code. Report the
configuration and actual checks performed; help output or a successful compile
alone does not establish successful importing, packaging or project generation.

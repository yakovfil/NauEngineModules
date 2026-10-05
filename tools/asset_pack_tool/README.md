# Asset packages

`AssetPackTool` builds uncompressed `nau-vfs-pack` version 0.1 archives. `AssetPackToolCmd` accepts an explicit JSON manifest and publishes a validated archive. It copies source bytes without asset importing or cooking.

```json
{"description":"Application assets","files":[{"source":"assets/cube.bin","path":"demo/cube.bin"}]}
```

Source paths resolve against the manifest directory. Package paths must be canonical relative slash-separated paths, with no traversal, duplicates or file/directory conflicts. Entries are sorted by package path; timestamps and host paths are excluded, so unchanged inputs produce identical bytes.

Build the focused host tool with Visual Studio 2026 and CMake 4.2:

```powershell
cmake -S NauEngineModules/cmake/probes/asset_pack -B NauDemo/build/asset-pack-host -G "Visual Studio 18 2026" -A x64
cmake --build NauDemo/build/asset-pack-host --config Release --target AssetPackToolCmd --parallel 8
NauDemo/build/asset-pack-host/bin/Release/AssetPackToolCmd.exe --manifest manifest.json --output content.nau_pack
```

The focused graph includes the standalone kernel and its bundled dependencies, not USD, editor, graphics or application framework. Runtime package access belongs to NauEngineLite. For cross builds, run the host executable separately; never execute a WebAssembly target as a build tool.

Failure returns nonzero with a diagnostic. The CLI writes `<output>.staging`, validates it, then replaces the destination. Existing staging files cause a diagnostic rather than being overwritten. Failed publication cleans the tool's staging file and preserves any previous output. Callers must check the exit status; an older archive is not evidence of a successful build.

Run focused native reader/writer/CLI tests with `ctest --test-dir <build> -C Debug --output-on-failure`, and repeat for Release. Configure the same probe using the supported Emscripten toolchain for portable reader tests in installed Chrome; `ctest --test-dir <web-build> --output-on-failure` runs them through Python/Playwright. These optional test targets require recorded googletest sources.

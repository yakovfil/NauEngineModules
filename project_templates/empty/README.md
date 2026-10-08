# Generated Nau code project

The portable application defaults to basic-rendering-scene and displays the
starter cube/tetrahedron scene. Windows and web share source and content policy.

Configure dependency locations in your environment or ignored CMakeUserPresets.json:

```powershell
$env:NAU_ENGINE_SOURCE_DIR = 'C:/src/NauEngineLite'
$env:NAU_MODULES_SOURCE_DIR = 'C:/src/NauEngineModules'
$env:NAU_ENGINE_SDK_DIR = 'C:/sdk/nau'
$env:NAU_HOST_TOOLS_DIR = 'C:/tools/nau'
$env:NAU_USD_SDK_DIR = 'C:/sdk/openusd'
$env:EMSDK = 'C:/tools/emsdk'
./run_windows.bat Debug
./run_web.bat Release
cmake --preset portable-windows-debug
cmake --build --preset portable-windows-debug
cmake --preset portable-web-release
cmake --build --preset portable-web-release
```

Both scripts and preset builds prepare current content, compile and stage it.
Provision VS 2026/v145, CMake 4.2+, Ninja, native Nau asset/pack tools, the matching
shared SDK and OpenUSD 24.08/Python 3.10. Web additionally requires Emscripten 6.0.9.
Nothing installs SDKs automatically. Native builds do not require EMSDK.

`build.ps1` supports explicit EngineDirectory, ModulesDirectory, HostSdk, HostTools,
UsdSdk and PythonExecutable overrides. The authoritative portable default is
portable_project.json. Use -Declaration for an alternate supported policy; presets
accept -DNAU_PROJECT_DECLARATION=<path>. For an already prepared archive, use
-PreparedArchive <path> or -DNAU_PROJECT_PREPARED_ARCHIVE=<path> in presets.
This mode validates the snapshot but does not refresh it from authored inputs.

Outputs are under build/basic-rendering-scene/<platform>-<configuration>/package;
non-rendering-scene uses build/<platform>-<configuration>/package. Host registration
uses build/portable-host-components; preset orchestration uses build/presets.
`build.ps1 -Platform Web -Configuration Debug -CleanOnly` removes only that target
cache/package. Preset targets named <ProjectName>MainClean do the same. Shared
prepared assets, host registration and other targets survive.

The original win_vs2026_x64 and win_vs2026_x64_dll presets select the legacy
Windows SDK route. Existing editor controls continue to use the native route and
native shared game module. Legacy application builds prepare desktop shaders into
build/legacy-resources with Python 3.10 and the native shader tool; that resource
copy is used by the generated legacy VFS configuration. The legacy configuration uses config/scene.json;
portable rendering uses config/render_scene.json. Neither route is a promise of
full desktop/web graphics feature parity. Keep the complete web package together,
serve with its supplied isolation-header server, and hard-refresh once when
switching from an older cached delivery.

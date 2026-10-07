# Non-rendering browser projects

The reusable profile supplies PlatformApp, CoreAssets, CoreScene, the portable
CoreSceneFormats reader and NauFrameworkScene. Project code links statically.
Native SDK binaries, USD, Qt, graphics, physics, animation, audio and input service
implementations are excluded from the WebAssembly graph. Unsupported entries in
`NAU_BROWSER_PROJECT_MODULES` fail configuration.

Configure the standalone engine with the consumer hook, using an active external
Emscripten 6.0.9 SDK:

```powershell
cmake -S C:/src/NauEngineLite -B C:/src/MyProject/build/web-debug -G Ninja `
  "-DCMAKE_TOOLCHAIN_FILE=$env:EMSDK/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake" `
  -DCMAKE_BUILD_TYPE=Debug -DNAU_BUILD_BROWSER=ON -DNAU_RUNTIME_PROFILE=scene `
  -DBUILD_SHARED_LIBS=OFF -DNAU_CORE_TOOLS=OFF -DNAU_CORE_TESTS=OFF `
  -DCMAKE_PROJECT_NauEngine_INCLUDE=C:/src/NauEngineModules/cmake/attach_browser_project.cmake `
  -DNAU_BROWSER_PROJECT_SOURCE_DIR=C:/src/MyProject `
  -DNAU_BROWSER_PROJECT_ARCHIVE=C:/src/MyProject/build/prepared-debug/content.nau_pack
```

The consumer's CMakeLists must handle `NAU_BROWSER_PROJECT_PROFILE` before its
desktop `project()`/SDK imports. Add its game module and executable, link
NauFrameworkScene and project component code, and use `nau_target_link_modules`
with PlatformApp, CoreAssets, CoreScene, CoreSceneFormats and the game module.
Call `nau_add_compile_options` for the executable, include `NauBrowserProject.cmake`
and call `nau_browser_project_delivery(<target>)`. The `<target>WebPackage` target
stages the application under the engine build's `package` directory; optional
`DESTINATION` selects a separate directory within that build.

Delivery preloads the prepared archive at `/project-package/content.nau_pack`.
Use `createPackagedSceneApplication(PackagedSceneOptions)` with that path and an
explicit supported component type list, then pass the delegate to `runApplication`.
It mounts content, parses packaged configuration, validates required entries and
database mappings, checks component types/properties/references, constructs the
scene on the application thread and awaits activation. Startup failure blocks
Running. The common host owns the delegate and settles asynchronous work before
teardown. Scenes/assets remain owned through service disposal and VFS destruction.
An opt-in activation diagnostics collector turns component/activator errors into
startup failure after their accepted work settles. Ordinary scene consumers keep
their existing best-effort activation policy.

The portable reader shares the desktop compiled-scene implementation. It is not
registered together with CoreAssetFormats. New records contain `componentTypeName`
so native and wasm32 hashes can differ; legacy numeric records remain supported
by native readers. This browser profile requires names and therefore recooking.
MissingComponent fallback remains available in the ordinary scene factory, while
this checked startup path rejects unknown types and excluded service requirements.

## Native preparation

Provision matching native tools, project registration DLL, OpenUSD SDK and its
Python interpreter/plugins before cross-compiling. No native tool is executed as
a WebAssembly target. Native SDK/tool/plugin paths may differ between machines.

`tools/asset_tools/prepare_browser_project.py` accepts `--project`, `--output`,
`--asset-tool`, `--pack-tool`, `--module`, repeated `--supported-component` and
optional repeated `--scene` fixtures. Run it with the native USD Python environment
described in the tools guide. The output must be a child of the project's `build`
directory. It copies authored content into a fresh scratch project, validates USD
dependencies, imports referenced meshes before scenes, checks compiled component
properties against authored data, validates generated model payload dependencies
and publishes an archive only after all steps succeed.

The archive includes app/scene configuration, asset mappings, compiled scenes,
mesh payloads and required passive data, plus a required-entry manifest. It does
not request GPU views or compile shaders. AssetToolCmd's optional `--modules`
argument loads native project factories before cooking and initializes the
scratch project's content/database mounts. Ordinary importing retains its defaults.

Keep configuration-specific target/preparation directories and a separate native
registration build. Re-run preparation after assets, metadata, configuration or
component definitions change; a failed invocation must stop delivery. This first
integration uses full fresh preparation for reliable invalidation.

## Serve and validate

The staged `serve.py` supplies COOP/COEP headers for installed Chrome. Serve HTTP
in a secure context; do not launch via file URLs. The complete directory is
relocatable, including nested URLs. Inspect `Module.nauRuntime.status` and request
asynchronous Stop with `Module.nauRuntime.stop()`.

Memory starts at 128 MiB and grows up to 512 MiB, configurable through
`NAU_BROWSER_PROJECT_INITIAL_MEMORY`/`NAU_BROWSER_PROJECT_MAXIMUM_MEMORY`. SDK/tool
provisioning remains explicit. The profile is startup support for its selected
components, not arbitrary desktop-module compatibility.

`NauDemo/EditorProject` is the initial consumer and provides build helpers, authored
component fixtures and real-browser acceptance. Its observation executable is a
separate test delivery. The `cmake/probes/scene_startup` consumer exercises the same
reader/profile natively and accepts `NAU_SCENE_TEST_PROJECT_SOURCE_DIR` explicitly;
it contains no hard-coded demo checkout path.

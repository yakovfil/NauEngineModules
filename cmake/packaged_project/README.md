# Portable packaged projects

The `non-rendering-scene` profile builds the same application entry, project
components and packaged-scene startup on Windows and web. It preserves real
scene hierarchy, transforms, camera data and passive static-mesh/material
references, with checked activation, updates and cleanup. Graphics, physics,
animation playback and audio services are excluded. Input is optional in the
`basic-rendering-scene` profile: explicitly add `CoreInput` to its module list to
enable keyboard and pointer queries on Windows and web. The non-rendering profile
continues to exclude input.

## Declare the project

### Basic scene rendering

The rendering profile also supports `nau::scene::BillboardComponent` when it is
included in the declaration's `supportedComponents`. Author `texture` as an
asset reference to `NauAssetTexture` metadata whose `path` names a PNG. Host
preparation preserves its UID and packs a portable RGBA8 texture; Windows and
web consume the same archive. A missing or invalid required texture fails
preparation or startup with an identifying diagnostic.

Billboards are unlit, camera-facing squares centered at component world position.
`screenPercentageSize` is their full width as a fraction of drawable width (default
`0.1`); height equals that width in pixels. Size stays constant with camera
distance/FOV and follows viewport resizing. Parent transforms affect world position,
but quad rotation and size do not use object rotation or scale. Size must be finite
and positive. Centers outside camera depth bounds are not drawn.

The initial texture subset is single-image 8-bit RGB/RGBA PNG, at most 4096 pixels
per dimension. RGB is treated as sRGB, alpha as linear; embedded color profiles
are not converted. The whole texture maps onto the square with linear clamp
sampling. Alpha below `0.01` discards the pixel; remaining pixels are opaque and
depth-tested against meshes and billboards. Soft transparency, source-aspect
sizing and general textured mesh materials are outside this subset.

The native preparation Python interpreter needs Pillow as well as the matching
USD bindings. Install it with `<host-python> -m pip install Pillow`; use
`-PythonExecutable` to select that interpreter. The build helper checks this
dependency before host compilation for declarations supporting billboards.
Projects without billboard preparation do not require this decoder. Runtime
packages need neither Pillow nor PNG decoding libraries.

The optional `basic-rendering-scene` profile adds `SceneRenderer` to the logical
module list. Supply a `rendering` object containing `materials` (source material
UUID to `materials/*.json` path), optional `defaultMaterial`, and `lighting`
with `direction`, `ambient` and `directional` three-number arrays. Direction
points toward the light and must have unit length; RGB values are finite within
`[0,1]`. Material records contain exactly `{"version":1,"baseColor":[r,g,b]}`
in linear RGB. Both backends apply diffuse/ambient lighting and the same sRGB
display conversion. Missing mappings fail unless an explicitly selected default
applies to an unbound mesh.

Preparation accepts static indexed triangle meshes with float positions/normals,
one primitive and external buffer per geometry; supported cooked layouts are
normalized to uint16 indices while preserving asset identities. Hierarchy and
component transforms remain scene data. Skinning, unsupported topology/material
features, malformed ranges and reflected/singular transforms are rejected.

Select the declaration with `-Declaration`. Rendering outputs use isolated
`build/basic-rendering-scene/<platform>-<configuration>` paths. One prepared
archive is usable byte-identically by both targets through `-PreparedArchive`.
Native builds require Windows SDK DXC shader tools (or the CMake
`NAU_DXC_EXECUTABLE` override); browser builds embed GLSL without native shader
tool dependencies. Shader bytes are target implementation resources, separate
from the project archive. The native adapter uses the existing DX12 Render
driver; this profile does not enable full Graphics, physics, animation or audio.

Initial presentation gates Running. Zero drawable size suspends submissions.
Stop settles accepted work and releases graphics resources before window teardown.
Device/context loss is terminal and requires restarting the application/page.

Place a versioned `portable_project.json` in the consumer project:

```json
{
  "version": 1,
  "profile": "non-rendering-scene",
  "modules": ["PlatformApp", "CoreAssets", "CoreScene", "CoreSceneFormats"],
  "projectModule": "MainGameModule",
  "startupConfiguration": "/config/scene.json",
  "supportedComponents": ["nau::scene::SceneComponent", "nau::scene::CameraComponent", "nau::scene::StaticMeshComponent", "MyMover", "MyRotator"]
}
```

This declaration supplies CMake module selection, cooking policy and the
generated `nau_project_policy.h` runtime policy. Unsupported versions/modules,
empty or duplicate lists, invalid identities and invalid configuration paths
fail before building. Component classes must still be registered by the compiled
game module. A declaration does not create factories or enable excluded services.

Handle `NAU_PACKAGED_PROJECT_PROFILE` before the consumer's desktop SDK branch.
Link its executable to `NauFrameworkScene` and its game module, register the
declared modules with `nau_target_link_modules`, and call
`nau_packaged_project_delivery` from `NauPackagedProject.cmake`. Use the generated
header directory `${CMAKE_BINARY_DIR}/generated` for the shared entry. The
packaged-scene delegate's empty archive path selects `content.nau_pack` beside
the native executable or in the browser preload directory. Its internal VFS
paths are identical on both targets.

## Build interface

The reusable PowerShell driver takes explicit source locations and application
target identity. Project wrappers may provide checkout-local defaults:

```powershell
$build = 'C:/src/NauEngineModules/cmake/packaged_project/build.ps1'
& $build -ProjectDirectory C:/src/MyProject -EngineDirectory C:/src/NauEngineLite `
  -ModulesDirectory C:/src/NauEngineModules -ApplicationTarget MyApplication `
  -Platform Windows -Configuration Debug `
  -HostSdk C:/sdk/nau -HostTools C:/tools/nau/bin/Release `
  -UsdSdk C:/sdk/openusd -PythonExecutable C:/Python310/python.exe
```

Normal builds rebuild the host registration DLL and freshly cook a scratch copy
of authored assets. The registration-only consumer CMake branch currently uses
`NAU_WEB_HOST_COMPONENTS=ON` for compatibility. Host SDK/tools, OpenUSD and Python
must match and be provisioned separately. Windows uses VS 2026/v145 and CMake
4.2+. Web adds Ninja and the recorded Emscripten SDK 6.0.9. Native operations do
not discover Emscripten. Runtime targets are built statically from engine/modules
sources, separate from the SDK used for cooking.

Use `-Platform Web` after activating Emscripten. `-Serve [-Port 8000]` serves the
web package with isolation headers and opens the browser; `-Run` launches the
Windows package. Host SDK/tool/USD environment overrides remain supported.
Explicit paths take priority over the known sibling editor/OpenUSD defaults.
Missing prerequisites fail; the driver never installs SDKs.

## Prepare once and deliver twice

To build either target from an existing prepared snapshot, pass
`-PreparedArchive C:/packages/content.nau_pack`. This explicit mode validates the
declaration and archive, skips SDK/USD/cooking-tool discovery and copies the
selected bytes into the target build's `selected-input` directory. It does not
claim the snapshot matches current authored sources. Python is still needed for
archive validation, and target compilers remain prerequisites.

Native delivery copies that archive; browser delivery preloads the identical
bytes at `/project-package/content.nau_pack`. Asset cooking stays on the host.
Both deliveries can be relocated outside the checkout. The native delivery
requires the corresponding MSVC runtime installed on the machine; the browser
requires a secure context and COOP/COEP headers. Neither runtime needs the
cooking tools, USD or installed engine SDK.

The driver serializes builds of a project with `build/portable-build.lock` and
rejects overlapping invocations before changing inputs. The OS releases the
lock handle when the owning process exits; the lock file may remain. Different
projects can build independently. Fresh-preparation failure prevents publishing
or running a stale target. Native delivery uses checked staging/previous
directories; inspect remnants of a failed publication before deleting them.

## Outputs and compatibility

All generated output stays under the consumer's ignored `build/` directory:
`portable-host-components`, `prepared-debug`/`prepared-release`,
`windows-debug`/`windows-release` and `web-debug`/`web-release`. Each target tree
has its own `selected-input` and `package`; acceptance builds add a separate
`probe-package`. Prepared content is shared across targets only when explicitly
selected, so target caches never become host tool inputs.

The former browser hook and preparation command remain forwarding entry points.
Legacy and generic CMake source/archive variables must agree if both are set.
The common driver clears legacy cache variables when migrating its own existing
browser tree. Direct CMake callers use `attach_packaged_project.cmake`,
`NAU_PACKAGED_PROJECT_SOURCE_DIR`, `NAU_PACKAGED_PROJECT_DECLARATION` and
`NAU_PACKAGED_PROJECT_ARCHIVE` with `NAU_RUNTIME_PROFILE=scene`.

The rendered desktop SDK route remains a separate consumer selection. This
profile does not change its launcher, dependencies or asset configuration.

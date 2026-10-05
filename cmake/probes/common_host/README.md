# Common application host acceptance

The consumer source lives in the engine's module-neutral framework tests. This
probe supplies the production platform module; it does not link scenes or graphics.
Tests write structured results under the build's `results/<configuration>/` folder.

From the parent checkout, configure native acceptance:

```powershell
cmake -S NauEngineModules/cmake/probes/common_host -B NauEngineModules/build/common-host-native -G "Visual Studio 18 2026" -A x64
cmake --build NauEngineModules/build/common-host-native --config Debug --parallel 8
ctest --test-dir NauEngineModules/build/common-host-native -C Debug --output-on-failure
cmake --build NauEngineModules/build/common-host-native --config Release --parallel 8
ctest --test-dir NauEngineModules/build/common-host-native -C Release --output-on-failure
```

For browser acceptance, activate Emscripten 6.0.9 and configure the same probe with
Ninja, `CMAKE_BUILD_TYPE=Debug` or `Release`, and the SDK's Emscripten toolchain file.
Build, then run CTest. Python, Playwright and locally installed Chrome are required;
real visibility transitions use Chrome's debugging interface without Playwright's
default page visibility overrides.

Scenarios cover explicit phases, legacy forwarding, fixed and presentation
scheduling, configuration/setup/startup failure, pending startup, Stop within
startup or pre-update, cleanup failure, progress timeout and visibility changes.
Delegate destruction verifies that the application, service provider and fixture
resource are already torn down. Controls are restricted to the dedicated fixture.

`../common_host_scene` builds a separate native scene-dependent test. Build and run
it in Debug and Release with the same Visual Studio commands. It verifies that a
pre-update transform mutation reaches a real scene component before post-update.

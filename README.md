# Nau Engine Modules

This repository owns the Nau feature modules, application framework, module-dependent tools, project templates, and their tests. `NauEngineLite` supplies the kernel, bundled dependencies, and shared CMake functions.

Configure the engine with this repository's project hook to build the modules:

```powershell
cd ../NauEngineLite
cmake --preset win_vs2026_x64 "-DCMAKE_PROJECT_NauEngine_INCLUDE=$((Resolve-Path ../NauEngineModules/cmake/attach_modules.cmake).Path)"
cmake --build build/win_vs2026_x64 --config Debug
ctest --test-dir build/win_vs2026_x64 -C Debug --output-on-failure
```

The hook sets `NAU_MODULES_SOURCE_DIR` and registers this directory before other external consumers. NauSamples and NauDemo include it through their own hooks. The minimal and scene runtime profiles require a consumer-owned runtime file; the minimal file is `cmake/NauMinimalRuntime.cmake`.

The parent Nau checkout currently records this as a local submodule. Change its `.gitmodules` URL to the hosted repository URL when the repository is published.

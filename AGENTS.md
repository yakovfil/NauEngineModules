# Repository Guidelines

`engine/core/modules/` contains feature modules and their tests. `engine/core/app_framework/` contains the application framework and lifecycle tests. Module-dependent tools and templates live in `tools/` and `project_templates/`. `cmake/attach_modules.cmake` integrates this repository with the sibling `NauEngineLite` build.

Read the parent checkout's `AGENTS.md` before editing. Follow the engine's C++20 style and `.clang-format`. Run focused builds and CTest suites for affected modules. Keep generated build outputs out of commits and avoid unrelated third-party edits.

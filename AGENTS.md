# Repository Guidelines

`modules/` contains feature modules and their tests. The application framework, lifecycle tests, and application test support live in the sibling `NauEngineLite/engine/core/app_framework/`. This repository enables their build after module targets are defined and supplies the test module list and project fixture path. Module-dependent tools and templates live in `tools/` and `project_templates/`. `cmake/attach_modules.cmake` integrates this repository with the sibling `NauEngineLite` build.

Read the parent checkout's `AGENTS.md` before editing. Follow the engine's C++20 style and `.clang-format`. Run focused builds and CTest suites for affected modules. Keep generated build outputs out of commits and avoid unrelated third-party edits.

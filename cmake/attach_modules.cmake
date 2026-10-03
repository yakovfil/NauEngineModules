# Loaded by the engine's project() call before its build graph is created.
get_filename_component(NAU_MODULES_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(NAU_MODULES_SOURCE_DIR "${NAU_MODULES_SOURCE_DIR}" CACHE PATH "Engine modules source directory" FORCE)

if(NAU_RUNTIME_PROFILE STREQUAL "minimal")
  set(NAU_EXTERNAL_RUNTIME_FILE "${NAU_MODULES_SOURCE_DIR}/cmake/NauMinimalRuntime.cmake"
    CACHE FILEPATH "Runtime configuration" FORCE)
endif()

list(PREPEND NAU_EXTERNAL_PROJECT_DIRS "${NAU_MODULES_SOURCE_DIR}")
list(REMOVE_DUPLICATES NAU_EXTERNAL_PROJECT_DIRS)
set(NAU_EXTERNAL_PROJECT_DIRS "${NAU_EXTERNAL_PROJECT_DIRS}" CACHE STRING "Consumer projects" FORCE)

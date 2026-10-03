cmake_minimum_required(VERSION 3.20)

if(NOT TARGET NauKernel)
  message(FATAL_ERROR "NauEngineModules must be attached to a NauEngine build. Set CMAKE_PROJECT_NauEngine_INCLUDE to cmake/attach_modules.cmake.")
endif()

option(NAU_ENGINE_MODULES "Build engine modules" ON)
option(NAU_ENGINE_FRAMEWORK "Build application framework" ON)
if(NOT NAU_FRAMEWORK_TARGET)
  set(NAU_FRAMEWORK_TARGET NauFramework)
endif()

if(NAU_ENGINE_MODULES)
  add_subdirectory(engine/core/modules)
endif()

if(NAU_ENGINE_FRAMEWORK)
  add_subdirectory(engine/core/app_framework)
  set_target_properties(${NAU_FRAMEWORK_TARGET} PROPERTIES FOLDER "${NauEngineFolder}/core")
endif()

if(NAU_CORE_TESTS)
  add_subdirectory(engine/core/kernel/tests/test_common)
  add_subdirectory(engine/core/kernel/tests/test_kernel)
  add_subdirectory(engine/core/kernel/tests/test_runtime)
endif()

if(NOT NAU_RUNTIME_PROFILE MATCHES "^(minimal|scene)$")
  if(NAU_CORE_TOOLS OR NAU_FORCE_ENABLE_SHADER_COMPILER_TOOL)
    add_subdirectory("${NAU_ENGINE_SOURCE_DIR}/tools/3rd_party" "${CMAKE_CURRENT_BINARY_DIR}/tools/3rd_party")
    add_subdirectory(tools)
  endif()
  install(DIRECTORY cmake/for_build/ DESTINATION ${CMAKE_INSTALL_PREFIX})
endif()

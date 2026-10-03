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
  # The framework sources live with the engine, but require the module targets above.
  add_subdirectory("${NAU_ENGINE_SOURCE_DIR}/engine/core/app_framework"
    "${CMAKE_CURRENT_BINARY_DIR}/app_framework")
  set_target_properties(${NAU_FRAMEWORK_TARGET} PROPERTIES FOLDER "${NauEngineFolder}/core")
  if(NAU_CORE_TESTS)
    set(NAU_TEST_PROJECT_SOURCE_DIR
      "${CMAKE_CURRENT_SOURCE_DIR}/engine/core/modules/asset_formats/test_assets")
    add_subdirectory("${NAU_ENGINE_SOURCE_DIR}/engine/core/app_framework/tests/support"
      "${CMAKE_CURRENT_BINARY_DIR}/test_support")
  endif()
  if(NAU_RUNTIME_PROFILE STREQUAL "minimal" AND NAU_MINIMAL_TESTS)
    set(NAU_TEST_MODULES ${NAU_MINIMAL_MODULES})
    add_subdirectory("${NAU_ENGINE_SOURCE_DIR}/engine/core/app_framework/tests/test_minimal_app"
      "${CMAKE_CURRENT_BINARY_DIR}/test_minimal_app")
  endif()
endif()

if(NOT NAU_RUNTIME_PROFILE MATCHES "^(minimal|scene)$")
  if(NAU_CORE_TOOLS OR NAU_FORCE_ENABLE_SHADER_COMPILER_TOOL)
    add_subdirectory("${NAU_ENGINE_SOURCE_DIR}/tools/3rd_party" "${CMAKE_CURRENT_BINARY_DIR}/tools/3rd_party")
    add_subdirectory(tools)
  endif()
  install(DIRECTORY cmake/for_build/ DESTINATION ${CMAKE_INSTALL_PREFIX})
endif()

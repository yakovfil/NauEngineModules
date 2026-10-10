set(TargetName CoreInput)

set(ModuleRoot ${CMAKE_CURRENT_SOURCE_DIR})

nau_collect_files(Sources
  RELATIVE ${ModuleRoot}
  DIRECTORIES
    ${ModuleRoot}/src
    ${ModuleRoot}/include
  MASK "*.cpp" "*.h"
  EXCLUDE
    "/platform/.*"
)

if(EMSCRIPTEN)
  set(Sources src/input.cpp src/input_module.cpp src/platform/emscripten/browser_input.cpp)
elseif (${Platform_Windows})

  nau_collect_files(Sources
    DIRECTORIES 
      ${ModuleRoot}/src/platform/windows
      ${ModuleRoot}/include/platform/windows
    RELATIVE ${ModuleRoot}
    MASK "*.cpp" "*.h"
  )
endif()

nau_add_module(${TargetName}
  SOURCES ${Sources}
  FOLDER "${NauEngineFolder}/core/modules"
  EXPORT_AS_LIBRARY
)

target_link_libraries(${TargetName} PRIVATE PlatformAppApi)
if(NOT EMSCRIPTEN)
  if(NAU_PACKAGED_PROJECT_PROFILE)
    target_link_libraries(${TargetName} PRIVATE gainputstatic)
  else()
    target_link_libraries(${TargetName} PRIVATE gainput)
  endif()
endif()

target_include_directories(${TargetName} PRIVATE 
  ${ModuleRoot}/src)

target_include_directories(${TargetName} PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>  
    $<INSTALL_INTERFACE:$<INSTALL_PREFIX>/include/core/modules/input/include>
)

target_precompile_headers(${TargetName} PRIVATE src/pch.h)

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${Sources})

## Module API
nau_collect_files(ApiHeaders
  RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
  DIRECTORIES ${CMAKE_CURRENT_SOURCE_DIR}/include
  MASK "*.h"
)

target_compile_definitions(${TargetName} PRIVATE
  NAU_COREINPUT_BUILD
)
if(NAU_PACKAGED_PROJECT_PROFILE)
  target_compile_definitions(${TargetName} PRIVATE NAU_PORTABLE_INPUT)
endif()

install(DIRECTORY ${ModuleRoot}/include
    DESTINATION ${CMAKE_INSTALL_PREFIX}/include/core/modules/input
    FILES_MATCHING 
      PATTERN "*.h"
      PATTERN "*.hpp"
      PATTERN "*.ipp"
)

nau_install(${TargetName} core)

option(NAU_PORTABLE_INPUT_TESTS "Build focused CoreInput regressions" OFF)
if(NAU_PORTABLE_INPUT_TESTS)
  nau_target_add_compile_options(TARGETS gtest gtest_main gmock)
  add_subdirectory(tests/test_input)
endif()

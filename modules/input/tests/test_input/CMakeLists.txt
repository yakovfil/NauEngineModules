set(TargetName test_input)
set(Sources)

nau_collect_files(Sources
  DIRECTORIES ${CMAKE_CURRENT_SOURCE_DIR}
  MASK "*.cpp" "*.h"
)

add_executable(${TargetName} ${Sources})
target_precompile_headers(${TargetName} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/pch.h)

target_include_directories(${TargetName} PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/../../src
)

target_link_libraries(${TargetName} PRIVATE
  gtest
  gmock
  NauKernel
)

nau_target_link_modules(${TargetName}
  CoreInput
)
if(NOT EMSCRIPTEN)
  if(NAU_PACKAGED_PROJECT_PROFILE)
    target_link_libraries(${TargetName} PRIVATE gainputstatic)
  else()
    target_link_libraries(${TargetName} PRIVATE gainput)
  endif()
endif()

#if(MSVC)
#  set_property(TARGET PlatformApp PROPERTY WINDOWS_EXPORT_ALL_SYMBOLS true)
#  target_link_libraries(${TargetName} PRIVATE delayimp)
#  target_link_options(${TargetName} PRIVATE "/DELAYLOAD:$<TARGET_FILE_BASE_NAME:PlatformApp>.dll")
#endif()


nau_add_compile_options(${TargetName})
if(EMSCRIPTEN)
  target_link_options(${TargetName} PRIVATE -sEXIT_RUNTIME=1)
endif()
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${Sources})
set_target_properties (${TargetName} PROPERTIES
    FOLDER "${NauEngineFolder}/tests"
)

include(GoogleTest)
gtest_discover_tests(${TargetName} DISCOVERY_TIMEOUT 10)

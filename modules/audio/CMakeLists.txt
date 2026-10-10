set(TargetName Audio)

if(NAU_PACKAGED_PROJECT_PROFILE)
  nau_add_module(Audio SOURCES src/portable_audio.cpp FOLDER "${NauEngineFolder}/core/modules" EXPORT_AS_LIBRARY)
  target_include_directories(Audio PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include/core/modules/audio/include>)
  target_link_libraries(Audio PRIVATE miniaudio PlatformAppApi)
  target_compile_definitions(Audio PRIVATE NAU_AUDIO_BUILD)
  if(EMSCRIPTEN)
    target_link_options(Audio INTERFACE -sEXPORTED_RUNTIME_METHODS=HEAPF32)
  endif()
  nau_install(Audio core)
  install(DIRECTORY include/ DESTINATION include/core/modules/audio/include)
  if(NAU_PORTABLE_AUDIO_TESTS)
    nau_target_add_compile_options(TARGETS gtest gtest_main gmock)
    add_subdirectory(tests)
  endif()
  return()
endif()

nau_collect_files(Sources   
    DIRECTORIES ${CMAKE_CURRENT_SOURCE_DIR}/src
    RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
  
    MASK "*.cpp" "*.hpp"
)

nau_collect_files(PublicHeaders
    DIRECTORIES ${CMAKE_CURRENT_SOURCE_DIR}/include
    RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
  
    MASK "*.hpp"
)

# The portable backend owns its module entry and miniaudio implementation.
list(FILTER Sources EXCLUDE REGEX "portable_audio\\.cpp$")

nau_add_module(${TargetName}
    SOURCES ${Sources} ${PublicHeaders}
    FOLDER "${NauEngineFolder}/core/modules"
    EXPORT_AS_LIBRARY
)

target_precompile_headers(${TargetName} PRIVATE src/pch.h)

target_include_directories(${TargetName} PRIVATE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>/src
  $<INSTALL_INTERFACE:$<INSTALL_PREFIX>/include/core/modules/audio/src>
)

target_include_directories(${TargetName} PRIVATE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/backends>
  $<INSTALL_INTERFACE:$<INSTALL_PREFIX>/include/core/modules/audio/src/backends>
)

target_include_directories(${TargetName} PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:$<INSTALL_PREFIX>/include/core/modules/audio/include>
)

target_link_libraries(${TargetName} PRIVATE miniaudio)

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/src PREFIX Source FILES ${Sources})
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/include PREFIX Headers FILES ${PublicHeaders})

target_compile_definitions(${TargetName} PRIVATE NAU_AUDIO_BUILD)

target_link_libraries(${TargetName} PUBLIC CoreScene)
nau_target_link_modules(${TargetName} CoreScene)

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include
    DESTINATION ${CMAKE_INSTALL_PREFIX}/include/core/modules/audio
    FILES_MATCHING 
      PATTERN "*.h"
      PATTERN "*.hpp"
      PATTERN "*.ipp"
)

nau_install(${TargetName} core)

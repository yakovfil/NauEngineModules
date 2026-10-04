add_library(NauScreenText STATIC src/screen_text.cpp)
target_include_directories(NauScreenText PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include/core/modules/screen_text/include>)
target_link_libraries(NauScreenText PUBLIC NauKernel PRIVATE stb)
nau_add_compile_options(NauScreenText)
set_target_properties(NauScreenText PROPERTIES FOLDER "${NauEngineFolder}/core/modules")
if(NOT NAU_RUNTIME_PROFILE STREQUAL "scene")
    nau_install(NauScreenText core)
    install(DIRECTORY include/ DESTINATION include/core/modules/screen_text/include)
endif()

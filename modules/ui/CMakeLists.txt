add_library(NauUi STATIC src/widget.cpp)
target_include_directories(NauUi PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include/core/modules/ui/include>)
target_link_libraries(NauUi PUBLIC NauKernel)
nau_add_compile_options(NauUi)
set_target_properties(NauUi PROPERTIES FOLDER "${NauEngineFolder}/core/modules")
nau_install(NauUi core)
install(DIRECTORY include/ DESTINATION include/core/modules/ui/include)

if(NAU_CORE_TESTS AND NOT EMSCRIPTEN)
    include(GoogleTest)
    add_executable(test_widget tests/test_widget.cpp)
    target_link_libraries(test_widget PRIVATE NauUi gtest)
    nau_add_compile_options(test_widget)
    gtest_discover_tests(test_widget DISCOVERY_TIMEOUT 30)
endif()

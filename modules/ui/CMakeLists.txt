nau_add_module(NauUi EXPORT_AS_LIBRARY
    SOURCES src/nau_widget.cpp src/nau_widget_root_component.cpp src/ui_module.cpp
    FOLDER "${NauEngineFolder}/core/modules")
target_compile_definitions(NauUi PRIVATE NAU_NAUUI_BUILD)
target_include_directories(NauUi PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include/core/modules/ui/include>)
target_link_libraries(NauUi PUBLIC CoreScene)
nau_add_compile_options(NauUi)
set_target_properties(NauUi PROPERTIES FOLDER "${NauEngineFolder}/core/modules")
nau_install(NauUi core)
install(DIRECTORY include/ DESTINATION include/core/modules/ui/include)

if((NAU_CORE_TESTS OR NAU_WIDGET_TESTS) AND NOT EMSCRIPTEN)
    if(NOT TARGET gtest)
        set(gtest_force_shared_crt OFF CACHE BOOL "" FORCE)
        set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
        set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
        add_subdirectory("${NAU_ENGINE_SOURCE_DIR}/engine/3rdparty_libs/googletest"
            "${CMAKE_CURRENT_BINARY_DIR}/googletest")
        nau_target_add_compile_options(TARGETS gtest gtest_main)
    endif()
    include(GoogleTest)
    if(NOT TARGET NauSceneSerialization)
        add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../asset_formats/src/scene"
            "${CMAKE_CURRENT_BINARY_DIR}/scene_serialization")
    endif()
    add_executable(test_widget tests/test_widget.cpp)
    target_link_libraries(test_widget PRIVATE NauUi NauSceneSerialization gtest)
    nau_add_compile_options(test_widget)
    target_include_directories(test_widget PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../scene/src")
    gtest_discover_tests(test_widget DISCOVERY_TIMEOUT 30)
    add_executable(test_scene_factory_extension ../scene/tests/test_scene/test_scene_factory_extension.cpp)
    target_link_libraries(test_scene_factory_extension PRIVATE CoreScene NauSceneSerialization gtest)
    target_include_directories(test_scene_factory_extension PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../scene/src")
    nau_add_compile_options(test_scene_factory_extension)
    gtest_discover_tests(test_scene_factory_extension DISCOVERY_TIMEOUT 30)
endif()

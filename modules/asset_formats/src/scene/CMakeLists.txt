add_library(NauSceneSerialization STATIC
    scene_container_builder.cpp scene_asset_container.cpp scene_serialization.cpp)
target_link_libraries(NauSceneSerialization PUBLIC CoreScene)
target_include_directories(NauSceneSerialization PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../include>
    $<INSTALL_INTERFACE:include/core/modules/asset_formats/include>)
nau_add_compile_options(NauSceneSerialization)
nau_install(NauSceneSerialization core)
install(DIRECTORY ../../include/ DESTINATION include/core/modules/asset_formats/include)

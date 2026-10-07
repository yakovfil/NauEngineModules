# Portable reader for the same compiled scene format used by desktop consumers.
if(NOT NAU_BROWSER_PROJECT_PROFILE)
  return()
endif()
set(TargetName CoreSceneFormats)
set(sceneFormats "${CMAKE_CURRENT_SOURCE_DIR}/../asset_formats/src/scene")
nau_add_module(${TargetName}
  SOURCES src/scene_formats_module.cpp src/checked_scene_startup.cpp src/packaged_scene_application.cpp "${sceneFormats}/scene_asset_container.cpp"
  FOLDER "${NauEngineFolder}/core/modules" EXPORT_AS_LIBRARY)
target_include_directories(${TargetName} PRIVATE "${sceneFormats}")
target_include_directories(${TargetName} PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(${TargetName} PUBLIC CoreScene CoreAssets NauFrameworkScene)
# Container reading is a generic kernel facility omitted by the minimal graph.
if(NAU_RUNTIME_PROFILE MATCHES "^(minimal|scene)$")
  target_sources(NauKernel PRIVATE "${NAU_ENGINE_SOURCE_DIR}/engine/core/kernel/src/io/nau_container.cpp")
endif()

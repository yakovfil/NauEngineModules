# Focused scene dependency graph for application host phase tests.
include("${NAU_ENGINE_SOURCE_DIR}/cmake/NauStandaloneMinimal.cmake")
set(NAU_ENGINE_MODULES ON)
set(NAU_ENGINE_FRAMEWORK ON)
set(NAU_FRAMEWORK_TARGET NauFrameworkScene)
set(NAU_MINIMAL_MODULE_DIRS platform_app assets scene)
list(APPEND NAU_MINIMAL_DEPENDENCY_DIRS tinyimageformat)
list(APPEND NAU_MINIMAL_DEPENDENCY_TARGETS tinyimageformat)

set(NAU_SCENE_CoreAssets_SOURCES
    "${NAU_MODULES_SOURCE_DIR}/modules/assets/src/asset_db_impl.cpp"
    "${NAU_MODULES_SOURCE_DIR}/modules/assets/src/asset_descriptor_impl.cpp"
    "${NAU_MODULES_SOURCE_DIR}/modules/assets/src/asset_file_content_provider.cpp"
    "${NAU_MODULES_SOURCE_DIR}/modules/assets/src/asset_manager_impl.cpp"
    "${NAU_MODULES_SOURCE_DIR}/modules/assets/src/asset_path.cpp"
    "${NAU_MODULES_SOURCE_DIR}/modules/assets/src/asset_ref.cpp"
    "${NAU_MODULES_SOURCE_DIR}/modules/assets/src/assets_module.cpp"
    "${NAU_MODULES_SOURCE_DIR}/modules/assets/src/reloadable_asset_view.cpp"
)

set(NAU_SCENE_CoreScene_SOURCES
    "${NAU_MODULES_SOURCE_DIR}/modules/scene/src/camera/camera_manager_impl.cpp"
    "${NAU_MODULES_SOURCE_DIR}/modules/scene/src/camera/detached_camera.cpp"
    "${NAU_MODULES_SOURCE_DIR}/modules/scene/src/camera/internal_camera_properties.cpp"
    "${NAU_MODULES_SOURCE_DIR}/modules/scene/src/camera/readonly_camera.cpp"
    "${NAU_MODULES_SOURCE_DIR}/modules/scene/src/components/billboard_component.cpp"
    "${NAU_MODULES_SOURCE_DIR}/modules/scene/src/components/camera_component.cpp"
    "${NAU_MODULES_SOURCE_DIR}/modules/scene/src/components/component.cpp"
    "${NAU_MODULES_SOURCE_DIR}/modules/scene/src/components/directional_light_component.cpp"
    "${NAU_MODULES_SOURCE_DIR}/modules/scene/src/components/environment_component.cpp"
    "${NAU_MODULES_SOURCE_DIR}/modules/scene/src/components/missing_component.cpp"
    "${NAU_MODULES_SOURCE_DIR}/modules/scene/src/components/omnilight_component.cpp"
    "${NAU_MODULES_SOURCE_DIR}/modules/scene/src/components/scene_component.cpp"
    "${NAU_MODULES_SOURCE_DIR}/modules/scene/src/components/skinned_mesh_component.cpp"
    "${NAU_MODULES_SOURCE_DIR}/modules/scene/src/components/spotlight_component.cpp"
    "${NAU_MODULES_SOURCE_DIR}/modules/scene/src/components/static_mesh_component.cpp"
    "${NAU_MODULES_SOURCE_DIR}/modules/scene/src/nau_object.cpp"
    "${NAU_MODULES_SOURCE_DIR}/modules/scene/src/scene/scene_object.cpp"
    "${NAU_MODULES_SOURCE_DIR}/modules/scene/src/scene_impl.cpp"
    "${NAU_MODULES_SOURCE_DIR}/modules/scene/src/scene_management/scene_asset_wrapper.cpp"
    "${NAU_MODULES_SOURCE_DIR}/modules/scene/src/scene_management/scene_builder.cpp"
    "${NAU_MODULES_SOURCE_DIR}/modules/scene/src/scene_management/scene_factory_impl.cpp"
    "${NAU_MODULES_SOURCE_DIR}/modules/scene/src/scene_management/scene_manager_impl.cpp"
    "${NAU_MODULES_SOURCE_DIR}/modules/scene/src/scene_management/scene_manager_query.cpp"
    "${NAU_MODULES_SOURCE_DIR}/modules/scene/src/scene_management/scene_query.cpp"
    "${NAU_MODULES_SOURCE_DIR}/modules/scene/src/scene_module.cpp"
    "${NAU_MODULES_SOURCE_DIR}/modules/scene/src/scene_object.cpp"
    "${NAU_MODULES_SOURCE_DIR}/modules/scene/src/world_impl.cpp"
)

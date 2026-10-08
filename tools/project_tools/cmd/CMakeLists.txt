set(TargetName "ProjectToolCmd")

nau_collect_files(Sources
  ABSOLUTE ${CMAKE_CURRENT_SOURCE_DIR}/src
  DIRECTORIES ${CMAKE_CURRENT_SOURCE_DIR}/src
  MASK "*.cpp" "*.h"
)

add_executable(${TargetName})

target_sources(${TargetName}
  PRIVATE
  ${Sources}
)

set_target_properties(
  ${TargetName}
  PROPERTIES
  FOLDER "${NauEngineFolder}/tools"
)

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${Sources})

target_link_libraries(${TargetName} PRIVATE nlohmann_json::nlohmann_json argparse::argparse NauKernel ProjectTool Shared)

if(WIN32)
  set(CONFIG_PLUGIN_PATH "$<$<CONFIG:DEBUG>:${CMAKE_BINARY_DIR}/bin/Debug>$<$<NOT:$<CONFIG:DEBUG>>:${CMAKE_BINARY_DIR}/bin/Release>")

  set_target_properties(${TargetName}
    PROPERTIES VS_DEBUGGER_ENVIRONMENT
    "PATH=${CONFIG_PLUGIN_PATH}/plugins;${PXR_ENV_PATHS};${PXR_DEBUGER_ENV};
        ${PXR_DEBUGER_PLUGINPATH}${CONFIG_PLUGIN_PATH}/plugins;")
endif()

nau_add_compile_options(${TargetName})

install(TARGETS ${TargetName}
  DESTINATION bin/$<CONFIG>
)

# Keep deployed CLI templates current even when only template files change.
add_custom_target(NauProjectTemplates ALL
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${NAU_MODULES_SOURCE_DIR}/project_templates"
    "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/$<CONFIG>/project_templates"
  VERBATIM)
add_dependencies(ProjectToolCmd NauProjectTemplates)
install(DIRECTORY "${NAU_MODULES_SOURCE_DIR}/project_templates/"
  DESTINATION bin/$<CONFIG>/project_templates)

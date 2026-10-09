include_guard(GLOBAL)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(sceneShaderDirectory "${NAU_MODULES_SOURCE_DIR}/modules/scene_renderer/shaders")
set(sceneShaderOutput "${CMAKE_BINARY_DIR}/generated/scene_renderer")
set(sceneShaderHeader "${sceneShaderOutput}/nau_scene_shaders.h")
set(sceneShaderInputs "${sceneShaderDirectory}/scene.vert" "${sceneShaderDirectory}/scene.frag" "${sceneShaderDirectory}/embed.py")
set(sceneShaderArguments --output "${sceneShaderHeader}" --vertex "${sceneShaderDirectory}/scene.vert" --fragment "${sceneShaderDirectory}/scene.frag")
list(APPEND sceneShaderInputs "${sceneShaderDirectory}/billboard.vert" "${sceneShaderDirectory}/billboard.frag")
list(APPEND sceneShaderArguments --billboard-vertex "${sceneShaderDirectory}/billboard.vert" --billboard-fragment "${sceneShaderDirectory}/billboard.frag")
if(WIN32)
  get_filename_component(windowsKitsRoot "[HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows Kits\\Installed Roots;KitsRoot10]" ABSOLUTE)
  find_program(NAU_DXC_EXECUTABLE NAMES dxc HINTS "${windowsKitsRoot}/bin/${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}/x64")
  if(NOT NAU_DXC_EXECUTABLE OR NOT EXISTS "${NAU_DXC_EXECUTABLE}" OR IS_DIRECTORY "${NAU_DXC_EXECUTABLE}")
    message(FATAL_ERROR "Basic scene rendering requires native DXC. Install the Windows SDK shader tools or set NAU_DXC_EXECUTABLE.")
  endif()
  foreach(stage IN ITEMS vs ps)
    set(binary "${sceneShaderOutput}/${stage}.dxil")
    add_custom_command(OUTPUT "${binary}"
      COMMAND "${CMAKE_COMMAND}" -E make_directory "${sceneShaderOutput}"
      COMMAND "${NAU_DXC_EXECUTABLE}" -T "${stage}_6_0" -E "${stage}_main" -Fo "${binary}" "${sceneShaderDirectory}/scene.hlsl"
      DEPENDS "${sceneShaderDirectory}/scene.hlsl" "${NAU_DXC_EXECUTABLE}" VERBATIM)
    list(APPEND sceneShaderInputs "${binary}")
  endforeach()
  foreach(stage IN ITEMS vs ps)
    set(binary "${sceneShaderOutput}/billboard-${stage}.dxil")
    add_custom_command(OUTPUT "${binary}"
      COMMAND "${CMAKE_COMMAND}" -E make_directory "${sceneShaderOutput}"
      COMMAND "${NAU_DXC_EXECUTABLE}" -T "${stage}_6_0" -E "${stage}_main" -Fo "${binary}" "${sceneShaderDirectory}/billboard.hlsl"
      DEPENDS "${sceneShaderDirectory}/billboard.hlsl" "${NAU_DXC_EXECUTABLE}" VERBATIM)
    list(APPEND sceneShaderInputs "${binary}")
  endforeach()
  list(APPEND sceneShaderArguments --native-billboard-vertex "${sceneShaderOutput}/billboard-vs.dxil" --native-billboard-fragment "${sceneShaderOutput}/billboard-ps.dxil")
  list(APPEND sceneShaderArguments --native-vertex "${sceneShaderOutput}/vs.dxil" --native-fragment "${sceneShaderOutput}/ps.dxil")
endif()
add_custom_command(OUTPUT "${sceneShaderHeader}"
  COMMAND "${Python3_EXECUTABLE}" "${sceneShaderDirectory}/embed.py" ${sceneShaderArguments}
  DEPENDS ${sceneShaderInputs} VERBATIM)
add_custom_target(NauSceneShaders DEPENDS "${sceneShaderHeader}")

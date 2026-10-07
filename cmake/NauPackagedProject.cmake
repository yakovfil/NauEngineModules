include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/NauBrowserProject.cmake")
function(nau_packaged_project_delivery target)
  cmake_parse_arguments(delivery "" "DESTINATION" "" ${ARGN})
  if(NOT delivery_DESTINATION)
    set(delivery_DESTINATION "${CMAKE_BINARY_DIR}/package")
  endif()
  if(NOT EXISTS "${NAU_PACKAGED_PROJECT_ARCHIVE}")
    message(FATAL_ERROR "Missing NAU_PACKAGED_PROJECT_ARCHIVE")
  endif()
  if(EMSCRIPTEN)
    set(NAU_BROWSER_PROJECT_ARCHIVE "${NAU_PACKAGED_PROJECT_ARCHIVE}")
    nau_browser_project_delivery(${target} DESTINATION "${delivery_DESTINATION}")
  else()
    add_custom_target(${target}Package ALL
      COMMAND ${CMAKE_COMMAND} "-DBUILD_ROOT=${CMAKE_BINARY_DIR}"
        "-DEXECUTABLE=$<TARGET_FILE:${target}>" "-DARCHIVE=${NAU_PACKAGED_PROJECT_ARCHIVE}"
        "-DDESTINATION=${delivery_DESTINATION}"
        -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/packaged_project/stage_native.cmake"
      DEPENDS ${target} "${NAU_PACKAGED_PROJECT_ARCHIVE}" VERBATIM)
  endif()
endfunction()

include_guard(GLOBAL)

# Apply to an executable after its portable game module has been linked.
function(nau_browser_project_delivery target)
  cmake_parse_arguments(delivery "" "DESTINATION" "" ${ARGN})
  if(NOT delivery_DESTINATION)
    set(delivery_DESTINATION "${CMAKE_BINARY_DIR}/package")
  endif()
  if(NOT EMSCRIPTEN OR NOT NAU_BROWSER_PROJECT_PROFILE)
    message(FATAL_ERROR "nau_browser_project_delivery requires the browser project runtime profile.")
  endif()
  set(NAU_BROWSER_PROJECT_ARCHIVE "${NAU_BROWSER_PROJECT_ARCHIVE}" CACHE FILEPATH "Native-prepared startup archive")
  if(NOT EXISTS "${NAU_BROWSER_PROJECT_ARCHIVE}")
    message(FATAL_ERROR "Missing NAU_BROWSER_PROJECT_ARCHIVE. Prepare project assets with native host tools before configuring.")
  endif()
  set(NAU_BROWSER_PROJECT_INITIAL_MEMORY 134217728 CACHE STRING "Initial browser project memory in bytes")
  set(NAU_BROWSER_PROJECT_MAXIMUM_MEMORY 536870912 CACHE STRING "Maximum browser project memory in bytes")
  target_link_options(${target} PRIVATE -sEXIT_RUNTIME=1 -sPTHREAD_POOL_SIZE=12
    "-sINITIAL_MEMORY=${NAU_BROWSER_PROJECT_INITIAL_MEMORY}"
    "-sMAXIMUM_MEMORY=${NAU_BROWSER_PROJECT_MAXIMUM_MEMORY}"
    -sALLOW_MEMORY_GROWTH=1 -sALLOW_BLOCKING_ON_MAIN_THREAD=0)
  target_link_options(${target} PRIVATE
    "SHELL:--preload-file ${NAU_BROWSER_PROJECT_ARCHIVE}@/project-package/content.nau_pack")
  set_property(TARGET ${target} APPEND PROPERTY LINK_DEPENDS "${NAU_BROWSER_PROJECT_ARCHIVE}")
  set(NAU_BROWSER_APPLICATION "${target}")
  configure_file("${CMAKE_CURRENT_FUNCTION_LIST_DIR}/browser_project/index.html.in"
    "${CMAKE_CURRENT_BINARY_DIR}/web/${target}/index.html" @ONLY)
  add_custom_target(${target}WebPackage ALL
    COMMAND ${CMAKE_COMMAND} "-DBUILD_ROOT=${CMAKE_BINARY_DIR}"
      "-DLOADER=$<TARGET_FILE:${target}>"
      "-DAPPLICATION=${target}"
      "-DDESTINATION=${delivery_DESTINATION}"
      "-DINDEX=${CMAKE_CURRENT_BINARY_DIR}/web/${target}/index.html"
      "-DSERVER=${CMAKE_CURRENT_FUNCTION_LIST_DIR}/browser_project/serve.py"
      -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/browser_project/stage.cmake"
    DEPENDS ${target} VERBATIM)
endfunction()

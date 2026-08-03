cmake_minimum_required(VERSION 3.25)

project(
  GainPilot
  VERSION 0.3.0
  DESCRIPTION "LUFS auto-leveler built with the DISTRHO Plugin Framework"
  LANGUAGES C CXX
)

include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(UNIX)
  find_package(PkgConfig QUIET)
  if(PkgConfig_FOUND)
    pkg_check_modules(EBUR128 IMPORTED_TARGET libebur128)
  endif()
endif()

configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/include/gainpilot/version.hpp.in"
  "${CMAKE_CURRENT_BINARY_DIR}/generated/gainpilot/version.hpp"
  @ONLY
)

if(UNIX AND NOT APPLE)
  set(GAINPILOT_ENABLE_LV2_DEFAULT ON)
else()
  set(GAINPILOT_ENABLE_LV2_DEFAULT OFF)
endif()
if(APPLE)
  set(GAINPILOT_ENABLE_AU_DEFAULT ON)
else()
  set(GAINPILOT_ENABLE_AU_DEFAULT OFF)
endif()

option(GAINPILOT_ENABLE_VST3 "Build DPF VST3 plugin targets" ON)
option(GAINPILOT_ENABLE_LV2 "Build DPF LV2 plugin targets" ${GAINPILOT_ENABLE_LV2_DEFAULT})
option(GAINPILOT_ENABLE_CLAP "Build DPF CLAP plugin targets" ON)
option(GAINPILOT_ENABLE_AU "Build DPF Audio Unit plugin targets on macOS" ${GAINPILOT_ENABLE_AU_DEFAULT})
option(GAINPILOT_ENABLE_STANDALONE "Build DPF standalone plugin targets" OFF)
option(GAINPILOT_ENABLE_TESTS "Build local DSP and state smoke tests" ON)
set(
  GAINPILOT_DPF_PATH
  "${CMAKE_CURRENT_SOURCE_DIR}/dpf"
  CACHE PATH
  "Path to a DISTRHO Plugin Framework checkout"
)

add_library(
  gainpilot_core
  src/dsp/k_weighting_filter.cpp
  src/dsp/loudness_meter.cpp
  src/dsp/true_peak_limiter.cpp
  src/dsp/processor.cpp
)

target_include_directories(
  gainpilot_core
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_BINARY_DIR}/generated
)

target_compile_features(gainpilot_core PUBLIC cxx_std_20)
target_compile_options(
  gainpilot_core
  PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /permissive->
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -Wpedantic>
)

if(TARGET PkgConfig::EBUR128)
  target_link_libraries(gainpilot_core PUBLIC PkgConfig::EBUR128)
  target_compile_definitions(gainpilot_core PUBLIC GAINPILOT_USE_LIBEBUR128=1)
endif()

if(NOT EXISTS "${GAINPILOT_DPF_PATH}/CMakeLists.txt")
  message(
    FATAL_ERROR
    "DPF was not found at ${GAINPILOT_DPF_PATH}. "
    "Initialize submodules with: git submodule update --init --recursive"
  )
endif()

if(APPLE AND GAINPILOT_ENABLE_AU)
  find_package(Git REQUIRED)
  set(
    GAINPILOT_DPF_AU_PATCH
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/patches/dpf-au-host-buffer-routing.patch"
  )
  execute_process(
    COMMAND
      "${GIT_EXECUTABLE}" apply --unidiff-zero --check "${GAINPILOT_DPF_AU_PATCH}"
    WORKING_DIRECTORY "${GAINPILOT_DPF_PATH}"
    RESULT_VARIABLE GAINPILOT_DPF_PATCH_CAN_APPLY
    OUTPUT_QUIET
    ERROR_QUIET
  )
  if(GAINPILOT_DPF_PATCH_CAN_APPLY EQUAL 0)
    execute_process(
      COMMAND
        "${GIT_EXECUTABLE}" apply --unidiff-zero "${GAINPILOT_DPF_AU_PATCH}"
      WORKING_DIRECTORY "${GAINPILOT_DPF_PATH}"
      COMMAND_ERROR_IS_FATAL ANY
    )
  else()
    execute_process(
      COMMAND
        "${GIT_EXECUTABLE}" apply --unidiff-zero --reverse --check "${GAINPILOT_DPF_AU_PATCH}"
      WORKING_DIRECTORY "${GAINPILOT_DPF_PATH}"
      RESULT_VARIABLE GAINPILOT_DPF_PATCH_ALREADY_APPLIED
      OUTPUT_QUIET
      ERROR_QUIET
    )
    if(NOT GAINPILOT_DPF_PATCH_ALREADY_APPLIED EQUAL 0)
      message(
        FATAL_ERROR
        "The GainPilot DPF Audio Unit compatibility patch does not apply cleanly "
        "to ${GAINPILOT_DPF_PATH}."
      )
    endif()
  endif()
endif()

set(DPF_EXAMPLES OFF CACHE BOOL "" FORCE)
set(DPF_LIBRARIES OFF CACHE BOOL "" FORCE)
add_subdirectory("${GAINPILOT_DPF_PATH}" "${CMAKE_CURRENT_BINARY_DIR}/dpf" EXCLUDE_FROM_ALL)

set(gainpilot_dpf_targets)
if(GAINPILOT_ENABLE_VST3)
  list(APPEND gainpilot_dpf_targets vst3)
endif()
if(GAINPILOT_ENABLE_LV2)
  list(APPEND gainpilot_dpf_targets lv2)
endif()
if(GAINPILOT_ENABLE_CLAP)
  list(APPEND gainpilot_dpf_targets clap)
endif()
if(GAINPILOT_ENABLE_AU)
  if(NOT APPLE)
    message(FATAL_ERROR "GAINPILOT_ENABLE_AU is only supported on macOS.")
  endif()
  list(APPEND gainpilot_dpf_targets au)
endif()
if(GAINPILOT_ENABLE_STANDALONE)
  list(APPEND gainpilot_dpf_targets jack)
endif()

if(NOT gainpilot_dpf_targets)
  message(FATAL_ERROR "At least one GainPilot DPF plugin format must be enabled.")
endif()

function(gainpilot_add_dpf_plugin target info_directory bundle_identifier display_name)
  dpf_add_plugin(
    ${target}
    TARGETS ${gainpilot_dpf_targets}
    FILES_DSP src/dpf/GainPilotPlugin.cpp
    FILES_UI src/dpf/GainPilotUI.cpp
  )

  target_include_directories(
    ${target}
    PUBLIC
      ${CMAKE_CURRENT_SOURCE_DIR}/${info_directory}
      ${CMAKE_CURRENT_SOURCE_DIR}/include
      ${CMAKE_CURRENT_BINARY_DIR}/generated
  )
  target_link_libraries(${target} PUBLIC gainpilot_core)
  target_compile_features(${target} PUBLIC cxx_std_20)
  target_compile_options(
    ${target}-dsp
    PRIVATE
      $<$<CXX_COMPILER_ID:MSVC>:/W4 /permissive->
      $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -Wpedantic>
  )
  target_compile_options(
    ${target}-ui
    PRIVATE
      $<$<CXX_COMPILER_ID:MSVC>:/W4 /permissive->
      $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra>
  )

  if(APPLE)
    set(GAINPILOT_BUNDLE_EXECUTABLE "${target}")
    set(GAINPILOT_BUNDLE_IDENTIFIER "${bundle_identifier}")
    set(GAINPILOT_BUNDLE_DISPLAY_NAME "${display_name}")
    foreach(bundle_extension vst3 clap)
      if(
        (bundle_extension STREQUAL "vst3" AND GAINPILOT_ENABLE_VST3)
        OR (bundle_extension STREQUAL "clap" AND GAINPILOT_ENABLE_CLAP)
      )
        configure_file(
          "${CMAKE_CURRENT_SOURCE_DIR}/cmake/GainPilotPluginInfo.plist.in"
          "${CMAKE_CURRENT_BINARY_DIR}/bin/${target}.${bundle_extension}/Contents/Info.plist"
          @ONLY
        )
      endif()
    endforeach()

    foreach(bundle_extension vst3 clap component)
      if(
        (bundle_extension STREQUAL "vst3" AND GAINPILOT_ENABLE_VST3)
        OR (bundle_extension STREQUAL "clap" AND GAINPILOT_ENABLE_CLAP)
        OR (bundle_extension STREQUAL "component" AND GAINPILOT_ENABLE_AU)
      )
        if(bundle_extension STREQUAL "component")
          set(bundle_target "${target}-au")
        else()
          set(bundle_target "${target}-${bundle_extension}")
        endif()
        add_custom_command(
          TARGET ${bundle_target}
          POST_BUILD
          COMMAND
            /usr/bin/codesign
            --force
            --deep
            --sign -
            "${CMAKE_CURRENT_BINARY_DIR}/bin/${target}.${bundle_extension}"
          COMMENT "Ad-hoc signing ${target}.${bundle_extension} for local macOS loading"
          VERBATIM
        )
      endif()
    endforeach()
  endif()
endfunction()

gainpilot_add_dpf_plugin(
  GainPilot
  src/dpf/stereo
  dev.gainpilot.gainpilot.stereo
  GainPilot
)
gainpilot_add_dpf_plugin(
  GainPilotMono
  src/dpf/mono
  dev.gainpilot.gainpilot.mono
  "GainPilot Mono"
)

set(GAINPILOT_PLUGIN_NAMES GainPilot GainPilotMono)

if(GAINPILOT_ENABLE_LV2)
  foreach(plugin_name IN LISTS GAINPILOT_PLUGIN_NAMES)
    install(
      DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/bin/${plugin_name}.lv2"
      DESTINATION "${CMAKE_INSTALL_LIBDIR}/lv2"
      USE_SOURCE_PERMISSIONS
    )
  endforeach()
endif()

if(GAINPILOT_ENABLE_VST3)
  if(WIN32)
    set(GAINPILOT_VST3_INSTALL_DESTINATION "VST3")
  elseif(APPLE)
    set(GAINPILOT_VST3_INSTALL_DESTINATION "Library/Audio/Plug-Ins/VST3")
  else()
    set(GAINPILOT_VST3_INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/vst3")
  endif()

  foreach(plugin_name IN LISTS GAINPILOT_PLUGIN_NAMES)
    install(
      DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/bin/${plugin_name}.vst3"
      DESTINATION "${GAINPILOT_VST3_INSTALL_DESTINATION}"
      USE_SOURCE_PERMISSIONS
    )
  endforeach()
endif()

if(GAINPILOT_ENABLE_CLAP)
  if(WIN32)
    set(GAINPILOT_CLAP_INSTALL_DESTINATION "CLAP")
  elseif(APPLE)
    set(GAINPILOT_CLAP_INSTALL_DESTINATION "Library/Audio/Plug-Ins/CLAP")
  else()
    set(GAINPILOT_CLAP_INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/clap")
  endif()

  foreach(plugin_name IN LISTS GAINPILOT_PLUGIN_NAMES)
    if(APPLE)
      install(
        DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/bin/${plugin_name}.clap"
        DESTINATION "${GAINPILOT_CLAP_INSTALL_DESTINATION}"
        USE_SOURCE_PERMISSIONS
      )
    else()
      install(
        FILES "${CMAKE_CURRENT_BINARY_DIR}/bin/${plugin_name}.clap"
        DESTINATION "${GAINPILOT_CLAP_INSTALL_DESTINATION}"
      )
    endif()
  endforeach()
endif()

if(GAINPILOT_ENABLE_AU)
  foreach(plugin_name IN LISTS GAINPILOT_PLUGIN_NAMES)
    install(
      DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/bin/${plugin_name}.component"
      DESTINATION "Library/Audio/Plug-Ins/Components"
      USE_SOURCE_PERMISSIONS
    )
  endforeach()
endif()

if(GAINPILOT_ENABLE_TESTS)
  enable_testing()
  add_executable(gainpilot_smoke tests/smoke.cpp)
  target_link_libraries(gainpilot_smoke PRIVATE gainpilot_core)
  add_test(NAME gainpilot_smoke COMMAND gainpilot_smoke)
endif()

set(CPACK_PACKAGE_NAME "GainPilot")
set(CPACK_PACKAGE_VENDOR "GainPilot contributors")
set(CPACK_PACKAGE_CONTACT "opensource@gainpilot.dev")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "DPF-based LUFS auto-leveler for VST3, Audio Unit, LV2, and CLAP")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE.txt")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/packages")
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY OFF)

if(WIN32)
  set(CPACK_GENERATOR "ZIP;NSIS;WIX")
  set(CPACK_PACKAGE_FILE_NAME "gainpilot-${PROJECT_VERSION}-windows-x64")
  set(CPACK_PACKAGE_INSTALL_DIRECTORY "GainPilot")
  set(CPACK_NSIS_DISPLAY_NAME "GainPilot")
  set(CPACK_NSIS_PACKAGE_NAME "GainPilot")
  set(CPACK_NSIS_INSTALL_ROOT "$COMMONFILES64")
  set(CPACK_NSIS_CONTACT "${CPACK_PACKAGE_CONTACT}")
  set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
  set(CPACK_WIX_ROOT_FOLDER_ID "CommonFiles64Folder")
  set(CPACK_WIX_UPGRADE_GUID "B4D7C8D0-7F0B-4B99-8D4A-59B6C42A8D41")
elseif(APPLE)
  set(CPACK_GENERATOR "ZIP;TGZ")
  set(CPACK_PACKAGE_FILE_NAME "gainpilot-${PROJECT_VERSION}-macos")
else()
  set(CPACK_GENERATOR "ZIP;TGZ")
  set(CPACK_PACKAGE_FILE_NAME "gainpilot-${PROJECT_VERSION}-linux-x64")
endif()

include(CPack)

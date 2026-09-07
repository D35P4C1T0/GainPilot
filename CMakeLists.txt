cmake_minimum_required(VERSION 3.25)

project(
  GainPilot
  VERSION 0.5.0
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
option(GAINPILOT_ENABLE_TESTS "Build DSP, state, and real-time regression tests" ON)
option(GAINPILOT_ENABLE_PLUGINS "Build plugin bundles and their UI" ON)
option(GAINPILOT_REFERENCE_TESTS "Validate against libebur128 when available" ON)
set(GAINPILOT_PLUGINVAL "" CACHE FILEPATH "Optional pluginval executable for VST3 host tests")
set(GAINPILOT_CLAP_VALIDATOR "" CACHE FILEPATH "Optional clap-validator executable for CLAP host tests")
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

if(GAINPILOT_ENABLE_PLUGINS)

if(NOT EXISTS "${GAINPILOT_DPF_PATH}/CMakeLists.txt")
  message(
    FATAL_ERROR
    "DPF was not found at ${GAINPILOT_DPF_PATH}. "
    "Initialize submodules with: git submodule update --init --recursive"
  )
endif()

if(GAINPILOT_ENABLE_CLAP OR (APPLE AND GAINPILOT_ENABLE_AU))
  find_package(Git REQUIRED)
  include(cmake/ApplyDPFPatch.cmake)
endif()
if(APPLE AND GAINPILOT_ENABLE_AU)
  gainpilot_apply_dpf_patch(dpf-au-host-buffer-routing.patch)
endif()
if(GAINPILOT_ENABLE_CLAP)
  gainpilot_apply_dpf_patch(dpf-clap-host-compatibility.patch)
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
    USE_FILE_BROWSER
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

endif() # GAINPILOT_ENABLE_PLUGINS

if(GAINPILOT_ENABLE_TESTS)
  enable_testing()
  foreach(test_name smoke learning realtime peak presets)
    add_executable(gainpilot_${test_name} tests/${test_name}.cpp)
    target_link_libraries(gainpilot_${test_name} PRIVATE gainpilot_core)
    add_test(NAME gainpilot_${test_name} COMMAND gainpilot_${test_name})
  endforeach()
  if(GAINPILOT_ENABLE_PLUGINS AND GAINPILOT_ENABLE_VST3 AND GAINPILOT_PLUGINVAL)
    foreach(plugin_name GainPilot GainPilotMono)
      add_test(NAME ${plugin_name}_pluginval COMMAND "${GAINPILOT_PLUGINVAL}"
        --strictness-level 10 --random-seed 12345 --skip-gui-tests
        --validate "${CMAKE_CURRENT_BINARY_DIR}/bin/${plugin_name}.vst3")
    endforeach()
  endif()
  if(GAINPILOT_ENABLE_PLUGINS AND GAINPILOT_ENABLE_CLAP)
    add_executable(gainpilot_clap_host tests/clap_host.cpp)
    target_include_directories(gainpilot_clap_host PRIVATE include "${GAINPILOT_DPF_PATH}/distrho/src")
    target_link_libraries(gainpilot_clap_host PRIVATE ${CMAKE_DL_LIBS})
    add_dependencies(gainpilot_clap_host GainPilot-clap GainPilotMono-clap)
    foreach(plugin_name GainPilot GainPilotMono)
      if(plugin_name STREQUAL "GainPilotMono")
        set(clap_channels 1)
      else()
        set(clap_channels 2)
      endif()
      add_test(NAME ${plugin_name}_clap_host COMMAND gainpilot_clap_host
        "${CMAKE_CURRENT_BINARY_DIR}/bin/${plugin_name}.clap" ${clap_channels})
    endforeach()
  endif()
  if(GAINPILOT_ENABLE_PLUGINS AND GAINPILOT_ENABLE_CLAP AND GAINPILOT_CLAP_VALIDATOR)
    foreach(plugin_name GainPilot GainPilotMono)
      add_test(NAME ${plugin_name}_clap_validator COMMAND "${GAINPILOT_CLAP_VALIDATOR}"
        validate --jobs 2 "${CMAKE_CURRENT_BINARY_DIR}/bin/${plugin_name}.clap")
    endforeach()
  endif()
  if(APPLE AND GAINPILOT_ENABLE_PLUGINS AND GAINPILOT_ENABLE_AU)
    add_executable(gainpilot_au_host tests/au_host.cpp)
    add_dependencies(gainpilot_au_host GainPilot-au GainPilotMono-au)
    target_include_directories(gainpilot_au_host PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_link_libraries(gainpilot_au_host PRIVATE "-framework AudioToolbox" "-framework CoreFoundation")
    add_test(NAME gainpilot_au_stereo COMMAND gainpilot_au_host "${CMAKE_CURRENT_BINARY_DIR}/bin/GainPilot.component" 2)
    add_test(NAME gainpilot_au_mono COMMAND gainpilot_au_host "${CMAKE_CURRENT_BINARY_DIR}/bin/GainPilotMono.component" 1)
  endif()
  if(GAINPILOT_REFERENCE_TESTS AND TARGET PkgConfig::EBUR128)
    add_executable(gainpilot_reference tests/reference.cpp)
    target_link_libraries(gainpilot_reference PRIVATE gainpilot_core PkgConfig::EBUR128)
    add_test(NAME gainpilot_reference COMMAND gainpilot_reference)
    add_executable(gainpilot_render_target tests/render_target.cpp)
    target_link_libraries(gainpilot_render_target PRIVATE gainpilot_core PkgConfig::EBUR128)
    foreach(pattern RANGE 0 2)
      add_test(NAME gainpilot_render_target_${pattern} COMMAND gainpilot_render_target 120 ${pattern} 0)
    endforeach()
    add_test(NAME gainpilot_render_target_speech COMMAND gainpilot_render_target 120 2 1)
  elseif(GAINPILOT_REFERENCE_TESTS)
    message(STATUS "libebur128 unavailable: independent reference test skipped")
  endif()
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

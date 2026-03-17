cmake_minimum_required(VERSION 3.25)

project(
  GainPilot
  VERSION 0.1.1
  DESCRIPTION "LUFS auto-leveler with native LV2 and VST3 targets"
  LANGUAGES CXX
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
  set(GAINPILOT_ENABLE_LV2_UI_DEFAULT ON)
  set(GAINPILOT_ENABLE_GTK_UI_DEFAULT ON)
else()
  set(GAINPILOT_ENABLE_LV2_DEFAULT OFF)
  set(GAINPILOT_ENABLE_LV2_UI_DEFAULT OFF)
  set(GAINPILOT_ENABLE_GTK_UI_DEFAULT OFF)
endif()

option(GAINPILOT_ENABLE_LV2 "Build LV2 plugin targets" ${GAINPILOT_ENABLE_LV2_DEFAULT})
option(GAINPILOT_ENABLE_LV2_UI "Build LV2 GTK UI targets" ${GAINPILOT_ENABLE_LV2_UI_DEFAULT})
option(GAINPILOT_ENABLE_TESTS "Build local smoke tests" ON)
option(GAINPILOT_ENABLE_WX_UI "Build shared wxWidgets UI module" ON)
option(GAINPILOT_ENABLE_GTK_UI "Build shared GTK UI module" ${GAINPILOT_ENABLE_GTK_UI_DEFAULT})

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

if(GAINPILOT_ENABLE_WX_UI)
  find_package(wxWidgets QUIET COMPONENTS core base)
  if(wxWidgets_FOUND)
    include(${wxWidgets_USE_FILE})

    add_library(
      gainpilot_ui
      src/ui/wx_runtime.cpp
      src/ui/editor_panel.cpp
    )

    target_include_directories(
      gainpilot_ui
      PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_BINARY_DIR}/generated
    )

    target_link_libraries(gainpilot_ui PUBLIC gainpilot_core ${wxWidgets_LIBRARIES})
    target_compile_features(gainpilot_ui PUBLIC cxx_std_20)
    target_compile_options(
      gainpilot_ui
      PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:/W4 /permissive->
        $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -Wpedantic>
    )
  else()
    message(STATUS "wxWidgets not found. Shared wx UI module is disabled.")
  endif()
endif()

if(GAINPILOT_ENABLE_GTK_UI)
  if(NOT PkgConfig_FOUND)
    find_package(PkgConfig REQUIRED)
  endif()
  pkg_check_modules(GTK3 REQUIRED IMPORTED_TARGET gtk+-3.0)

  add_library(
    gainpilot_gtk_ui
    src/ui/gtk_editor.cpp
  )

  target_include_directories(
    gainpilot_gtk_ui
    PUBLIC
      ${CMAKE_CURRENT_SOURCE_DIR}/include
      ${CMAKE_CURRENT_BINARY_DIR}/generated
  )

  target_link_libraries(gainpilot_gtk_ui PUBLIC gainpilot_core PkgConfig::GTK3)
  target_compile_features(gainpilot_gtk_ui PUBLIC cxx_std_20)
  target_compile_options(
    gainpilot_gtk_ui
    PRIVATE
      $<$<CXX_COMPILER_ID:MSVC>:/W4 /permissive->
      $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -Wpedantic>
  )
endif()

if(GAINPILOT_ENABLE_LV2)
  if(NOT PkgConfig_FOUND)
    find_package(PkgConfig REQUIRED)
  endif()
  pkg_check_modules(LV2 REQUIRED IMPORTED_TARGET lv2)
  if(GAINPILOT_ENABLE_LV2_UI)
    if(NOT TARGET gainpilot_gtk_ui)
      message(FATAL_ERROR "GAINPILOT_ENABLE_LV2_UI requires GTK UI support.")
    endif()
  endif()

  set(GAINPILOT_LV2_BUNDLE_ROOT "${CMAKE_BINARY_DIR}/lv2")

  function(gainpilot_add_lv2_target target_name plugin_name channel_count plugin_uri ttl_template)
    set(bundle_dir "${GAINPILOT_LV2_BUNDLE_ROOT}/${plugin_name}.lv2")
    file(MAKE_DIRECTORY "${bundle_dir}")

    add_library(${target_name} MODULE src/lv2/plugin.cpp)
    target_link_libraries(${target_name} PRIVATE gainpilot_core PkgConfig::LV2)
    target_compile_definitions(
      ${target_name}
      PRIVATE
        GAINPILOT_LV2_URI="${plugin_uri}"
        GAINPILOT_LV2_CHANNELS=${channel_count}
    )
    set_target_properties(
      ${target_name}
      PROPERTIES
        PREFIX ""
        OUTPUT_NAME "${plugin_name}"
        LIBRARY_OUTPUT_DIRECTORY "${bundle_dir}"
    )

    set(plugin_name "${plugin_name}")
    set(plugin_uri "${plugin_uri}")
    set(ui_uri "${plugin_uri}-ui")
    set(ui_binary_name "${plugin_name}_ui")
    if(channel_count EQUAL 1)
      set(CHANNEL_PORTS [=[
    [
      a lv2:OutputPort, lv2:AudioPort ;
      lv2:index 1 ;
      lv2:symbol "out_l" ;
      lv2:name "Output"
    ] ,
]=])
      set(CONTROL_BASE 2)
    else()
      set(CHANNEL_PORTS [=[
    [
      a lv2:InputPort, lv2:AudioPort ;
      lv2:index 1 ;
      lv2:symbol "in_r" ;
      lv2:name "Input R"
    ] ,
    [
      a lv2:OutputPort, lv2:AudioPort ;
      lv2:index 2 ;
      lv2:symbol "out_l" ;
      lv2:name "Output L"
    ] ,
    [
      a lv2:OutputPort, lv2:AudioPort ;
      lv2:index 3 ;
      lv2:symbol "out_r" ;
      lv2:name "Output R"
    ] ,
]=])
      set(CONTROL_BASE 4)
    endif()
    math(EXPR CONTROL_BASE_PLUS_1 "${CONTROL_BASE} + 1")
    math(EXPR CONTROL_BASE_PLUS_2 "${CONTROL_BASE} + 2")
    math(EXPR CONTROL_BASE_PLUS_3 "${CONTROL_BASE} + 3")
    math(EXPR CONTROL_BASE_PLUS_4 "${CONTROL_BASE} + 4")
    math(EXPR CONTROL_BASE_PLUS_5 "${CONTROL_BASE} + 5")
    math(EXPR CONTROL_BASE_PLUS_6 "${CONTROL_BASE} + 6")
    math(EXPR CONTROL_BASE_PLUS_7 "${CONTROL_BASE} + 7")
    math(EXPR CONTROL_BASE_PLUS_8 "${CONTROL_BASE} + 8")
    math(EXPR CONTROL_BASE_PLUS_9 "${CONTROL_BASE} + 9")
    math(EXPR CONTROL_BASE_PLUS_10 "${CONTROL_BASE} + 10")
    math(EXPR CONTROL_BASE_PLUS_11 "${CONTROL_BASE} + 11")

    if(GAINPILOT_ENABLE_LV2_UI)
      add_library(${target_name}_ui MODULE src/lv2/ui.cpp)
      target_link_libraries(${target_name}_ui PRIVATE gainpilot_gtk_ui PkgConfig::LV2)
      target_include_directories(
        ${target_name}_ui
        PRIVATE
          ${CMAKE_CURRENT_SOURCE_DIR}/include
          ${CMAKE_CURRENT_BINARY_DIR}/generated
      )
      target_compile_definitions(
        ${target_name}_ui
        PRIVATE
          GAINPILOT_LV2_URI="${plugin_uri}"
          GAINPILOT_LV2_UI_URI="${ui_uri}"
          GAINPILOT_LV2_CHANNELS=${channel_count}
      )
      set_target_properties(
        ${target_name}_ui
        PROPERTIES
          PREFIX ""
          OUTPUT_NAME "${ui_binary_name}"
          LIBRARY_OUTPUT_DIRECTORY "${bundle_dir}"
      )
      set(PLUGIN_UI_REFERENCE "  ui:ui <${ui_uri}> ;")
      configure_file("${CMAKE_CURRENT_SOURCE_DIR}/lv2/ui.ttl.in" "${bundle_dir}/${plugin_name}_ui.ttl" @ONLY)
      set(UI_MANIFEST_ENTRY_TEMPLATE [=[
<@ui_uri@>
  a <http://lv2plug.in/ns/extensions/ui#Gtk3UI> ;
  <http://lv2plug.in/ns/extensions/ui#binary> <@ui_binary_name@.so> ;
  rdfs:seeAlso <@plugin_name@_ui.ttl> .
]=])
      string(CONFIGURE "${UI_MANIFEST_ENTRY_TEMPLATE}" UI_MANIFEST_ENTRY @ONLY)
    else()
      set(PLUGIN_UI_REFERENCE "")
      set(UI_MANIFEST_ENTRY "")
    endif()

    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/lv2/manifest.ttl.in" "${bundle_dir}/manifest.ttl" @ONLY)
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/lv2/${ttl_template}" "${bundle_dir}/${plugin_name}.ttl" @ONLY)

    install(DIRECTORY "${bundle_dir}/" DESTINATION "${CMAKE_INSTALL_LIBDIR}/lv2/${plugin_name}.lv2")
  endfunction()

  gainpilot_add_lv2_target(
    gainpilot_lv2_mono
    gainpilot_mono
    1
    "https://gainpilot.dev/plugins/gainpilot-mono"
    plugin.ttl.in
  )

  gainpilot_add_lv2_target(
    gainpilot_lv2_stereo
    gainpilot_stereo
    2
    "https://gainpilot.dev/plugins/gainpilot-stereo"
    plugin.ttl.in
  )
endif()

set(GAINPILOT_VST3_SDK_PATH "${CMAKE_CURRENT_SOURCE_DIR}/external/vst3sdk" CACHE PATH "Path to a VST3 SDK checkout")

if(EXISTS "${GAINPILOT_VST3_SDK_PATH}/CMakeLists.txt")
  set(SMTG_ENABLE_VST3_PLUGIN_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(SMTG_ENABLE_VST3_HOSTING_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(SMTG_ENABLE_VSTGUI_SUPPORT OFF CACHE BOOL "" FORCE)
  set(SMTG_CREATE_PLUGIN_LINK OFF CACHE BOOL "" FORCE)
  set(SMTG_RUN_VST_VALIDATOR OFF CACHE BOOL "" FORCE)
  add_subdirectory("${GAINPILOT_VST3_SDK_PATH}" "${CMAKE_BINARY_DIR}/vst3sdk" EXCLUDE_FROM_ALL)

  set(gainpilot_vst3_sources
    ${GAINPILOT_VST3_SDK_PATH}/public.sdk/source/main/moduleinit.cpp
    ${GAINPILOT_VST3_SDK_PATH}/public.sdk/source/vst/vstsinglecomponenteffect.cpp
    src/vst3/common.cpp
    src/vst3/controller.cpp
    src/vst3/plugin.cpp
    src/vst3/entry.cpp
  )
  if(WIN32 AND TARGET gainpilot_ui)
    list(APPEND gainpilot_vst3_sources src/vst3/wx_view.cpp)
  endif()
  if(APPLE)
    list(APPEND gainpilot_vst3_sources src/vst3/mac_view.mm)
  endif()
  if(WIN32)
    list(APPEND gainpilot_vst3_sources ${GAINPILOT_VST3_SDK_PATH}/public.sdk/source/main/dllmain.cpp)
  elseif(APPLE)
    list(APPEND gainpilot_vst3_sources ${GAINPILOT_VST3_SDK_PATH}/public.sdk/source/main/macmain.cpp)
  elseif(UNIX AND NOT APPLE)
    list(APPEND gainpilot_vst3_sources ${GAINPILOT_VST3_SDK_PATH}/public.sdk/source/main/linuxmain.cpp)
  endif()

  smtg_add_vst3plugin(gainpilot_vst3 PACKAGE_NAME "GainPilot" SOURCES_LIST ${gainpilot_vst3_sources})
  target_link_libraries(gainpilot_vst3 PRIVATE gainpilot_core sdk)
  if(WIN32 AND TARGET gainpilot_ui)
    target_link_libraries(gainpilot_vst3 PRIVATE gainpilot_ui)
    target_compile_definitions(gainpilot_vst3 PRIVATE GAINPILOT_VST3_USE_WX_VIEW=1)
  endif()
  if(APPLE)
    find_library(COCOA_FRAMEWORK Cocoa REQUIRED)
    target_link_libraries(gainpilot_vst3 PRIVATE ${COCOA_FRAMEWORK})
  endif()
  target_include_directories(
    gainpilot_vst3
    PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/include
      ${CMAKE_CURRENT_BINARY_DIR}/generated
      ${CMAKE_CURRENT_SOURCE_DIR}/src
  )
  target_compile_features(gainpilot_vst3 PRIVATE cxx_std_20)
  if(WIN32)
    target_compile_definitions(gainpilot_vst3 PRIVATE NOMINMAX)
  endif()
  add_custom_command(
    TARGET gainpilot_vst3
    POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E rm -f "$<TARGET_FILE_DIR:gainpilot_vst3>/../Resources/gainpilot.uidesc"
  )

  if(WIN32)
    set(GAINPILOT_VST3_INSTALL_DESTINATION ".")
  elseif(APPLE)
    set(GAINPILOT_VST3_INSTALL_DESTINATION "Library/Audio/Plug-Ins/VST3")
  else()
    set(GAINPILOT_VST3_INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/vst3")
  endif()

  install(
    DIRECTORY "${CMAKE_BINARY_DIR}/VST3/$<CONFIG>/GainPilot.vst3"
    DESTINATION "${GAINPILOT_VST3_INSTALL_DESTINATION}"
    USE_SOURCE_PERMISSIONS
  )
else()
  message(STATUS "VST3 SDK not found. VST3 targets are disabled for now.")
endif()

if(GAINPILOT_ENABLE_TESTS)
  enable_testing()
  add_executable(gainpilot_smoke tests/smoke.cpp)
  target_link_libraries(gainpilot_smoke PRIVATE gainpilot_core)
  add_test(NAME gainpilot_smoke COMMAND gainpilot_smoke)

  if(GAINPILOT_ENABLE_LV2)
    add_executable(gainpilot_lv2_state_smoke tests/lv2_state_smoke.cpp src/lv2/plugin.cpp)
    target_link_libraries(gainpilot_lv2_state_smoke PRIVATE gainpilot_core PkgConfig::LV2)
    target_compile_definitions(
      gainpilot_lv2_state_smoke
      PRIVATE
        GAINPILOT_LV2_URI="https://gainpilot.dev/plugins/gainpilot-stereo"
        GAINPILOT_LV2_CHANNELS=2
    )
    add_test(NAME gainpilot_lv2_state_smoke COMMAND gainpilot_lv2_state_smoke)
  endif()
endif()

set(CPACK_PACKAGE_NAME "GainPilot")
set(CPACK_PACKAGE_VENDOR "GainPilot contributors")
set(CPACK_PACKAGE_CONTACT "opensource@gainpilot.dev")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "LUFS auto-leveler with native VST3 and LV2 targets")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE.txt")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DIRECTORY "${CMAKE_BINARY_DIR}/packages")
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY OFF)

if(WIN32)
  set(CPACK_GENERATOR "ZIP;NSIS;WIX")
  set(CPACK_PACKAGE_FILE_NAME "gainpilot-${PROJECT_VERSION}-windows-x64")
  set(CPACK_PACKAGE_INSTALL_DIRECTORY "VST3")
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

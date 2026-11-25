
include_directories(${CMAKE_CURRENT_BINARY_DIR}
    ${CMAKE_SOURCE_DIR}/DPF/distrho .
    ${CMAKE_SOURCE_DIR}/DPF/dgl
    ${CMAKE_SOURCE_DIR}/DPF/dgl/src)

# The plugin has no user interface: the graphical ones were removed.
add_library(ZynAddSubFX_lv2 SHARED
    ${CMAKE_SOURCE_DIR}/src/globals.cpp
    ${CMAKE_SOURCE_DIR}/src/UI/ConnectionDummy.cpp
    ${CMAKE_SOURCE_DIR}/DPF/distrho/DistrhoPluginMain.cpp
    ZynAddSubFX.cpp)

add_library(ZynAddSubFX_vst SHARED
    ${CMAKE_SOURCE_DIR}/src/globals.cpp
    ${CMAKE_SOURCE_DIR}/src/UI/ConnectionDummy.cpp
    ${CMAKE_SOURCE_DIR}/DPF/distrho/DistrhoPluginMain.cpp
    ZynAddSubFX.cpp)

if(NOT ${CMAKE_SYSTEM_NAME} STREQUAL "Windows")
    set_target_properties(ZynAddSubFX_lv2 PROPERTIES COMPILE_DEFINITIONS "DISTRHO_PLUGIN_TARGET_LV2")
    set_target_properties(ZynAddSubFX_lv2 PROPERTIES LIBRARY_OUTPUT_DIRECTORY "lv2")
    set_target_properties(ZynAddSubFX_lv2 PROPERTIES RUNTIME_OUTPUT_DIRECTORY "lv2")
    set_target_properties(ZynAddSubFX_lv2 PROPERTIES OUTPUT_NAME "ZynAddSubFX")
    set_target_properties(ZynAddSubFX_lv2 PROPERTIES PREFIX "")
endif()

set_target_properties(ZynAddSubFX_vst PROPERTIES COMPILE_DEFINITIONS "DISTRHO_PLUGIN_TARGET_VST2")
set_target_properties(ZynAddSubFX_vst PROPERTIES LIBRARY_OUTPUT_DIRECTORY "vst")
set_target_properties(ZynAddSubFX_vst PROPERTIES RUNTIME_OUTPUT_DIRECTORY "vst")
set_target_properties(ZynAddSubFX_vst PROPERTIES OUTPUT_NAME "ZynAddSubFX")
set_target_properties(ZynAddSubFX_vst PROPERTIES PREFIX "")

if(${CMAKE_SYSTEM_NAME} STREQUAL "Windows")
    set(PLATFORM_LIBRARIES ws2_32 winmm iphlpapi)
elseif(HAVE_LIBRT)
    set(PLATFORM_LIBRARIES rt)
endif()

if(NOT ${CMAKE_SYSTEM_NAME} STREQUAL "Windows")
    target_link_libraries(ZynAddSubFX_lv2 zynaddsubfx_core ${OS_LIBRARIES} ${LIBLO_LIBRARIES}
        ${PLATFORM_LIBRARIES})
endif()
if(APPLE)
    target_link_libraries(ZynAddSubFX_vst zynaddsubfx_core ${OS_LIBRARIES} ${LIBLO_LIBRARIES}
        ${PLATFORM_LIBRARIES} "-Wl,-exported_symbol,_VSTPluginMain")
else()
    target_link_libraries(ZynAddSubFX_vst zynaddsubfx_core ${OS_LIBRARIES} ${LIBLO_LIBRARIES}
        ${PLATFORM_LIBRARIES})
endif()

if(NOT ${CMAKE_SYSTEM_NAME} STREQUAL "Windows")
    install(TARGETS ZynAddSubFX_lv2 LIBRARY DESTINATION ${PluginLibDir}/lv2/ZynAddSubFX.lv2/)
    install(TARGETS ZynAddSubFX_vst LIBRARY DESTINATION ${PluginLibDir}/vst/)

    add_custom_command(TARGET ZynAddSubFX_lv2 POST_BUILD
        COMMAND ../../lv2-ttl-generator $<TARGET_FILE:ZynAddSubFX_lv2>
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/lv2)

    add_dependencies(ZynAddSubFX_lv2 lv2-ttl-generator)
endif()

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/lv2/manifest.ttl
    ${CMAKE_CURRENT_BINARY_DIR}/lv2/presets.ttl
    ${CMAKE_CURRENT_BINARY_DIR}/lv2/ZynAddSubFX.ttl
    DESTINATION ${PluginLibDir}/lv2/ZynAddSubFX.lv2/)

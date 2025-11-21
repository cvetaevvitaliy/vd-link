add_library(vdlink_sample_addon MODULE sample_addon.c)

set_target_properties(vdlink_sample_addon
        PROPERTIES
        PREFIX ""
        OUTPUT_NAME "sample_addon")

target_include_directories(vdlink_sample_addon
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../..)

install(TARGETS vdlink_sample_addon
        LIBRARY DESTINATION ${VDLINK_PLUGIN_INSTALL_SUBDIR}
        RUNTIME DESTINATION ${VDLINK_PLUGIN_INSTALL_SUBDIR})

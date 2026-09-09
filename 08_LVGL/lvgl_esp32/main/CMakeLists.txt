file(GLOB_RECURSE LVGL_EXAMPLES "${CMAKE_CURRENT_LIST_DIR}/../components/lvgl/examples/*.c")
# file(GLOB_RECURSE LVGL_DEMO_WIDGETS "${CMAKE_CURRENT_LIST_DIR}/../components/lvgl/demos/widgets/*.c")
file(GLOB_RECURSE LVGL_DEMO_MUSIC "${CMAKE_CURRENT_LIST_DIR}/../components/lvgl/demos/music/*.c")


idf_component_register(SRCS "main.c" "st7796.c" "ft6336.c"
                       ${LVGL_EXAMPLES}
                    #    ${LVGL_DEMO_WIDGETS}
                       ${LVGL_DEMO_MUSIC}
                       INCLUDE_DIRS "."
                       REQUIRES lvgl driver esp_timer)

target_compile_definitions(${COMPONENT_LIB} PUBLIC -DLV_CONF_INCLUDE_SIMPLE -DLV_USE_DEMO_MUSIC=1)
set_source_files_properties(${LVGL_EXAMPLES} ${LVGL_DEMO_MUSIC} COMPILE_FLAGS "-Wno-unused-variable -Wno-format")


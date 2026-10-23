# Pinned C-only LVGL build; optional third-party engines remain disabled.
file(GLOB_RECURSE LVGL_C_SOURCES "${ROOT}/third_party/lvgl/src/*.c")
add_library(lvgl STATIC ${LVGL_C_SOURCES} "${ROOT}/third_party/source_han_sans/sim_ui_font_16.c")
target_include_directories(lvgl SYSTEM PUBLIC "${ROOT}/third_party/lvgl/include" "${ROOT}/third_party/lvgl" "${ROOT}/app/ui")
target_compile_definitions(lvgl PUBLIC LV_CONF_INCLUDE_SIMPLE)
target_compile_definitions(lvgl PRIVATE _DEFAULT_SOURCE)
target_link_libraries(lvgl PUBLIC m)

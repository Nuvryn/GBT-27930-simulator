# Keep upstream sources and CMake unchanged. Only its library is pulled into ALL.
set(OPENSAE_J1939_TARGET_PLATFORM INTERNAL_CALLBACK)
add_subdirectory(third_party/open_sae_j1939 EXCLUDE_FROM_ALL)

# Expose upstream headers as system headers to consumers; do not impose the
# project's naming/formatting/warning policy on the third-party target.
get_target_property(open_sae_j1939_include_dirs opensaej1939 INTERFACE_INCLUDE_DIRECTORIES)
set_property(TARGET opensaej1939 PROPERTY
    INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${open_sae_j1939_include_dirs}")

add_library(open_sae_j1939_port STATIC app/transport/open_sae_j1939_port.c)
target_include_directories(open_sae_j1939_port PUBLIC "${ROOT}/app")
target_link_libraries(open_sae_j1939_port PUBLIC opensaej1939 scaffold_support)
target_compile_options(open_sae_j1939_port PRIVATE
    -Wall -Wextra -Werror -Wconversion -Wsign-conversion -Wstrict-prototypes)

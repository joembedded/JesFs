set(JESFS_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../..")

target_sources_ifdef(CONFIG_JESFS_SHELL app PRIVATE 
    jesfs_shell.c
    "${JESFS_ROOT}/jesfs_hl.c"
    "${JESFS_ROOT}/jesfs_ml.c"
    jesfs_ll_zephyr.c
)

target_include_directories(app PRIVATE
    "${JESFS_ROOT}"
)

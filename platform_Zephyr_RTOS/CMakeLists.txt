# MAIN-JESFS-Shell - Zephyr Framework for JesFs-Test

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(main-jesfs-shell)

target_sources(app PRIVATE  src/main.c)
target_include_directories(app PRIVATE src)
add_subdirectory(src/jesfs)

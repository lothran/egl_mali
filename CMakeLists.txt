cmake_minimum_required(VERSION 3.10)
project(egl_headless)
add_executable(egl_headless main.cpp egl.c gles2.c)

target_include_directories(egl_headless PUBLIC include)

install(TARGETS egl_headless DESTINATION bin)
file(COPY shaders DESTINATION ${CMAKE_BINARY_DIR})

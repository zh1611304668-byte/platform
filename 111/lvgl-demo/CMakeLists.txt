cmake_minimum_required(VERSION 3.16)
project(lvgl-demo LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# SDL2 路径配置
set(SDL2_DIR "C:/SDL2/x86_64-w64-mingw32")
set(SDL2_INCLUDE_DIRS "${SDL2_DIR}/include")
set(SDL2_LIBRARIES "${SDL2_DIR}/lib/libSDL2main.a" "${SDL2_DIR}/lib/libSDL2.dll.a")

# LVGL 路径配置
set(LVGL_DIR "${CMAKE_SOURCE_DIR}/../../lib/lvgl")

# 包含目录
include_directories(
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/ui
    ${SDL2_INCLUDE_DIRS}
    ${LVGL_DIR}
    ${CMAKE_SOURCE_DIR}/../../lib  # lv_conf.h 所在目录
)

# Critical: Define this globally so lv_conf.h is found
add_definitions(-DLV_CONF_INCLUDE_SIMPLE)
# Explicitly tell LVGL where to find lv_conf.h
add_definitions(-DLV_CONF_PATH=${CMAKE_SOURCE_DIR}/../../lib/lv_conf.h)

add_definitions(-DSDL_MAIN_HANDLED)

# 收集 LVGL 源文件 (核心)
file(GLOB_RECURSE LVGL_CORE_SOURCES
    "${LVGL_DIR}/src/core/*.c"
    "${LVGL_DIR}/src/draw/*.c"
    "${LVGL_DIR}/src/extra/*.c"
    "${LVGL_DIR}/src/font/*.c"
    "${LVGL_DIR}/src/hal/*.c"
    "${LVGL_DIR}/src/misc/*.c"
    "${LVGL_DIR}/src/widgets/*.c"
)

# 排除 SDL draw（需要单独处理）和其他 GPU
list(FILTER LVGL_CORE_SOURCES EXCLUDE REGEX ".*sdl.*")
list(FILTER LVGL_CORE_SOURCES EXCLUDE REGEX ".*stm32.*")
list(FILTER LVGL_CORE_SOURCES EXCLUDE REGEX ".*nxp.*")
list(FILTER LVGL_CORE_SOURCES EXCLUDE REGEX ".*arm2d.*")
list(FILTER LVGL_CORE_SOURCES EXCLUDE REGEX ".*swm341.*")

# 收集 UI 源文件
file(GLOB_RECURSE UI_SOURCES
    "${CMAKE_SOURCE_DIR}/ui/*.c"
)

# 主程序
set(SOURCES
    main.c
    lv_port_disp.c
    lv_port_indev.c
)


add_executable(${PROJECT_NAME} WIN32
    ${SOURCES}
    ${LVGL_CORE_SOURCES}
    ${UI_SOURCES}
)


target_link_libraries(${PROJECT_NAME}
    ${SDL2_LIBRARIES}
    mingw32
)

# 复制 SDL2.dll 到输出目录
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${SDL2_DIR}/bin/SDL2.dll"
    $<TARGET_FILE_DIR:${PROJECT_NAME}>
)

message(STATUS "LVGL Demo configured")
message(STATUS "SDL2 Include: ${SDL2_INCLUDE_DIRS}")
message(STATUS "LVGL Dir: ${LVGL_DIR}")

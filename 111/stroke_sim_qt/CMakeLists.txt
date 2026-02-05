cmake_minimum_required(VERSION 3.16)
project(stroke_sim_qt LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Find Qt6 (explicit)
find_package(Qt6 REQUIRED COMPONENTS Core Widgets Charts Qml Quick QuickWidgets Location Positioning)
set(QT_LIBS Qt6::Core Qt6::Widgets Qt6::Charts Qt6::Qml Qt6::Quick Qt6::QuickWidgets Qt6::Location Qt6::Positioning)

# LVGL paths
set(LVGL_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../lib/lvgl)

# SDL2 not needed - using Qt for display
# find_package(SDL2 QUIET)
# if(NOT SDL2_FOUND)
#     # Try pkg-config on Linux/macOS
#     find_package(PkgConfig)
#     if(PKG_CONFIG_FOUND)
#         pkg_check_modules(SDL2 QUIET sdl2)
#     endif()
# endif()

# Collect LVGL core sources
file(GLOB_RECURSE LVGL_CORE_SOURCES
    "${LVGL_DIR}/src/core/*.c"
    "${LVGL_DIR}/src/hal/*.c"
    "${LVGL_DIR}/src/misc/*.c"
    "${LVGL_DIR}/src/draw/*.c"
    "${LVGL_DIR}/src/font/*.c"
    "${LVGL_DIR}/src/widgets/*.c"
    "${LVGL_DIR}/src/extra/*.c"
)

# Exclude GPU-specific files
list(FILTER LVGL_CORE_SOURCES EXCLUDE REGEX ".*sdl.*")
list(FILTER LVGL_CORE_SOURCES EXCLUDE REGEX ".*gpu.*")
list(FILTER LVGL_CORE_SOURCES EXCLUDE REGEX ".*stm32.*")
list(FILTER LVGL_CORE_SOURCES EXCLUDE REGEX ".*nxp.*")
list(FILTER LVGL_CORE_SOURCES EXCLUDE REGEX ".*arm2d.*")
list(FILTER LVGL_CORE_SOURCES EXCLUDE REGEX ".*swm341.*")

# Collect UI sources
file(GLOB_RECURSE UI_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/ui/*.c")

# Include directories
include_directories(
    ${LVGL_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/ui
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/src
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/src/components
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/src/screens
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/src/fonts
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/src/images
    ${CMAKE_CURRENT_SOURCE_DIR}/../../lib
)

# SDL2 not needed
# if(SDL2_FOUND)
#     include_directories(${SDL2_INCLUDE_DIRS})
# endif()

# Define LV_CONF_INCLUDE_SIMPLE globally
add_definitions(-DLV_CONF_INCLUDE_SIMPLE)
add_definitions(-DLV_CONF_PATH=${CMAKE_CURRENT_SOURCE_DIR}/../../lib/lv_conf.h)

add_executable(stroke_sim_qt
    main.cpp
    mainwindow.h
    mainwindow.cpp
    stroke_detector.h
    stroke_detector.cpp
    csv_loader.h
    csv_loader.cpp
    simulation_worker.h
    simulation_worker.cpp
    chart_view.h
    chart_view.cpp
    lvgl_widget.h
    lvgl_widget.cpp
    training_mode.h
    training_mode.cpp
    GNSSProcessor.cpp
    resources.qrc
    app_icon.rc
    ${LVGL_CORE_SOURCES}
    ${UI_SOURCES}
)

# Mark all C source files to skip AUTOMOC/AUTOUIC
set_source_files_properties(
    ${LVGL_CORE_SOURCES}
    ${UI_SOURCES}
    PROPERTIES
    SKIP_AUTOMOC ON
    SKIP_AUTOUIC ON
)

target_link_libraries(stroke_sim_qt PRIVATE ${QT_LIBS})

# SDL2 not needed - using Qt for display
# if(SDL2_FOUND)
#     target_link_libraries(stroke_sim_qt PRIVATE ${SDL2_LIBRARIES})
# endif()

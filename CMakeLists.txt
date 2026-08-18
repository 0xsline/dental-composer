cmake_minimum_required(VERSION 3.16)
project(dental-composer VERSION 0.1.3 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# 同一份源码兼容 Qt 5.15（Win7 发布）与 Qt 6（macOS 发布/开发）
find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets PrintSupport)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets PrintSupport)

add_executable(dental-composer
    src/main.cpp
    src/mainwindow.cpp
    src/mainwindow.h
    src/canvasview.cpp
    src/canvasview.h
    src/exportdialog.cpp
    src/exportdialog.h
    src/patientinfodialog.cpp
    src/patientinfodialog.h
    src/selftest.cpp
    src/selftest.h
    src/qtcompat.h
)

target_link_libraries(dental-composer PRIVATE
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::PrintSupport
)

# 源码含中文：MSVC 必须按 UTF-8 解析字面量
if(MSVC)
    target_compile_options(dental-composer PRIVATE /utf-8)
endif()

if(WIN32)
    set_target_properties(dental-composer PROPERTIES WIN32_EXECUTABLE TRUE)
    target_sources(dental-composer PRIVATE resources/app.rc)
endif()

if(APPLE)
    set_target_properties(dental-composer PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "牙片拼图"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.morningdental.composer"
    )
endif()

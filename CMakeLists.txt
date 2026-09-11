cmake_minimum_required(VERSION 3.5)

project(kht LANGUAGES CXX)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_CXX_FLAGS_RELEASE "-O2 -flto -ffunction-sections -fdata-sections")
set(CMAKE_SHARED_LINKER_FLAGS "-Wl,--gc-sections -flto")
set(CMAKE_EXE_LINKER_FLAGS "-Wl,--gc-sections -flto -s")  # -s = strip

# QtCreator supports the following variables for Android, which are identical to qmake Android variables.
# Check https://doc.qt.io/qt/deployment-android.html for more information.
# They need to be set before the find_package( ...) calls below.

#if(ANDROID)
#    set(ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/android")
#    if (ANDROID_ABI STREQUAL "armeabi-v7a")
#        set(ANDROID_EXTRA_LIBS
#            ${CMAKE_CURRENT_SOURCE_DIR}/path/to/libcrypto.so
#            ${CMAKE_CURRENT_SOURCE_DIR}/path/to/libssl.so)
#    endif()
#endif()

find_package(QT NAMES Qt6 Qt5 COMPONENTS Widgets REQUIRED)
find_package(Qt${QT_VERSION_MAJOR} COMPONENTS Widgets REQUIRED)
find_package(Qt${QT_VERSION_MAJOR} COMPONENTS Sql REQUIRED)

# 定位 Qt6Core 私有头文件目录（自实现 xlsx 读写需要 QZipReader/QZipWriter，符号由 Qt6Core 导出）
set(QT_CORE_PRIVATE_INC "")
get_target_property(_qt6core_incs Qt${QT_VERSION_MAJOR}::Core INTERFACE_INCLUDE_DIRECTORIES)
foreach(_inc IN LISTS _qt6core_incs)
    file(GLOB _qzip_priv "${_inc}/[0-9]*/QtCore/private/qzipwriter_p.h")
    if(_qzip_priv)
        list(GET _qzip_priv 0 _qzip_first)
        string(REGEX REPLACE "/QtCore/private/qzipwriter_p.h$" "" QT_CORE_PRIVATE_INC "${_qzip_first}")
        break()
    endif()
endforeach()
if(QT_CORE_PRIVATE_INC)
    message(STATUS "Qt6Core private include dir: ${QT_CORE_PRIVATE_INC}")
endif()

set(PROJECT_SOURCES
        main.cpp
        kht.cpp
        kht.h
        sqlitedb.cpp
        sqlitedb.h
        aboutdialog.cpp
        aboutdialog.h
        tableselectordialog.h
        tableselectordialog.cpp
        tableio.h
        tableio.cpp
        tablebrowserdialog.h
        tablebrowserdialog.cpp
        columnselectordialog.h
        columnselectordialog.cpp
        tablemanagerdialog.h
        tablemanagerdialog.cpp
        kht.ui
        aboutdialog.ui
        tableselectordialog.ui
)
set(app_WINRC resources/logo.rc)
set(qrc_SOURCES resources/resources.qrc)

if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
    qt_add_executable(kht WIN32 #增加WIN32，否则独立运行时会出现命令行
        ${PROJECT_SOURCES}
        ${app_WINRC}
        ${qrc_SOURCES}

    )
else()
    if(ANDROID)
        add_library(kht SHARED
            ${PROJECT_SOURCES}
        )
    else()
        add_executable(kht
            ${PROJECT_SOURCES}
        )
    endif()
endif()

target_link_libraries(kht PRIVATE Qt${QT_VERSION_MAJOR}::Widgets)
target_link_libraries(kht PRIVATE Qt${QT_VERSION_MAJOR}::Sql)

if(QT_CORE_PRIVATE_INC)
    target_include_directories(kht PRIVATE "${QT_CORE_PRIVATE_INC}")
endif()

# ============================================================
# 部署目标: cmake --build . --target deploy
# 运行 windeployqt 并清理冗余文件（Qt6Network、触摸插件、OpenGL软件渲染器等）
# ============================================================
add_custom_target(deploy
    COMMAND "${CMAKE_COMMAND}"
        -DEXE_PATH="$<TARGET_FILE:kht>"
        -DQT_BIN_DIR="$<TARGET_FILE_DIR:Qt6::Core>"
        -P "${CMAKE_SOURCE_DIR}/deploy.cmake"
    COMMENT "Deploying kht with minimal dependencies..."
    DEPENDS kht
)

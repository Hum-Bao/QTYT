cmake_minimum_required(VERSION 3.16)
project(QTYT VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose build type: Debug Release RelWithDebInfo" FORCE)
endif()

option(LIGHTYT_SANITIZE "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(LIGHTYT_WIN_CONSOLE "Keep attached console for Windows executable" OFF)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-finput-charset=UTF-8 -fexec-charset=UTF-8 -Wall -Wextra -Wno-unused-parameter)
    add_compile_options("$<$<CONFIG:Release>:-O2;-fdata-sections;-ffunction-sections>")
    add_link_options("$<$<CONFIG:Release>:-Wl,--gc-sections>")
    if(LIGHTYT_SANITIZE)
        add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
        add_link_options(-fsanitize=address,undefined)
    endif()
endif()

find_package(Threads REQUIRED)

# Core Library Sources
set(CORE_SOURCES
    src/core/CookieManager.cpp
    src/core/CurlApi.cpp
    src/core/HttpClient.cpp
    src/core/InnerTubeParser.cpp
    src/core/WatchHistoryTracker.cpp
    src/core/YouTubeClient.cpp
    src/core/Process.cpp
    src/core/StreamResolver.cpp
    src/core/ThumbnailCache.cpp
    src/player/MpvApi.cpp
    src/player/LibMpvBackend.cpp
    src/player/IpcMpvBackend.cpp
    src/player/PlayerController.cpp
    src/utils/Logger.cpp
    src/utils/AppPaths.cpp
)

add_library(lightyt_core STATIC ${CORE_SOURCES})
target_include_directories(lightyt_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(lightyt_core PUBLIC
    Threads::Threads
    ${CMAKE_DL_LIBS}
)

if(WIN32)
    # Standard Windows socket provider
    target_link_libraries(lightyt_core PUBLIC ws2_32)
endif()

# ---------------------------------------------------------
# Standalone CLI Diagnostic & Test Tool
# ---------------------------------------------------------
add_executable(lightyt-cli src/cli_main.cpp)
target_link_libraries(lightyt-cli PRIVATE lightyt_core)

# ---------------------------------------------------------
# Unit Tests & CTest
# ---------------------------------------------------------
add_executable(test_core tests/test_core.cpp)
target_link_libraries(test_core PRIVATE lightyt_core)

enable_testing()
add_test(NAME core COMMAND test_core)


# ---------------------------------------------------------
# Qt GUI Application (Built when Qt6 is available)
# ---------------------------------------------------------
find_package(Qt6 COMPONENTS Core Gui Widgets Concurrent OpenGLWidgets QUIET)

if(Qt6_FOUND)
    set(CMAKE_AUTOMOC ON)
    set(CMAKE_AUTORCC ON)
    set(CMAKE_AUTOUIC ON)

    set(GUI_SOURCES
        src/main.cpp
        src/ui/MainWindow.cpp
        src/ui/FeedModel.cpp
        src/ui/FeedView.cpp
        src/ui/VideoCardDelegate.cpp
        src/ui/ThumbnailLoader.cpp
        src/ui/PlayerWidget.cpp
        src/ui/PlayerControlsOverlay.cpp
        src/ui/StatsForNerdsWidget.cpp
        src/ui/MpvGlWidget.cpp
        src/ui/CookieDialog.cpp
        src/utils/AppSettings.cpp
    )

    add_executable(QTYT ${GUI_SOURCES})
    target_link_libraries(QTYT PRIVATE
        lightyt_core
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::Concurrent
        Qt6::OpenGLWidgets
    )

    message(STATUS "Configuring QTYT with Qt6 (Core, Gui, Widgets, Concurrent, OpenGLWidgets)")

    if(WIN32)
        # Disable libQt6EntryPoint to prevent MinGW __imp___argc MSVCRT/UCRT linker mismatch
        set_target_properties(QTYT PROPERTIES QT_NO_ENTRYPOINT TRUE)
        if(NOT LIGHTYT_WIN_CONSOLE)
            set_target_properties(QTYT PROPERTIES WIN32_EXECUTABLE TRUE)
        endif()
    endif()
else()
    message(STATUS "Qt6 not found. Built lightyt-cli and test_core successfully.")
    message(STATUS "To build the Qt GUI (QTYT), specify CMAKE_PREFIX_PATH to your Qt 6.8+ installation.")
endif()

# ---------------------------------------------------------
# Installation & CPack Packaging
# ---------------------------------------------------------
include(GNUInstallDirs)

install(TARGETS lightyt-cli RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
if(TARGET QTYT)
    install(TARGETS QTYT RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

set(CPACK_PACKAGE_NAME "QTYT")
set(CPACK_PACKAGE_VENDOR "LightYT")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Ultra-Lightweight Native YouTube Client")
set(CPACK_PACKAGE_VERSION_MAJOR 1)
set(CPACK_PACKAGE_VERSION_MINOR 0)
set(CPACK_PACKAGE_VERSION_PATCH 0)
include(CPack)

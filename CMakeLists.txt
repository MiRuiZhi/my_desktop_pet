cmake_minimum_required(VERSION 3.15)
project(ScreenshotService LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(CMAKE_SYSTEM_NAME STREQUAL "Windows" OR MINGW)
    add_compile_definitions(WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
    add_link_options(-mwindows)
    add_link_options(-Wl,--subsystem,windows)
    add_link_options(-static-libgcc)
    add_link_options(-static-libstdc++)
endif()

add_executable(screenshot_svc
    src/main.cpp
    src/screenshot.cpp
    src/hotkey.cpp
    src/crypto.cpp
    src/stealth.cpp
)

target_include_directories(screenshot_svc PRIVATE include)

target_link_libraries(screenshot_svc
    gdi32
    user32
    advapi32
    ole32
    shell32
    shlwapi
    ntdll
    winhttp
)

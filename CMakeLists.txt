cmake_minimum_required(VERSION 3.20)
project(DamnShutUp LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_executable(DamnShutUp WIN32
    src/main.cpp
    src/util.cpp
    src/store.cpp
    src/audio.cpp
    src/buttons.cpp
    src/app.rc
    src/app.manifest
)

target_compile_definitions(DamnShutUp PRIVATE UNICODE _UNICODE _WIN32_WINNT=0x0A00 WINVER=0x0A00)
target_compile_options(DamnShutUp PRIVATE /W4 /permissive- /utf-8)
target_link_libraries(DamnShutUp PRIVATE ole32 advapi32 gdiplus dwmapi shell32)

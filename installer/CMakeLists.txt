cmake_minimum_required(VERSION 3.15)
project(VxrInstaller VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Setup Wizard (Setup.exe)
add_executable(Setup
    WIN32
    src/setup.cpp
)

target_link_libraries(Setup PRIVATE
    ole32
    shell32
    advapi32
)

# Uninstaller (uninstall.exe)
add_executable(uninstall
    WIN32
    src/uninstall.cpp
)

target_link_libraries(uninstall PRIVATE
    ole32
    shell32
    advapi32
)

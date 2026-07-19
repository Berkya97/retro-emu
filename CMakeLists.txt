cmake_minimum_required(VERSION 3.16)
project(nes CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

find_package(SDL2 REQUIRED CONFIG)

add_executable(nes
    src/main.cpp
    src/bus.cpp
    src/cpu.cpp
    src/ppu.cpp
    src/apu.cpp
    src/cartridge.cpp
    src/mapper000.cpp
    src/mapper001.cpp
    src/mapper004.cpp
    src/gb/gb.cpp
    src/gb/gbapu.cpp
    src/gb/gbcart.cpp
    src/gb/gbcpu.cpp
    src/gb/gbppu.cpp
)

target_compile_definitions(nes PRIVATE SDL_MAIN_HANDLED)
target_link_libraries(nes PRIVATE SDL2::SDL2)

if(MINGW)
    # Self-contained exe: -static folds libstdc++/libgcc/winpthread into the
    # binary (SDL2 stays dynamic; it is linked via its import library path)
    target_link_options(nes PRIVATE -static)
    target_compile_options(nes PRIVATE -Wall -Wextra)
endif()

# Put SDL2.dll next to the executable
add_custom_command(TARGET nes POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:SDL2::SDL2> $<TARGET_FILE_DIR:nes>
)

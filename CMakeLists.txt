cmake_minimum_required(VERSION 3.20)

project(rp2350_calculator LANGUAGES CXX)

option(CALC_BUILD_EMULATOR "Build the SDL2 desktop emulator when SDL2 is available" ON)
option(CALC_BUILD_TESTS "Build host-side tests" ON)
option(CALC_USE_FLOAT "Use float instead of double for CalcReal" OFF)

add_library(calc_core
    src/core/calculator.cpp
    src/core/evaluator.cpp
    src/core/graph.cpp
    src/core/renderer.cpp
)

target_include_directories(calc_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(calc_core PUBLIC cxx_std_17)

if(CALC_USE_FLOAT)
    target_compile_definitions(calc_core PUBLIC CALC_USE_FLOAT=1)
endif()

if(MSVC)
    target_compile_options(calc_core PRIVATE /W4 /permissive- /GR-)
else()
    target_compile_options(calc_core PRIVATE -Wall -Wextra -Wpedantic -fno-rtti -fno-exceptions)
endif()

if(CALC_BUILD_TESTS)
    enable_testing()

    add_executable(calc_core_tests
        tests/core_tests.cpp
    )
    target_link_libraries(calc_core_tests PRIVATE calc_core)
    target_compile_features(calc_core_tests PRIVATE cxx_std_17)
    add_test(NAME calc_core_tests COMMAND calc_core_tests)

    add_executable(calc_visual_verifier
        tests/visual_verifier.cpp
    )
    target_link_libraries(calc_visual_verifier PRIVATE calc_core)
    target_compile_features(calc_visual_verifier PRIVATE cxx_std_17)
    add_test(NAME calc_visual_verifier COMMAND calc_visual_verifier)

    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND)
        add_test(
            NAME tether_protocol_tests
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/tether_viewer.py --self-test
        )
    endif()
endif()

if(CALC_BUILD_EMULATOR)
    find_package(SDL2 QUIET)
    if(SDL2_FOUND)
        add_executable(calc_emulator
            src/emulator/main.cpp
        )
        target_link_libraries(calc_emulator PRIVATE calc_core)
        target_compile_features(calc_emulator PRIVATE cxx_std_17)

        if(TARGET SDL2::SDL2)
            target_link_libraries(calc_emulator PRIVATE SDL2::SDL2)
        else()
            target_include_directories(calc_emulator PRIVATE ${SDL2_INCLUDE_DIRS})
            target_link_libraries(calc_emulator PRIVATE ${SDL2_LIBRARIES})
        endif()

        if(TARGET SDL2::SDL2main)
            target_link_libraries(calc_emulator PRIVATE SDL2::SDL2main)
        endif()
    else()
        message(STATUS "SDL2 not found; calc_emulator target will not be built.")
    endif()
endif()

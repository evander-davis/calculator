cmake_minimum_required(VERSION 3.20)

if(NOT DEFINED ENV{PICO_SDK_PATH})
    message(FATAL_ERROR "Set PICO_SDK_PATH to the Pico SDK directory, for example C:/tmp/pico-sdk")
endif()

set(PICO_BOARD seeed_xiao_rp2350 CACHE STRING "Pico board")

include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(calc_tether_firmware C CXX ASM)

pico_sdk_init()

add_executable(calc_tether
    tether_main.cpp
    ../src/core/calculator.cpp
    ../src/core/evaluator.cpp
    ../src/core/graph.cpp
    ../src/core/renderer.cpp
)

target_include_directories(calc_tether PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/../include
)

target_compile_features(calc_tether PRIVATE cxx_std_17)

target_compile_options(calc_tether PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>
    $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>
)

if(CALC_USE_FLOAT)
    target_compile_definitions(calc_tether PRIVATE CALC_USE_FLOAT=1)
endif()

target_compile_definitions(calc_tether PRIVATE
    PICO_STDIO_USB_CONNECT_WAIT_TIMEOUT_MS=0
    PICO_STDIO_USB_DEFAULT_CRLF=0
)

target_link_libraries(calc_tether PRIVATE
    pico_stdlib
    pico_stdio_usb
)

pico_enable_stdio_usb(calc_tether 1)
pico_enable_stdio_uart(calc_tether 0)
pico_add_extra_outputs(calc_tether)

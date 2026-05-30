# CMake generated Testfile for 
# Source directory: C:/Documents/calculator
# Build directory: C:/Documents/calculator/build-sdl
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test([=[calc_core_tests]=] "C:/Documents/calculator/build-sdl/calc_core_tests.exe")
set_tests_properties([=[calc_core_tests]=] PROPERTIES  _BACKTRACE_TRIPLES "C:/Documents/calculator/CMakeLists.txt;41;add_test;C:/Documents/calculator/CMakeLists.txt;0;")
add_test([=[calc_visual_verifier]=] "C:/Documents/calculator/build-sdl/calc_visual_verifier.exe")
set_tests_properties([=[calc_visual_verifier]=] PROPERTIES  _BACKTRACE_TRIPLES "C:/Documents/calculator/CMakeLists.txt;48;add_test;C:/Documents/calculator/CMakeLists.txt;0;")

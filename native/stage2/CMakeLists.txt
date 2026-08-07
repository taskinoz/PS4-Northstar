cmake_minimum_required(VERSION 3.20)
project(northstar_ps4 LANGUAGES C CXX)

if(NOT DEFINED ENV{OO_PS4_TOOLCHAIN})
    message(FATAL_ERROR "OO_PS4_TOOLCHAIN is not configured. Install the OpenOrbis PS4 toolchain and set OO_PS4_TOOLCHAIN before building Stage 2.")
endif()

add_library(northstar_ps4 MODULE src/module.cpp src/runtime.cpp)
target_include_directories(northstar_ps4 PRIVATE include)
target_compile_features(northstar_ps4 PRIVATE cxx_std_20)
target_compile_definitions(northstar_ps4 PRIVATE NORTHSTAR_PS4=1)

# PRX conversion/link flags belong to the selected OpenOrbis toolchain file.

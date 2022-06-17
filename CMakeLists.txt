cmake_minimum_required(VERSION 3.22)

option(SWAPK_EXAMPLES_ENABLE "Enable example libs and applications"
  off)
option(SWAPK_EXAMPLES_BUILD_APPS "Build example applications"
  off)
option(SWAPK_EXAMPLES_LIB_PICO "Build pico SDK integration"
  on)

if(SWAPK_EXAMPLES_LIB_PICO)
  # Add environment variables
  include(${CMAKE_CURRENT_LIST_DIR}/lib/pico-sdk/pico_sdk_init.cmake)
  set(PICO_TOOLCHAIN_PATH $ENV{PICO_TOOLCHAIN_PATH})
endif()

project(swapkernel)

if(SWAPK_EXAMPLES_LIB_PICO)
  pico_sdk_init()
endif()

if(SWAPK_EXAMPLES_ENABLE)
  add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/examples)
endif()

################################
# Main air-quality MCU program #
################################

add_library(${PROJECT_NAME} INTERFACE)

target_sources(${PROJECT_NAME} INTERFACE
  ${CMAKE_CURRENT_LIST_DIR}/src/kernel.S
  ${CMAKE_CURRENT_LIST_DIR}/src/swapk.c)

target_include_directories(${PROJECT_NAME} INTERFACE
  ${CMAKE_CURRENT_LIST_DIR}/include)

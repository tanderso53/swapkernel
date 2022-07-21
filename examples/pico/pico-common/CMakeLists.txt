cmake_minimum_required(VERSION 3.22)

project(swapkernel-pico)

################################
# Main air-quality MCU program #
################################

add_library(${PROJECT_NAME} INTERFACE)

target_sources(${PROJECT_NAME} INTERFACE
  ${CMAKE_CURRENT_LIST_DIR}/src/swapk-pico-integration.c
  ${CMAKE_CURRENT_LIST_DIR}/src/swapk-pico-isr.S)

target_link_libraries(${PROJECT_NAME} INTERFACE
  swapkernel pico_stdlib pico_multicore pico_util
  hardware_exception pico_runtime)

target_compile_definitions(${PROJECT_NAME} INTERFACE
  PICO_TIME_DEFAULT_ALARM_POOL_DISABLED=1
  SWAPK_HARDWARE_THREADS=2
  SWAPK_UNMANAGED_PROCS=1)

target_include_directories(${PROJECT_NAME} INTERFACE
  ${CMAKE_CURRENT_LIST_DIR}/include)

target_include_directories(pico_sync_core INTERFACE
  ${CMAKE_CURRENT_LIST_DIR}/include)

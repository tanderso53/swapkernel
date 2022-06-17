cmake_minimum_required(VERSION 3.22)

add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/pico-common)

if(SWAPK_EXAMPLES_BUILD_APPS)
  add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/hello-world)
endif()

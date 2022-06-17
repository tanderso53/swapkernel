cmake_minimum_required(VERSION 3.22)

# Add environment variables
# include($ENV{PICO_SDK_PATH}/pico_sdk_init.cmake)
# set(PICO_TOOLCHAIN_PATH $ENV{PICO_TOOLCHAIN_PATH})

project(example-hello-world)
# pico_sdk_init()

################################
# Main air-quality MCU program #
################################

add_executable(${PROJECT_NAME})

target_sources(${PROJECT_NAME} PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}/src/main.c)

pico_enable_stdio_usb(${PROJECT_NAME} 1)
pico_enable_stdio_uart(${PROJECT_NAME} 1)

target_link_libraries(${PROJECT_NAME}
  swapkernel-pico)

# Need this to get our .uf2
pico_add_extra_outputs(${PROJECT_NAME})

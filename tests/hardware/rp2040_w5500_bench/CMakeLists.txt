cmake_minimum_required(VERSION 3.20)

if(NOT PICO_SDK_PATH)
    message(FATAL_ERROR "PICO_SDK_PATH is required")
endif()
if(NOT IOLIBRARY_ROOT)
    message(FATAL_ERROR "IOLIBRARY_ROOT is required")
endif()
if(NOT WIZNET_PICO_C_PATH)
    message(FATAL_ERROR "WIZNET_PICO_C_PATH is required")
endif()

include(${PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(rp2040_w5500_bench LANGUAGES C CXX ASM)

pico_sdk_init()

add_library(w5500_bench_iolibrary STATIC
    "${IOLIBRARY_ROOT}/Ethernet/wizchip_conf.c"
    "${IOLIBRARY_ROOT}/Ethernet/socket.c"
    "${IOLIBRARY_ROOT}/Ethernet/W5500/w5500.c"
)
target_include_directories(w5500_bench_iolibrary SYSTEM PUBLIC
    "${IOLIBRARY_ROOT}/Ethernet"
)
target_compile_definitions(w5500_bench_iolibrary PUBLIC _WIZCHIP_=W5500)
target_compile_options(w5500_bench_iolibrary PRIVATE
    -ffunction-sections -fdata-sections)
target_compile_options(w5500_bench_iolibrary PRIVATE -fstack-usage)

add_library(w5500_bench_transport STATIC
    "${WIZNET_PICO_C_PATH}/port/ioLibrary_Driver/src/wizchip_qspi_pio.c"
)
target_include_directories(w5500_bench_transport SYSTEM PUBLIC
    "${WIZNET_PICO_C_PATH}/port/ioLibrary_Driver/inc"
    "${WIZNET_PICO_C_PATH}/port"
)
target_compile_definitions(w5500_bench_transport PUBLIC
    _WIZCHIP_=W5500
    DEVICE_BOARD_NAME=W55RP20_EVB_PICO
    _WIZCHIP_SPI_SCLK_SPEED=43
)
target_compile_options(w5500_bench_transport PRIVATE -fstack-usage)
target_link_libraries(w5500_bench_transport PUBLIC
    w5500_bench_iolibrary
    pico_stdlib
    hardware_pio
    hardware_spi
    hardware_dma
    hardware_clocks
)
pico_generate_pio_header(w5500_bench_transport
    "${WIZNET_PICO_C_PATH}/port/ioLibrary_Driver/src/wizchip_qspi_pio.pio"
)

add_executable(rp2040_w5500_bench src/main.c)
target_include_directories(rp2040_w5500_bench SYSTEM PRIVATE
    "${IOLIBRARY_ROOT}/Ethernet"
    "${WIZNET_PICO_C_PATH}/port/ioLibrary_Driver/inc"
    "${WIZNET_PICO_C_PATH}/port"
)
target_compile_definitions(rp2040_w5500_bench PRIVATE
    _WIZCHIP_=W5500
    DEVICE_BOARD_NAME=W55RP20_EVB_PICO
    _WIZCHIP_SPI_SCLK_SPEED=43
)
target_compile_features(rp2040_w5500_bench PRIVATE c_std_11)
set_source_files_properties(src/main.c PROPERTIES
    COMPILE_OPTIONS "-Wall;-Wextra;-Werror;-Wno-unused-function"
)
target_link_libraries(rp2040_w5500_bench PRIVATE
    w5500_bench_iolibrary
    w5500_bench_transport
    pico_stdlib
    pico_stdio_usb
)
pico_enable_stdio_usb(rp2040_w5500_bench 1)
pico_enable_stdio_uart(rp2040_w5500_bench 0)
pico_add_extra_outputs(rp2040_w5500_bench)

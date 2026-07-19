cmake_minimum_required(VERSION 3.20)

option(DIAG_BUILD_FIRMWARE "Build RP2040 W5500 diagnostic firmware" OFF)
option(DIAG_EXPECT_SPI_STATUS "Driver revision claims SPI-status callback support" ON)
set(DIAG_GIT_SHA "" CACHE STRING "40-digit Git commit SHA embedded in firmware")
set(DIAG_GIT_DIRTY "" CACHE STRING "Git worktree dirty flag (0 or 1)")
set(DIAG_DIFF_SHA256 "" CACHE STRING "64-digit SHA-256 of the binary Git diff")
set(DIAG_BUILD_UTC "" CACHE STRING "UTC firmware build time (YYYY-MM-DDTHH:MM:SSZ)")

if(DIAG_BUILD_FIRMWARE)
    if(NOT PICO_SDK_PATH)
        message(FATAL_ERROR "PICO_SDK_PATH is required for the firmware build")
    endif()
    if(NOT IOLIBRARY_ROOT)
        message(FATAL_ERROR "IOLIBRARY_ROOT is required for the firmware build")
    endif()
    if(NOT WIZNET_PICO_C_PATH)
        message(FATAL_ERROR "WIZNET_PICO_C_PATH is required for the firmware build")
    endif()
    foreach(variable IN ITEMS
            DIAG_GIT_SHA
            DIAG_GIT_DIRTY
            DIAG_DIFF_SHA256
            DIAG_BUILD_UTC)
        if("${${variable}}" STREQUAL "")
            message(FATAL_ERROR "${variable} is required for the firmware build")
        endif()
    endforeach()
else()
    set(DIAG_GIT_SHA "0123456789abcdef0123456789abcdef01234567")
    set(DIAG_GIT_DIRTY "1")
    set(DIAG_DIFF_SHA256
        "89abcdef0123456789abcdef0123456789abcdef0123456789abcdef01234567")
    set(DIAG_BUILD_UTC "2026-07-19T12:34:56Z")
endif()

string(LENGTH "${DIAG_GIT_SHA}" DIAG_GIT_SHA_LENGTH)
if(NOT DIAG_GIT_SHA_LENGTH EQUAL 40 OR
   NOT DIAG_GIT_SHA MATCHES "^[0-9A-Fa-f]+$")
    message(FATAL_ERROR "DIAG_GIT_SHA must contain exactly 40 hexadecimal digits")
endif()
if(NOT DIAG_GIT_DIRTY STREQUAL "0" AND NOT DIAG_GIT_DIRTY STREQUAL "1")
    message(FATAL_ERROR "DIAG_GIT_DIRTY must be 0 or 1")
endif()
string(LENGTH "${DIAG_DIFF_SHA256}" DIAG_DIFF_SHA256_LENGTH)
if(NOT DIAG_DIFF_SHA256_LENGTH EQUAL 64 OR
   NOT DIAG_DIFF_SHA256 MATCHES "^[0-9A-Fa-f]+$")
    message(FATAL_ERROR
        "DIAG_DIFF_SHA256 must contain exactly 64 hexadecimal digits")
endif()
string(LENGTH "${DIAG_BUILD_UTC}" DIAG_BUILD_UTC_LENGTH)
if(NOT DIAG_BUILD_UTC_LENGTH EQUAL 20 OR
   NOT DIAG_BUILD_UTC MATCHES
       "^[0-9][0-9][0-9][0-9]-[0-9][0-9]-[0-9][0-9]T[0-9][0-9]:[0-9][0-9]:[0-9][0-9]Z$")
    message(FATAL_ERROR "DIAG_BUILD_UTC must use YYYY-MM-DDTHH:MM:SSZ")
endif()

if(DIAG_BUILD_FIRMWARE)
    include(${PICO_SDK_PATH}/external/pico_sdk_import.cmake)
endif()

project(rp2040_w5500_diag LANGUAGES C CXX ASM)

configure_file(
    src/diag_build_info.h.in
    "${CMAKE_CURRENT_BINARY_DIR}/diag_build_info.h"
    @ONLY
)

if(DIAG_BUILD_FIRMWARE)
    pico_sdk_init()

    set(DIAG_FIRMWARE_SOURCES
        src/diag_build_info.c
        src/diag_journal.c
        src/diag_net.c
        src/diag_protocol.c
        src/diag_runner.c
        src/diag_usb.c
        src/diag_watchdog.c
        src/diag_w5500_stages.c
        src/main.c
        src/usb_descriptors.c
        src/w5500_diag_board.c
        src/w5500_diag_status_contract.c
    )

    add_library(diag_iolibrary STATIC
        "${IOLIBRARY_ROOT}/Ethernet/wizchip_conf.c"
        "${IOLIBRARY_ROOT}/Ethernet/socket.c"
        "${IOLIBRARY_ROOT}/Ethernet/W5500/w5500.c"
        "${IOLIBRARY_ROOT}/Internet/DHCP/dhcp.c"
    )
    target_include_directories(diag_iolibrary SYSTEM PUBLIC
        "${IOLIBRARY_ROOT}/Ethernet"
        "${IOLIBRARY_ROOT}/Internet/DHCP"
    )
    target_compile_definitions(diag_iolibrary PUBLIC _WIZCHIP_=W5500)

    add_library(diag_wiznet_transport STATIC
        "${WIZNET_PICO_C_PATH}/port/ioLibrary_Driver/src/wizchip_qspi_pio.c"
    )
    target_include_directories(diag_wiznet_transport SYSTEM PUBLIC
        "${WIZNET_PICO_C_PATH}/port/ioLibrary_Driver/inc"
        "${WIZNET_PICO_C_PATH}/port"
    )
    target_compile_definitions(diag_wiznet_transport PUBLIC
        _WIZCHIP_=W5500
        DEVICE_BOARD_NAME=W55RP20_EVB_PICO
        _WIZCHIP_SPI_SCLK_SPEED=43
    )
    target_compile_options(diag_wiznet_transport PRIVATE -w)
    target_link_libraries(diag_wiznet_transport PUBLIC
        diag_iolibrary
        pico_stdlib
        hardware_pio
        hardware_spi
        hardware_dma
        hardware_clocks
    )
    pico_generate_pio_header(diag_wiznet_transport
        "${WIZNET_PICO_C_PATH}/port/ioLibrary_Driver/src/wizchip_qspi_pio.pio"
    )

    add_executable(rp2040_w5500_diag ${DIAG_FIRMWARE_SOURCES})
    target_include_directories(rp2040_w5500_diag PRIVATE
        "${CMAKE_CURRENT_BINARY_DIR}"
        include
        src
    )
    target_include_directories(rp2040_w5500_diag SYSTEM PRIVATE
        "${IOLIBRARY_ROOT}/Ethernet"
        "${IOLIBRARY_ROOT}/Internet/DHCP"
        "${WIZNET_PICO_C_PATH}/port/ioLibrary_Driver/inc"
        "${WIZNET_PICO_C_PATH}/port"
    )
    target_compile_definitions(rp2040_w5500_diag PRIVATE
        _WIZCHIP_=W5500
        DEVICE_BOARD_NAME=W55RP20_EVB_PICO
        _WIZCHIP_SPI_SCLK_SPEED=43
        DIAG_EXPECT_SPI_STATUS=$<BOOL:${DIAG_EXPECT_SPI_STATUS}>
    )
    target_compile_features(rp2040_w5500_diag PRIVATE c_std_11)
    target_compile_options(rp2040_w5500_diag PRIVATE
        -Wall
        -Wextra
        -Werror
        -pedantic
    )
    set_property(SOURCE src/w5500_diag_board.c APPEND PROPERTY
        COMPILE_OPTIONS -Wno-unused-function
    )
    target_link_libraries(rp2040_w5500_diag PRIVATE
        diag_iolibrary
        diag_wiznet_transport
        hardware_clocks
        hardware_watchdog
        pico_stdlib
        pico_unique_id
        tinyusb_device
        tinyusb_board
    )
    pico_enable_stdio_uart(rp2040_w5500_diag 0)
    pico_enable_stdio_usb(rp2040_w5500_diag 0)
    pico_add_extra_outputs(rp2040_w5500_diag)
else()
    enable_testing()

    get_filename_component(DIAG_REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)

    add_executable(test_diag_protocol
        src/diag_protocol.c
        tests/test_diag_protocol.c
    )
    add_executable(test_diag_build_info
        src/diag_build_info.c
        src/diag_protocol.c
        tests/test_diag_build_info.c
    )
    target_include_directories(test_diag_build_info PRIVATE
        "${CMAKE_CURRENT_BINARY_DIR}"
    )
    add_executable(test_diag_runner
        src/diag_runner.c
        tests/test_diag_runner.c
    )
    add_executable(test_diag_stage_phases
        src/diag_runner.c
        tests/test_diag_stage_phases.c
    )
    add_executable(test_diag_journal
        src/diag_journal.c
        tests/test_diag_journal.c
    )
    add_executable(test_diag_usb_identity
        tests/test_diag_usb_identity.c
    )
    add_executable(test_w5500_diag_status_contract
        src/w5500_diag_status_contract.c
        tests/test_w5500_diag_status_contract.c
    )
    target_include_directories(test_w5500_diag_status_contract PRIVATE
        "${DIAG_REPO_ROOT}/Ethernet"
    )
    target_compile_definitions(test_w5500_diag_status_contract PRIVATE
        _WIZCHIP_=W5500
    )
    add_executable(test_diag_net
        src/diag_runner.c
        src/diag_net.c
        tests/test_diag_net.c
    )

    foreach(target IN ITEMS
            test_diag_build_info
            test_diag_protocol
            test_diag_runner
            test_diag_stage_phases
            test_diag_journal
            test_diag_usb_identity
            test_w5500_diag_status_contract
            test_diag_net)
        target_include_directories(${target} PRIVATE src)
        target_compile_features(${target} PRIVATE c_std_11)
        target_compile_options(${target} PRIVATE
            -Wall
            -Wextra
            -Werror
            -pedantic
        )
    endforeach()

    add_test(NAME diag_protocol COMMAND test_diag_protocol)
    add_test(NAME diag_build_info COMMAND test_diag_build_info)
    add_test(NAME diag_runner COMMAND test_diag_runner)
    add_test(NAME diag_stage_phases COMMAND test_diag_stage_phases)
    add_test(NAME diag_journal COMMAND test_diag_journal)
    add_test(NAME diag_usb_identity COMMAND test_diag_usb_identity)
    add_test(NAME w5500_diag_status_contract COMMAND test_w5500_diag_status_contract)
    add_test(NAME diag_net COMMAND test_diag_net)
endif()

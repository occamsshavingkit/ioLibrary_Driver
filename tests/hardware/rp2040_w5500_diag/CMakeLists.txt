cmake_minimum_required(VERSION 3.13)

option(DIAG_BUILD_FIRMWARE "Build RP2040 W5500 diagnostic firmware" OFF)
option(DIAG_EXPECT_SPI_STATUS "Driver revision claims SPI-status callback support" ON)

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
    include(${PICO_SDK_PATH}/external/pico_sdk_import.cmake)
endif()

project(rp2040_w5500_diag LANGUAGES C CXX ASM)

if(DIAG_BUILD_FIRMWARE)
    pico_sdk_init()

    set(DIAG_FIRMWARE_SOURCES
        src/diag_journal.c
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
    set_source_files_properties(${DIAG_FIRMWARE_SOURCES} PROPERTIES
        COMPILE_OPTIONS "-Wall;-Wextra;-Werror;-pedantic"
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

    foreach(target IN ITEMS
            test_diag_protocol
            test_diag_runner
            test_diag_stage_phases
            test_diag_journal
            test_diag_usb_identity
            test_w5500_diag_status_contract)
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
    add_test(NAME diag_runner COMMAND test_diag_runner)
    add_test(NAME diag_stage_phases COMMAND test_diag_stage_phases)
    add_test(NAME diag_journal COMMAND test_diag_journal)
    add_test(NAME diag_usb_identity COMMAND test_diag_usb_identity)
    add_test(NAME w5500_diag_status_contract COMMAND test_w5500_diag_status_contract)
endif()

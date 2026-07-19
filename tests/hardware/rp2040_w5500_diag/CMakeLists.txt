cmake_minimum_required(VERSION 3.13)

option(DIAG_BUILD_FIRMWARE "Build RP2040 W5500 diagnostic firmware" OFF)

if(DIAG_BUILD_FIRMWARE)
    if(NOT PICO_SDK_PATH)
        message(FATAL_ERROR "PICO_SDK_PATH is required for the firmware build")
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
        src/main.c
        src/usb_descriptors.c
    )
    add_executable(rp2040_w5500_diag ${DIAG_FIRMWARE_SOURCES})
    target_include_directories(rp2040_w5500_diag PRIVATE
        include
        src
    )
    target_compile_features(rp2040_w5500_diag PRIVATE c_std_11)
    set_source_files_properties(${DIAG_FIRMWARE_SOURCES} PROPERTIES
        COMPILE_OPTIONS "-Wall;-Wextra;-Werror;-pedantic"
    )
    target_link_libraries(rp2040_w5500_diag PRIVATE
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

    add_executable(test_diag_protocol
        src/diag_protocol.c
        tests/test_diag_protocol.c
    )
    add_executable(test_diag_runner
        src/diag_runner.c
        tests/test_diag_runner.c
    )
    add_executable(test_diag_journal
        src/diag_journal.c
        tests/test_diag_journal.c
    )
    add_executable(test_diag_usb_identity
        tests/test_diag_usb_identity.c
    )

    foreach(target IN ITEMS
            test_diag_protocol
            test_diag_runner
            test_diag_journal
            test_diag_usb_identity)
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
    add_test(NAME diag_journal COMMAND test_diag_journal)
    add_test(NAME diag_usb_identity COMMAND test_diag_usb_identity)
endif()

cmake_minimum_required(VERSION 3.13)

project(rp2040_w5500_diag C)

option(DIAG_BUILD_FIRMWARE "Build RP2040 W5500 diagnostic firmware" OFF)

if(NOT DIAG_BUILD_FIRMWARE)
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
    target_include_directories(test_diag_protocol PRIVATE src)
    target_include_directories(test_diag_runner PRIVATE src)
    target_include_directories(test_diag_journal PRIVATE src)
    target_compile_features(test_diag_protocol PRIVATE c_std_11)
    target_compile_features(test_diag_runner PRIVATE c_std_11)
    target_compile_features(test_diag_journal PRIVATE c_std_11)
    target_compile_options(test_diag_protocol PRIVATE -Wall -Wextra -Werror -pedantic)
    target_compile_options(test_diag_runner PRIVATE -Wall -Wextra -Werror -pedantic)
    target_compile_options(test_diag_journal PRIVATE -Wall -Wextra -Werror -pedantic)

    add_test(NAME diag_protocol COMMAND test_diag_protocol)
    add_test(NAME diag_runner COMMAND test_diag_runner)
    add_test(NAME diag_journal COMMAND test_diag_journal)
endif()

add_executable(ucraft_game_tests game_tests.c
    ../src/game.c ../src/storage.c ../src/blocks.c ../src/log.c)
target_link_libraries(ucraft_game_tests PRIVATE ucraft_options)

foreach(test_case IN ITEMS spawn chunks placement placement_rejected breaking
        crafting_inventory crafting_table storage lifecycle time blocks)
    add_test(NAME game.${test_case} COMMAND ucraft_game_tests ${test_case})
    set_tests_properties(game.${test_case} PROPERTIES TIMEOUT 15 LABELS gameplay)
endforeach()

# Compile the same complete server sources with controlled external I/O.
get_target_property(server_sources UCraft SOURCES)
list(REMOVE_ITEM server_sources main.c)
list(TRANSFORM server_sources PREPEND "${PROJECT_SOURCE_DIR}/src/")
add_executable(ucraft_source_tests source_tests.c packet_tests.c state_tests.c auth_tests.c ${server_sources})
target_link_libraries(ucraft_source_tests PRIVATE ucraft_options)
target_include_directories(ucraft_source_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(ucraft_source_tests PRIVATE UCRAFT_TEST_HOOKS)
foreach(test_case IN ITEMS readers reader_bounds writers s2c queues writer_failures handlers
        inventory_capacity block_churn allocations players world digest auth auth_errors utilities)
    add_test(NAME source.${test_case} COMMAND ucraft_source_tests ${test_case})
    set_tests_properties(source.${test_case} PROPERTIES TIMEOUT 20 LABELS source)
endforeach()

if(UCRAFT_TCP_TESTS)
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
        message(FATAL_ERROR "UCRAFT_TCP_TESTS currently supports Linux only")
    endif()
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_test(NAME source.compression
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/compression_test.py"
            --directory "${CMAKE_CURRENT_BINARY_DIR}")
    set_tests_properties(source.writers PROPERTIES FIXTURES_SETUP compression_packets)
    set_tests_properties(source.compression PROPERTIES
        TIMEOUT 15 LABELS source FIXTURES_REQUIRED compression_packets)
    add_test(NAME server.status_ping
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/server_smoke.py"
            --server $<TARGET_FILE:UCraft>
            --config "${PROJECT_SOURCE_DIR}/config.h"
            --log "${CMAKE_CURRENT_BINARY_DIR}/server-smoke.log")
    set_tests_properties(server.status_ping PROPERTIES
        TIMEOUT 45 LABELS tcp RESOURCE_LOCK ucraft_server_port)
endif()

if(UCRAFT_COVERAGE)
    get_property(regression_tests DIRECTORY PROPERTY TESTS)
    set_tests_properties(${regression_tests} PROPERTIES
        ENVIRONMENT "LLVM_PROFILE_FILE=${CMAKE_BINARY_DIR}/profiles/%m-%p.profraw")
endif()

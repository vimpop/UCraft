add_executable(ucraft_game_tests game_tests.c
    ../src/game.c ../src/storage.c ../src/blocks.c ../src/log.c)
target_link_libraries(ucraft_game_tests PRIVATE ucraft_options)

foreach(test_case IN ITEMS spawn chunks placement placement_rejected breaking
        crafting_inventory crafting_table storage lifecycle time blocks)
    add_test(NAME game.${test_case} COMMAND ucraft_game_tests ${test_case})
    set_tests_properties(game.${test_case} PROPERTIES TIMEOUT 15 LABELS gameplay)
endforeach()

if(UCRAFT_TCP_TESTS)
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
        message(FATAL_ERROR "UCRAFT_TCP_TESTS currently supports Linux only")
    endif()
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_test(NAME server.status_ping
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/server_smoke.py"
            --server $<TARGET_FILE:UCraft>
            --config "${PROJECT_SOURCE_DIR}/config.h"
            --log "${CMAKE_CURRENT_BINARY_DIR}/server-smoke.log")
    set_tests_properties(server.status_ping PROPERTIES
        TIMEOUT 45 LABELS tcp RESOURCE_LOCK ucraft_server_port)
endif()

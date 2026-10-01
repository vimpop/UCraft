#find mbedtls in the system
find_package(MbedTLS QUIET)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()
if(MbedTLS_FOUND)
    message(STATUS "Found mbedtls: ${mbedtls_INCLUDE_DIRS} ${mbedtls_LIBRARIES}")
else()
    message(STATUS "Using bundled mbedtls")
    if(NOT EXISTS "${CMAKE_CURRENT_LIST_DIR}/../../mbedtls/CMakeLists.txt")
        find_package(Git REQUIRED)
        execute_process(
            COMMAND ${GIT_EXECUTABLE} submodule update --init --recursive 3rdparty/mbedtls
            WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/../../..
            RESULT_VARIABLE mbedtls_checkout_result
        )
        if(NOT mbedtls_checkout_result EQUAL 0)
            message(FATAL_ERROR "Could not initialize the mbedtls submodule")
        endif()
    endif()
    option(ENABLE_TESTING "Build and run tests" OFF)
    option(ENABLE_PROGRAMS "Build mbed TLS programs." OFF)

    if(NOT WIN32)
        set(CMAKE_C_COMPILER   "${CMAKE_C_COMPILER}"   CACHE INTERNAL "")
        set(CMAKE_CXX_COMPILER "${CMAKE_CXX_COMPILER}" CACHE INTERNAL "")
        set(CMAKE_ASM_COMPILER "${CMAKE_ASM_COMPILER}" CACHE INTERNAL "")
    endif()

    add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../../mbedtls)
    target_include_directories(mbedtls PUBLIC ${CMAKE_CURRENT_LIST_DIR}/)
    if(NOT MBEDTLS_CONFIG_FILE)
        target_compile_definitions(mbedtls PUBLIC MBEDTLS_CONFIG_FILE="mbedtls_custom_config.h")
    endif()
endif()

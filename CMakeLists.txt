cmake_minimum_required(VERSION 3.16)
project(vnreader LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Host build: warnings are errors, ASan + UBSan on everything.
add_compile_options(-Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer -g)
add_link_options(-fsanitize=address,undefined)

# Vendored dictionary (third_party/switch-vnpop, do not edit).
set(VNPOP_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/switch-vnpop)
add_library(vnpop_dict STATIC
    ${VNPOP_DIR}/core/dict/vnpop_dict.c
    ${VNPOP_DIR}/core/dict/vnpop_deinflect.c
    ${VNPOP_DIR}/core/dict/vnpop_inflate.c
    ${VNPOP_DIR}/core/vnpop_ledger.c
)
target_include_directories(vnpop_dict PUBLIC ${VNPOP_DIR}/include ${VNPOP_DIR}/core/dict)

enable_testing()
find_package(Python3 COMPONENTS Interpreter REQUIRED)

foreach(fixture test_yomitan_fixture:test_dict test_long_run:test_long_run)
    string(REPLACE ":" ";" pair ${fixture})
    list(GET pair 0 src)
    list(GET pair 1 out)
    add_custom_command(
        OUTPUT ${CMAKE_BINARY_DIR}/${out}.bin
        COMMAND Python3::Interpreter ${VNPOP_DIR}/tools/dict_packer.py
                ${VNPOP_DIR}/tests/fixtures/${src}.json -o ${CMAKE_BINARY_DIR}/${out}.bin
        DEPENDS ${VNPOP_DIR}/tools/dict_packer.py ${VNPOP_DIR}/tests/fixtures/${src}.json)
    list(APPEND TEST_DICTS ${CMAKE_BINARY_DIR}/${out}.bin)
endforeach()
add_custom_target(test_dicts DEPENDS ${TEST_DICTS})

# Upstream dictionary tests, driven by our own main.
add_executable(dict_tests
    tests/dict_tests_main.cpp
    ${VNPOP_DIR}/tests/test_dict.cpp
    ${VNPOP_DIR}/tests/test_lookup.cpp
)
target_include_directories(dict_tests PRIVATE ${VNPOP_DIR}/tests)
target_link_libraries(dict_tests PRIVATE vnpop_dict)
add_dependencies(dict_tests test_dicts)
add_test(NAME dict_tests COMMAND dict_tests WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# core/ (pure C++17) and its tests are added here once the core plan is approved.

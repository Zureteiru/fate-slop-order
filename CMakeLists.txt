cmake_minimum_required(VERSION 3.16)
project(vnreader LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# host    = tests only, gcc/clang with ASan+UBSan (tests/run.sh)
# desktop = the app for Windows / macOS / Linux (apps/desktop)
# switch / android come later as thin wrappers over the same runtime.
set(VNR_TARGET "host" CACHE STRING "host | desktop")
set_property(CACHE VNR_TARGET PROPERTY STRINGS host desktop)

if(VNR_TARGET STREQUAL "host")
    # Non-PIE: gcc 12's ASan hangs before main() in about half the runs of a PIE binary on kernels
    # with vm.mmap_rnd_bits=32 (WSL2 6.x); measured 9/20 hangs with PIE, 0/20 without.
    add_compile_options(-Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer -g -fno-pie)
    add_link_options(-fsanitize=address,undefined -no-pie)
elseif(MSVC)
    add_compile_options(/W4 /utf-8 /permissive- /wd4100 /wd4244 /wd4267 /wd4305)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
else()
    add_compile_options(-Wall -Wextra)
endif()

# ---- core/: pure C++17 script engine
add_library(vnr_core STATIC
    core/src/script_lines.cpp
    core/src/command.cpp
    core/src/rich_text.cpp
    core/src/program.cpp
    core/src/tween.cpp
    core/src/stage.cpp
    core/src/assets.cpp
    core/src/player.cpp
)
target_include_directories(vnr_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/core/include)

# ---- runtime/: the shared app logic every platform wrapper drives (pure C++17)
add_library(vnr_runtime STATIC
    runtime/src/app.cpp
    runtime/src/audio.cpp
    runtime/src/audio_mixer.cpp
    runtime/src/mp3.cpp
    runtime/src/dr_mp3_impl.cpp
    runtime/src/asset_cache.cpp
    runtime/src/draw_list.cpp
    runtime/src/image.cpp
    runtime/src/json.cpp
    runtime/src/layer_mask.cpp
    runtime/src/special_mask.cpp
    runtime/src/title_card.cpp
    runtime/src/wipe_mask.cpp
    runtime/src/backlog.cpp
    runtime/src/catalogue.cpp
    runtime/src/focus_nav.cpp
    runtime/src/picker.cpp
    runtime/src/scene_render.cpp
    runtime/src/text.cpp
    runtime/src/ui_skin.cpp
    runtime/src/stb_impl.cpp
)
target_include_directories(vnr_runtime PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
                                       PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb)
target_include_directories(vnr_runtime SYSTEM PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party/dr_libs)
target_link_libraries(vnr_runtime PUBLIC vnr_core)
# vendored stb / dr_mp3: third-party code, compiled without project warnings
if(MSVC)
    set_source_files_properties(runtime/src/stb_impl.cpp runtime/src/dr_mp3_impl.cpp PROPERTIES COMPILE_OPTIONS "/W0")
else()
    set_source_files_properties(runtime/src/stb_impl.cpp runtime/src/dr_mp3_impl.cpp PROPERTIES COMPILE_OPTIONS "-w")
endif()

if(VNR_TARGET STREQUAL "host")
    # ---- vendored dictionary (third_party/switch-vnpop, do not edit) and its tests
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

    add_executable(dict_tests
        tests/dict_tests_main.cpp
        ${VNPOP_DIR}/tests/test_dict.cpp
        ${VNPOP_DIR}/tests/test_lookup.cpp
    )
    target_include_directories(dict_tests PRIVATE ${VNPOP_DIR}/tests)
    target_link_libraries(dict_tests PRIVATE vnpop_dict)
    add_dependencies(dict_tests test_dicts)
    add_test(NAME dict_tests COMMAND dict_tests WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

    set(FIXTURES "${CMAKE_CURRENT_SOURCE_DIR}/.cache/scripts")
    add_executable(core_tests
        tests/core_main.cpp
        tests/core_text_tests.cpp
        tests/core_program_tests.cpp
        tests/core_assets_tests.cpp
        tests/core_player_tests.cpp
        tests/core_golden_tests.cpp
    )
    target_link_libraries(core_tests PRIVATE vnr_core)
    target_compile_definitions(core_tests PRIVATE VNR_FIXTURE_DIR="${FIXTURES}")
    add_test(NAME core_tests COMMAND core_tests)

    add_executable(runtime_tests tests/core_main.cpp tests/runtime_tests.cpp tests/audio_tests.cpp tests/picker_tests.cpp)
    target_include_directories(runtime_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    target_link_libraries(runtime_tests PRIVATE vnr_runtime)
    target_compile_definitions(runtime_tests PRIVATE VNR_FIXTURE_DIR="${FIXTURES}")
    add_test(NAME runtime_tests COMMAND runtime_tests)

    # Host inspection tool (not part of core).
    add_executable(script_dump tools/script_dump.cpp)
    target_link_libraries(script_dump PRIVATE vnr_core)
endif()

if(VNR_TARGET STREQUAL "desktop")
    add_subdirectory(platform/sdl2)
    add_subdirectory(apps/desktop)
endif()

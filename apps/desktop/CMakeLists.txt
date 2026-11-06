# Desktop wrapper: Windows now. macOS/Linux reuse main.cpp and add an os_posix.cpp
# (libcurl HTTP, cache dir, Hiragino / Noto CJK font) in a later step.
add_executable(vnreader WIN32 main.cpp)
target_include_directories(vnreader PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(vnreader PRIVATE vnr_sdl2)

if(WIN32)
    target_sources(vnreader PRIVATE os_windows.cpp)
    target_link_libraries(vnreader PRIVATE winhttp)
else()
    message(FATAL_ERROR "The macOS/Linux desktop wrapper (os_posix.cpp) is a later step; see apps/README.md")
endif()

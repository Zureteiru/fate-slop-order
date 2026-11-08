# Desktop wrapper: Windows (os_windows.cpp) and Linux / PortMaster (os_posix.cpp); macOS later.
add_executable(vnreader WIN32 main.cpp)
target_include_directories(vnreader PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(vnreader PRIVATE vnr_sdl2)

if(WIN32)
    target_sources(vnreader PRIVATE os_windows.cpp)
    target_link_libraries(vnreader PRIVATE winhttp)
elseif(APPLE)
    message(FATAL_ERROR "The macOS wrapper is a later step; see apps/README.md")
else()
    # Linux (desktop and PortMaster handhelds): libcurl is dlopen()ed, nothing of it is needed to build.
    find_package(Threads REQUIRED)
    target_sources(vnreader PRIVATE os_posix.cpp)
    target_link_libraries(vnreader PRIVATE Threads::Threads ${CMAKE_DL_LIBS})
endif()

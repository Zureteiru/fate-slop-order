# Switch wrapper: builds vnreader.nro with devkitA64 (see apps/README.md, "Switch").
add_executable(vnreader main.cpp os_switch.cpp)
target_include_directories(vnreader PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(vnreader PRIVATE vnr_sdl2)

# devkitPro's libcurl (TLS through libnx's ssl service) has no CMake package; link it by hand.
find_path(VNR_CURL_INCLUDE curl/curl.h REQUIRED)
find_library(VNR_CURL_LIB curl REQUIRED)
find_library(VNR_Z_LIB z REQUIRED)
target_include_directories(vnreader PRIVATE ${VNR_CURL_INCLUDE})
target_link_libraries(vnreader PRIVATE ${VNR_CURL_LIB} ${VNR_Z_LIB})

nx_generate_nacp(vnreader.nacp NAME "Fate/Slop Order" AUTHOR "Zureteiru" VERSION "0.1.0")
nx_create_nro(vnreader NACP vnreader.nacp ICON ${CMAKE_CURRENT_SOURCE_DIR}/icon.jpg)

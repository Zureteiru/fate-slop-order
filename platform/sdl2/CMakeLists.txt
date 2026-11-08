# Shared SDL2 backend: used by every wrapper (desktop now; Switch and Android later).
# Platforms that ship SDL2 (devkitPro portlibs, Android's SDL project) provide the SDL2::SDL2-static
# or SDL2::SDL2 target themselves; otherwise a pinned release is downloaded and built.
if(NOT TARGET SDL2::SDL2-static AND NOT TARGET SDL2::SDL2)
    include(FetchContent)
    # VNR_SDL_SHARED: link libSDL2-2.0.so.0 instead, so a handheld uses its firmware's SDL (KMSDRM, its
    # GPU driver, its controller setup). The library built here only provides the link-time symbols.
    option(VNR_SDL_SHARED "Link SDL2 as a shared library (PortMaster)" OFF)
    set(SDL_SHARED ${VNR_SDL_SHARED} CACHE BOOL "" FORCE)
    if(VNR_SDL_SHARED)
        set(SDL_STATIC OFF CACHE BOOL "" FORCE)
    else()
        set(SDL_STATIC ON CACHE BOOL "" FORCE)
    endif()
    set(SDL_TEST OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(SDL2
        URL https://github.com/libsdl-org/SDL/releases/download/release-2.32.10/SDL2-2.32.10.tar.gz
        URL_HASH SHA256=5f5993c530f084535c65a6879e9b26ad441169b3e25d789d83287040a9ca5165)
    FetchContent_MakeAvailable(SDL2)
endif()

add_library(vnr_sdl2 STATIC sdl_backend.cpp)
target_include_directories(vnr_sdl2 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(vnr_sdl2 PUBLIC vnr_runtime)
if(TARGET SDL2::SDL2-static)
    target_link_libraries(vnr_sdl2 PUBLIC SDL2::SDL2-static)
else()
    target_link_libraries(vnr_sdl2 PUBLIC SDL2::SDL2)
endif()
if(TARGET SDL2::SDL2main)
    target_link_libraries(vnr_sdl2 PUBLIC SDL2::SDL2main)
endif()

add_library(sdl.hdll
    sdl.c
    gl.c
)

if ((NOT WIN32) AND DOWNLOAD_DEPENDENCIES)
    FetchContent_Declare(
        SDL3
        URL https://github.com/libsdl-org/SDL/releases/download/release-3.2.8/SDL3-3.2.8.tar.gz
        URL_HASH SHA256=13388fabb361de768ecdf2b65e52bb27d1054cae6ccb6942ba926e378e00db03
        EXCLUDE_FROM_ALL
    )
    set(SDL_TESTS OFF CACHE INTERNAL "")
    set(SDL_STATIC ON CACHE INTERNAL "")
    set(SDL_SHARED OFF CACHE INTERNAL "")
    if (BUILD_SHARED_LIBS)
        set(SDL_STATIC_PIC ON CACHE INTERNAL "")
    endif()
    FetchContent_MakeAvailable(SDL3)
    target_include_directories(sdl.hdll
        PRIVATE
        ${INCLUDES_BASE_DIR}/gl
    )
    target_link_libraries(sdl.hdll
        SDL3::SDL3
    )
else()
    if(WIN32)
        # On Windows, SDL3 is extracted to include/sdl/ from the CI script
        # Try to find SDL3 config, or use the include path directly
        if(EXISTS "${INCLUDES_BASE_DIR}/sdl/SDL3/SDL.h")
            target_include_directories(sdl.hdll
                PRIVATE
                ${INCLUDES_BASE_DIR}/sdl
                ${INCLUDES_BASE_DIR}/gl
            )
            # Add SDL3 lib directory based on architecture
            if(CMAKE_SIZEOF_VOID_P EQUAL 8)
                target_link_directories(sdl.hdll PRIVATE ${INCLUDES_BASE_DIR}/sdl/lib/x64)
            else()
                target_link_directories(sdl.hdll PRIVATE ${INCLUDES_BASE_DIR}/sdl/lib/x86)
            endif()
            # SDL3 on Windows needs these libraries
            target_link_libraries(sdl.hdll
                SDL3
            )
        else()
            find_package(SDL3 REQUIRED)
            target_link_libraries(sdl.hdll
                SDL3::SDL3
            )
        endif()
    else()
        find_package(SDL3 REQUIRED)
        target_include_directories(sdl.hdll
            PRIVATE
            ${INCLUDES_BASE_DIR}/gl
        )
        target_link_libraries(sdl.hdll
            SDL3::SDL3
        )
    endif()
endif()

set_as_hdll(sdl)
target_link_libraries(sdl.hdll
    libhl
)
if(APPLE)
    target_compile_definitions(sdl.hdll PRIVATE GL_SILENCE_DEPRECATION)
endif()

if(WIN32)
    target_link_libraries(sdl.hdll
        winmm
        opengl32
    )
endif()

if(ANDROID)
    target_link_libraries(sdl.hdll GLESv3)
endif()

if(APPLE AND NOT ANDROID)
    find_package(OpenGL REQUIRED)
    target_include_directories(sdl.hdll
        PRIVATE
        ${OPENGL_INCLUDE_DIR}
    )
    target_link_libraries(sdl.hdll
        libhl
        ${OPENGL_gl_LIBRARY}
    )
endif()
if(UNIX AND NOT APPLE AND NOT ANDROID)
    target_link_libraries(sdl.hdll
        libhl
        GL
    )
endif()

install(
    TARGETS sdl.hdll
    DESTINATION ${HDLL_DESTINATION}
)

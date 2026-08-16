cmake_minimum_required(VERSION 3.16)
project(OpenTournament LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# --- SDL2 (built from the vendored submodule) ---
set(SDL_SHARED ON CACHE BOOL "" FORCE)
set(SDL_STATIC OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/SDL2 EXCLUDE_FROM_ALL)

add_subdirectory(third_party/enet EXCLUDE_FROM_ALL)

# --- Game target ---
add_executable(opentournament
    src/main.cpp
    src/render/Renderer.cpp
    src/render/Camera.cpp
    src/render/Mesh.cpp
    src/render/Font.cpp
    src/ui/Menu.cpp
    src/input/Input.cpp
    src/game/Level.cpp
    src/game/Player.cpp
    src/game/Weapon.cpp
    src/game/CollisionWorld.cpp
    src/game/BrushCollisionWorld.cpp
    src/net/Server.cpp
    src/net/Client.cpp
    src/map/QuakeMap.cpp
    src/map/OtMap.cpp
    src/map/MapFormat.cpp
    src/map/BspMap.cpp
)

target_include_directories(opentournament PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/glm
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/enet/include
)

target_link_libraries(opentournament PRIVATE
    SDL2::SDL2main
    SDL2::SDL2
    enet
    ue2conv
)

if(WIN32)
    target_link_libraries(opentournament PRIVATE glew32 opengl32)
endif()

if(MSVC)
    target_compile_options(opentournament PRIVATE /W4)
else()
    target_compile_options(opentournament PRIVATE -Wall -Wextra)
endif()

# Copy runtime DLLs (SDL2 + MinGW runtime) next to the executable so the game
# can be run without a MinGW/MSYS2 environment on PATH.
if(WIN32 AND MINGW)
    get_filename_component(OT_MINGW_BIN "${CMAKE_CXX_COMPILER}" DIRECTORY)
    set(OT_RUNTIME_DLLS
        "${OT_MINGW_BIN}/libstdc++-6.dll"
        "${OT_MINGW_BIN}/libwinpthread-1.dll"
        "${OT_MINGW_BIN}/glew32.dll"
    )
    foreach(_dll IN ITEMS libgcc_s_seh-1.dll libgcc_s_dw2-1.dll)
        if(EXISTS "${OT_MINGW_BIN}/${_dll}")
            list(APPEND OT_RUNTIME_DLLS "${OT_MINGW_BIN}/${_dll}")
        endif()
    endforeach()

    add_custom_command(TARGET opentournament POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:SDL2::SDL2>"
            "$<TARGET_FILE_DIR:opentournament>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${OT_RUNTIME_DLLS}
            "$<TARGET_FILE_DIR:opentournament>"
        COMMENT "Copying runtime DLLs next to opentournament.exe"
    )
endif()

# --- UE2 asset tooling (no SDL/GL dependency) ---
add_library(ue2conv STATIC
    tools/ue2/main.cpp
    tools/ue2/UE2.cpp
    src/map/MapFormat.cpp
)
target_include_directories(ue2conv PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/tools/ue2
)
if(MSVC)
    target_compile_options(ue2conv PRIVATE /W4)
else()
    target_compile_options(ue2conv PRIVATE -Wall -Wextra)
endif()

add_executable(ue2tool
    tools/ue2/ue2tool_main.cpp
)
target_link_libraries(ue2tool PRIVATE ue2conv)

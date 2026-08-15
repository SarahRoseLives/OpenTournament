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
    src/input/Input.cpp
    src/game/Level.cpp
    src/game/Player.cpp
    src/game/Weapon.cpp
    src/game/CollisionWorld.cpp
    src/net/Server.cpp
    src/net/Client.cpp
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
)

if(WIN32)
    target_link_libraries(opentournament PRIVATE glew32 opengl32)
endif()

if(MSVC)
    target_compile_options(opentournament PRIVATE /W4)
else()
    target_compile_options(opentournament PRIVATE -Wall -Wextra)
endif()

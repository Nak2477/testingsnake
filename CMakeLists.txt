cmake_minimum_required(VERSION 3.10)
project(HardcoreSnake VERSION 1.0 LANGUAGES C CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Set C standard
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Compiler flags
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wno-psabi")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wno-psabi")

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -O0 -fsanitize=address -fno-omit-frame-pointer")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g -O0 -fsanitize=address -fno-omit-frame-pointer")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=address")
else()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O2")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O2")
endif()

# Find SDL2
find_package(SDL2 REQUIRED)
find_package(SDL2_ttf REQUIRED)

# Include directories
include_directories(
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/libs
    ${CMAKE_SOURCE_DIR}/libs/jansson
    ${SDL2_INCLUDE_DIRS}
)

# Jansson library sources
file(GLOB JANSSON_SOURCES 
    "libs/jansson/*.c"
)

# Create jansson library
add_library(jansson STATIC ${JANSSON_SOURCES})
target_include_directories(jansson PUBLIC 
    ${CMAKE_SOURCE_DIR}/libs/jansson
)

# Multiplayer API library
add_library(multiplayer_api STATIC
    libs/MultiplayerApi.c
)
target_link_libraries(multiplayer_api
    jansson
    uuid
    curl
    pthread
    m
)

# Snake game C++ sources
set(SNAKE_CPP_SOURCES
    src/hardcoresnake.cpp
    src/rendermenu.cpp
    src/multiplayer.cpp
    src/game.cpp
)

# Create main executable
add_executable(HardcoreSnake
    ${SNAKE_CPP_SOURCES}
    src/main.cpp
)

# Link libraries
target_link_libraries(HardcoreSnake
    multiplayer_api
    jansson
    ${SDL2_LIBRARIES}
    SDL2_ttf
    pthread
    m
)

# Print build information
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ Compiler: ${CMAKE_CXX_COMPILER}")
message(STATUS "C Compiler: ${CMAKE_C_COMPILER}")
message(STATUS "SDL2 Include: ${SDL2_INCLUDE_DIRS}")
message(STATUS "SDL2 Libraries: ${SDL2_LIBRARIES}")

# =============================================================================
# Unit Testing Setup with Google Test
# =============================================================================
option(BUILD_TESTS "Build unit tests" OFF)

if(BUILD_TESTS)
    enable_testing()
    
    # Download and configure Google Test
    include(FetchContent)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.14.0
    )
    
    # For Windows: Prevent overriding parent project's compiler/linker settings
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
    
    # Create a library with game logic (no main.cpp)
    # This allows tests to link against game code without duplicate main()
    add_library(snake_core STATIC
        ${SNAKE_CPP_SOURCES}
    )
    target_link_libraries(snake_core
        multiplayer_api
        jansson
        ${SDL2_LIBRARIES}
        SDL2_ttf
        pthread
        m
    )
    
    # Test executables
    add_executable(test_snake tests/test_snake.cpp)
    target_link_libraries(test_snake
        snake_core
        gtest_main
        gmock
    )
    
    add_executable(test_food tests/test_food.cpp)
    target_link_libraries(test_food
        snake_core
        gtest_main
        gmock
    )
    
    add_executable(test_collision tests/test_collision.cpp)
    target_link_libraries(test_collision
        snake_core
        gtest_main
        gmock
    )
    
    add_executable(test_network tests/test_network.cpp)
    target_link_libraries(test_network
        snake_core
        gtest_main
        gmock
    )
    
    # Register tests with CTest
    include(GoogleTest)
    gtest_discover_tests(test_snake)
    gtest_discover_tests(test_food)
    gtest_discover_tests(test_collision)
    gtest_discover_tests(test_network)
    
    message(STATUS "Unit tests enabled - run with 'make test' or 'ctest'")
endif()

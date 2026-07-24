cmake_minimum_required(VERSION 3.16)
project(TypeDuel LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Expects SFML 3.x to be available via vcpkg (see README.md for setup)
find_package(SFML 3 COMPONENTS Graphics Window System REQUIRED)

add_executable(TypeDuel src/main.cpp)
target_link_libraries(TypeDuel PRIVATE SFML::Graphics SFML::Window SFML::System)

# Copy the assets folder next to the built executable so the font loads correctly
add_custom_command(TARGET TypeDuel POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/assets $<TARGET_FILE_DIR:TypeDuel>/assets)

cmake_minimum_required(VERSION 3.20)
project(BandoriSandbox CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Set engine root directory
set(ENGINE_ROOT ${CMAKE_SOURCE_DIR}/../..)

# Link to engine library (build it if needed)
add_subdirectory(${ENGINE_ROOT} ${CMAKE_BINARY_DIR}/engine)

# Build game executable
add_executable(BandoriSandbox
    src/main.cpp
    src/BandoriRenderer.cpp
)

target_link_libraries(BandoriSandbox PRIVATE MusicGameEngine)

# Copy shaders to output directory
add_custom_command(TARGET BandoriSandbox POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        $<TARGET_FILE_DIR:BandoriSandbox>/shaders
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/../../build/shaders
        $<TARGET_FILE_DIR:BandoriSandbox>/shaders
    COMMENT "Copying shaders to output directory"
)

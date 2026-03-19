cmake_minimum_required(VERSION 3.20)
project(MusicGameEngine CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Vulkan ───────────────────────────────────────────────────────────────────
find_package(Vulkan REQUIRED)

# ── Third-party paths ────────────────────────────────────────────────────────
set(TP ${CMAKE_SOURCE_DIR}/third_party)

set(GLFW_INCLUDE_DIR  "${TP}/glfw/include")
set(GLFW_LIBRARY      "${TP}/glfw/lib-vc2022/glfw3.lib")
set(GLM_INCLUDE_DIR   "${TP}/glm_extracted")
set(VMA_INCLUDE_DIR   "${TP}/vma")
set(STB_INCLUDE_DIR   "${TP}/stb")

# ── Sources ──────────────────────────────────────────────────────────────────
file(GLOB_RECURSE SOURCES
    src/core/*.cpp
    src/renderer/*.cpp
    src/renderer/vulkan/*.cpp
    src/game/modes/*.cpp
    src/game/chart/*.cpp
    src/engine/*.cpp
    src/main.cpp
)

add_executable(MusicGameEngine ${SOURCES})

target_include_directories(MusicGameEngine PRIVATE
    src
    ${Vulkan_INCLUDE_DIRS}
    ${GLFW_INCLUDE_DIR}
    ${GLM_INCLUDE_DIR}
    ${VMA_INCLUDE_DIR}
    ${STB_INCLUDE_DIR}
)

target_link_libraries(MusicGameEngine PRIVATE
    ${Vulkan_LIBRARIES}
    ${GLFW_LIBRARY}
)

# ── Windows defines ──────────────────────────────────────────────────────────
target_compile_definitions(MusicGameEngine PRIVATE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    VK_USE_PLATFORM_WIN32_KHR
    $<$<CONFIG:Debug>:ENABLE_VALIDATION_LAYERS>
)

# ── Shader compilation ───────────────────────────────────────────────────────
find_program(GLSLC glslc HINTS
    "$ENV{VULKAN_SDK}/Bin"
    "C:/VulkanSDK/1.4.335.0/Bin"
)

if(GLSLC)
    set(SHADER_DIR    ${CMAKE_SOURCE_DIR}/shaders)
    set(SHADER_OUT    ${CMAKE_BINARY_DIR}/shaders)
    file(MAKE_DIRECTORY ${SHADER_OUT})

    set(SHADER_SOURCES
        ${SHADER_DIR}/quad.vert      ${SHADER_DIR}/quad.frag
        ${SHADER_DIR}/line.vert      ${SHADER_DIR}/line.frag
        ${SHADER_DIR}/mesh.vert      ${SHADER_DIR}/mesh.frag
        ${SHADER_DIR}/composite.vert ${SHADER_DIR}/composite.frag
        ${SHADER_DIR}/bloom_downsample.comp
        ${SHADER_DIR}/bloom_upsample.comp
    )

    foreach(SHADER ${SHADER_SOURCES})
        get_filename_component(NAME ${SHADER} NAME)
        set(SPV ${SHADER_OUT}/${NAME}.spv)
        add_custom_command(OUTPUT ${SPV}
            COMMAND ${GLSLC} ${SHADER} -o ${SPV}
            DEPENDS ${SHADER}
            COMMENT "Compiling ${NAME}"
        )
        list(APPEND SPIRV_OUTPUTS ${SPV})
    endforeach()

    add_custom_target(Shaders ALL DEPENDS ${SPIRV_OUTPUTS})
    add_dependencies(MusicGameEngine Shaders)
else()
    message(WARNING "glslc not found — shaders will not be compiled automatically")
endif()

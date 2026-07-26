cmake_minimum_required(VERSION 3.16)
project(vulkan_pbr_demo LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 源文件
set(SOURCES
    src/main.cpp
    src/pbr_init.cpp
    src/pbr_swapchain.cpp
    src/pbr_render.cpp
    src/pbr_mesh_buffers.cpp
    src/pbr_descriptors.cpp
    src/pbr_commands.cpp
    src/pbr_sync.cpp
    src/pbr_shadow.cpp
    src/pbr_runtime.cpp
    src/pbr_cleanup.cpp
    src/vulkan_utils.cpp
    src/mesh.cpp
)

add_executable(vulkan_pbr ${SOURCES})

# Vulkan SDK
find_package(Vulkan REQUIRED)
target_include_directories(vulkan_pbr PRIVATE ${Vulkan_INCLUDE_DIRS})
target_link_libraries(vulkan_pbr PRIVATE Vulkan::Vulkan)

# GLFW (Homebrew on macOS)
find_package(glfw3 QUIET)
if(NOT glfw3_FOUND)
    # Fallback: try pkg-config
    find_package(PkgConfig QUIET)
    if(PKG_CONFIG_FOUND)
        pkg_check_modules(GLFW QUIET glfw3)
    endif()
    if(GLFW_FOUND)
        target_include_directories(vulkan_pbr PRIVATE ${GLFW_INCLUDE_DIRS})
        target_link_libraries(vulkan_pbr PRIVATE ${GLFW_LIBRARIES})
    else()
        message(FATAL_ERROR "GLFW not found. Install with: brew install glfw")
    endif()
else()
    target_link_libraries(vulkan_pbr PRIVATE glfw)
endif()

# macOS: 链接 OpenGL framework (MoltenVK 需要)
if(APPLE)
    find_library(OPENGL_FRAMEWORK OpenGL)
    find_library(METAL_FRAMEWORK Metal)
    find_library(QUARTZCORE_FRAMEWORK QuartzCore)
    target_link_libraries(vulkan_pbr PRIVATE
        ${OPENGL_FRAMEWORK}
        ${METAL_FRAMEWORK}
        ${QUARTZCORE_FRAMEWORK}
    )
endif()

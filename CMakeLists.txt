cmake_minimum_required(VERSION 3.16)
project(vk_bench LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Vulkan REQUIRED)
include(cmake/compile_shaders.cmake)

set(SHADER_DIR  "${CMAKE_CURRENT_SOURCE_DIR}/shaders")
set(SPIRV_DIR   "${CMAKE_CURRENT_BINARY_DIR}/shaders")
vk_compile_slang_shaders(SHADER_OUTPUTS vk-bench-shaders "${SHADER_DIR}" "${SPIRV_DIR}")
add_executable(vk-bench src/main.cpp)
target_include_directories(vk-bench PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(vk-bench PRIVATE Vulkan::Vulkan)

add_dependencies(vk-bench vk-bench-shaders)

install(TARGETS vk-bench RUNTIME DESTINATION bin)
install(FILES ${SHADER_OUTPUTS} DESTINATION bin/shaders)
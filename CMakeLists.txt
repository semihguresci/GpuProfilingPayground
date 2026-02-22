cmake_minimum_required(VERSION 3.16)
project(vk_bench LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Vulkan REQUIRED)
find_program(SLANGC slangc REQUIRED)

set(SHADER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/shaders)
set(SPIRV_DIR ${CMAKE_CURRENT_BINARY_DIR}/shaders)
file(MAKE_DIRECTORY ${SPIRV_DIR})

set(SHADER_OUTPUTS
  ${SPIRV_DIR}/triangle.vert.spv
  ${SPIRV_DIR}/triangle.frag.spv
)

add_custom_command(
  OUTPUT ${SPIRV_DIR}/triangle.vert.spv
  COMMAND ${SLANGC} -target spirv -profile glsl_460 -entry main -stage vertex ${SHADER_DIR}/triangle.vert.slang -o ${SPIRV_DIR}/triangle.vert.spv
  DEPENDS ${SHADER_DIR}/triangle.vert.slang
)

add_custom_command(
  OUTPUT ${SPIRV_DIR}/triangle.frag.spv
  COMMAND ${SLANGC} -target spirv -profile glsl_460 -entry main -stage fragment ${SHADER_DIR}/triangle.frag.slang -o ${SPIRV_DIR}/triangle.frag.spv
  DEPENDS ${SHADER_DIR}/triangle.frag.slang
)

add_custom_target(vk-bench-shaders DEPENDS ${SHADER_OUTPUTS})

add_executable(vk-bench src/main.cpp)
add_dependencies(vk-bench vk-bench-shaders)
target_include_directories(vk-bench PRIVATE include)
target_link_libraries(vk-bench PRIVATE Vulkan::Vulkan)
install(TARGETS vk-bench RUNTIME DESTINATION bin)
install(FILES ${SHADER_OUTPUTS} DESTINATION bin/shaders)

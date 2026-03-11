cmake_minimum_required(VERSION 3.16)
project(vk_bench LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Vulkan REQUIRED)
include(cmake/compile_shaders.cmake)

option(VK_BENCH_ENABLE_WINDOW "Enable window mode support (requires GLFW)" ON)
option(VK_BENCH_FETCH_GLFW
       "Download and build GLFW if it is not available on the system" ON)

set(VK_BENCH_HAS_WINDOW OFF)
set(VK_BENCH_GLFW_TARGET "")

if (VK_BENCH_ENABLE_WINDOW)
  find_package(glfw3 QUIET)

  if (glfw3_FOUND)
    set(VK_BENCH_HAS_WINDOW ON)
    set(VK_BENCH_GLFW_TARGET glfw)
    message(STATUS "Using system glfw3 package")
  elseif (VK_BENCH_FETCH_GLFW)
    include(FetchContent)
    FetchContent_Declare(
      glfw
      GIT_REPOSITORY https://github.com/glfw/glfw.git
      GIT_TAG 3.4
    )
    set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(glfw)
    set(VK_BENCH_HAS_WINDOW ON)
    set(VK_BENCH_GLFW_TARGET glfw)
    message(STATUS "Using fetched GLFW (3.4)")
  else()
    message(STATUS
            "glfw3 not found and VK_BENCH_FETCH_GLFW=OFF: windowed rendering disabled, use --headless")
  endif()
else()
  message(STATUS "VK_BENCH_ENABLE_WINDOW=OFF: windowed rendering disabled, use --headless")
endif()

set(SHADER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/shaders")
set(SPIRV_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders")
file(TO_CMAKE_PATH "${SPIRV_DIR}" SPIRV_DIR_CMAKE)
vk_compile_slang_shaders(SHADER_OUTPUTS vk-bench-shaders "${SHADER_DIR}" "${SPIRV_DIR}")

add_executable(vk-bench src/main.cpp)
target_include_directories(vk-bench PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_definitions(vk-bench PRIVATE VK_BENCH_SHADER_DIR="${SPIRV_DIR_CMAKE}")
target_link_libraries(vk-bench PRIVATE Vulkan::Vulkan)
if (VK_BENCH_HAS_WINDOW)
  target_link_libraries(vk-bench PRIVATE ${VK_BENCH_GLFW_TARGET})
  target_compile_definitions(vk-bench PRIVATE VK_BENCH_HAS_WINDOW=1)
else()
  target_compile_definitions(vk-bench PRIVATE VK_BENCH_HAS_WINDOW=0)
endif()

add_dependencies(vk-bench vk-bench-shaders)

install(TARGETS vk-bench RUNTIME DESTINATION bin)
install(FILES ${SHADER_OUTPUTS} DESTINATION bin/shaders)

cmake_minimum_required(VERSION 3.16)
project(vk_bench LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Vulkan REQUIRED)

add_executable(vk-bench src/main.cpp)
target_include_directories(vk-bench PRIVATE include)
target_link_libraries(vk-bench PRIVATE Vulkan::Vulkan)

install(TARGETS vk-bench RUNTIME DESTINATION bin)

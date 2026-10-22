cmake_minimum_required(VERSION 3.20)
project(tiny_jit_x64 VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(TJIT_SANITIZE "Build with AddressSanitizer + UndefinedBehaviorSanitizer" OFF)
if(TJIT_SANITIZE)
  add_compile_options(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
endif()

add_library(tjit src/parse.cpp src/tree.cpp src/emit.cpp src/exec_mem.cpp)
target_include_directories(tjit PUBLIC include)
target_compile_options(tjit PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Werror)

add_executable(tjit-cli tools/tjit.cpp)
set_target_properties(tjit-cli PROPERTIES OUTPUT_NAME tjit)
target_link_libraries(tjit-cli PRIVATE tjit)

enable_testing()
add_executable(test_tjit tests/test_tjit.cpp)
target_link_libraries(test_tjit PRIVATE tjit)
target_compile_options(test_tjit PRIVATE -Wall -Wextra -Werror)
add_test(NAME tjit COMMAND test_tjit)

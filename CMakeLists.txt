cmake_minimum_required(VERSION 3.5)
project(Kaleidoscope)

set(CMAKE_CXX_STANDARD 17)

# Set LLVM directory
set(LLVM_ROOT "/usr/local/opt/llvm")
set(LLVM_DIR "${LLVM_ROOT}/lib/cmake/llvm")

# Add LLVM
find_package(LLVM REQUIRED CONFIG)

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

# Add include and lib paths
include_directories(${LLVM_INCLUDE_DIRS})
link_directories(${LLVM_LIBRARY_DIRS})
add_definitions(${LLVM_DEFINITIONS})

add_executable(toy toy.cpp)
target_include_directories(toy PRIVATE ${LLVM_INCLUDE_DIRS})
target_link_libraries(toy PRIVATE LLVM)
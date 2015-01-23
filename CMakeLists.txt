cmake_minimum_required(VERSION 2.8)
project(llvm_passes C CXX)

if(UNIX)
    SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -std=c++11 -fdiagnostics-color -g -UNDEBUG")
endif()

# where cmake extension lies
set(CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)
include(Python)

# LLVM detection part
set(LLVM_ROOT "" CACHE PATH "Root of LLVM install.")

# A bit of a sanity check:
if( NOT EXISTS ${LLVM_ROOT}/include/llvm )
message(FATAL_ERROR "LLVM_ROOT (${LLVM_ROOT}) is not a valid LLVM install")
endif()


# Incorporate the CMake features provided by LLVM:
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${LLVM_ROOT}/share/llvm/cmake")

# only needed for multi target build?
set(LLVM_RUNTIME_OUTPUT_INTDIR ${CMAKE_CFG_INTDIR})
set(LLVM_LIBRARY_OUTPUT_INTDIR ${CMAKE_CFG_INTDIR})

# get regular includes
include(LLVMConfig)
include(HandleLLVMOptions)
include(AddLLVM)

# Now set the header and library paths:
include_directories(${LLVM_INCLUDE_DIRS})
link_directories(${LLVM_LIBRARY_DIRS})
add_definitions(${LLVM_DEFINITIONS})

# generate config-dependent files
configure_file("${CMAKE_SOURCE_DIR}/tests/lit.cfg.in" "${CMAKE_BINARY_DIR}/tests/lit.cfg")

# testing part relies on lit
find_python_module(lit REQUIRED)
add_custom_target(check COMMAND ${PYTHON_EXECUTABLE} -m lit.main ${CMAKE_BINARY_DIR}/tests)

# Finally load appropriate config from subdirs
add_subdirectory(llvm-passes)

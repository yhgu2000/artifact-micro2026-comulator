#
# 时间戳头文件
#
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/cmake/timestamp.cmake.in
               ${CMAKE_CURRENT_BINARY_DIR}/timestamp.cmake @ONLY)

add_custom_command(
  OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/timestamp.h
  COMMAND ${CMAKE_COMMAND} -P timestamp.cmake
  WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

add_library(timestamp INTERFACE ${CMAKE_CURRENT_BINARY_DIR}/timestamp.h)

target_include_directories(timestamp INTERFACE ${CMAKE_CURRENT_BINARY_DIR})

# ==============================================================================

add_subdirectory(util)
add_subdirectory(gyhcall)

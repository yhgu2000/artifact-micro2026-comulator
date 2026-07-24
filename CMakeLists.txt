#
# 项目配置
#
cmake_minimum_required(VERSION 3.28)

include(env.cmake OPTIONAL RESULT_VARIABLE _env_cmake_found)
if(NOT _env_cmake_found)
  include(environ.cmake)
endif()

# 这里应使用 CamelCase 以贴合 cmake 导出文件的命名风格
project(gyhcall VERSION 0.0.0.0)
include(CTest) # CTest 需要在顶层包含

# 项目自己的 CMake 模块包含路径
list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/lib/cmake)
include(util) # CMake 实用工具
include(code-coverage) # 覆盖率测试功能

# 语言标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED TRUE)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
enable_language(ASM)

# 项目信息头文件
configure_project_h(${PROJECT_BINARY_DIR}/include)
include_directories(${PROJECT_BINARY_DIR}/include)

#
# llvm
#
if(DEFINED LLVM_INSTALL_DIR)
  message("使用 LLVM 安装目录 ${LLVM_INSTALL_DIR}")
  set(LLVM_DIR "${LLVM_INSTALL_DIR}/lib/cmake/llvm")
else()
  message("使用 LLVM 的系统安装目录")
  set(LLVM_DIR "/usr/lib/llvm-18/cmake")
endif()
find_package(LLVM CONFIG REQUIRED)
message(
  "LLVM STATUS:
  Definitions ${LLVM_DEFINITIONS}
  Includes    ${LLVM_INCLUDE_DIRS}
  Libraries   ${LLVM_LIBRARY_DIRS}
  Targets     ${LLVM_TARGETS_TO_BUILD}")

function(target_use_llvm _target)
  target_include_directories(${_target} SYSTEM AFTER
                             PUBLIC ${LLVM_INCLUDE_DIRS})
  target_link_directories(${_target} PUBLIC ${LLVM_LIBRARY_DIRS})
  target_link_libraries(${_target} PUBLIC LLVM)
  target_compile_definitions(${_target} PUBLIC ${LLVM_DEFINITIONS})
  if(NOT LLVM_ENABLE_RTTI)
    target_compile_options(${_target} PUBLIC -fno-rtti)
  endif()
endfunction()

#
# 主体代码库
#
add_subdirectory(lib)

#
# 应用程序
#
add_subdirectory(app)

# 实验测试集
set(_path ${CMAKE_SOURCE_DIR}/_running/cases/build.ctests)
if(EXISTS ${_path})
  message("包含测试集: ${_path}")
  include(${_path})
else()
  message("测试集未找到: ${_path}")
endif()

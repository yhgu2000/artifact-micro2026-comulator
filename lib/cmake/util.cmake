# Python3
find_package(Python3 REQUIRED COMPONENTS Interpreter)

# ==============================================================================
# 将文件内容转换为 C/C++ 字符数组以直接在代码中包含.
#
# * _input[IN]: 输入文件路径, 相对路径基于 CMAKE_CURRENT_SOURCE_DIR
# * _output[IN]: 输出文件路径, 相对路径基于 CMAKE_CURRENT_BINARY_DIR
# ==============================================================================
function(file_to_chars _input _output)
  cmake_path(IS_RELATIVE _input _relative)
  if(_relative)
    set(_input ${CMAKE_CURRENT_SOURCE_DIR}/${_input})
  endif()
  cmake_path(IS_RELATIVE _output _relative)
  if(_relative)
    set(_output ${CMAKE_CURRENT_BINARY_DIR}/${_output})
  endif()

  add_custom_command(
    OUTPUT ${_output}
    COMMAND
      ${Python3_EXECUTABLE} ${CMAKE_CURRENT_FUNCTION_LIST_DIR}/file-to-chars.py
      ${_input} ${_output}
    DEPENDS ${_input} ${CMAKE_CURRENT_FUNCTION_LIST_DIR}/file-to-chars.py
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "file-to-chars ${_output}")
endfunction()

# ==============================================================================
# 配置项目信息头文件
#
# * _outdir[IN]: 输出目录, 相对路径基于 CMAKE_CURRENT_BINARY_DIR
# ==============================================================================
function(configure_project_h _outdir)
  cmake_path(IS_RELATIVE _outdir _relative)
  if(_relative)
    set(_outdir ${CMAKE_CURRENT_BINARY_DIR}/${_outdir})
  endif()

  configure_file(lib/cmake/project.h.in ${_outdir}/project.h)
endfunction()

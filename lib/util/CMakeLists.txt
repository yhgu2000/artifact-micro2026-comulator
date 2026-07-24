file(GLOB _src ./*.h ./*.c ./*.hpp ./*.cpp)

add_library(util ${_src})
target_use_llvm(util)
target_include_directories(util PUBLIC ..)

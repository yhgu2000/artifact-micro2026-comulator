file_to_chars(host.ll host.ll.chars)
file_to_chars(guest.ll guest.ll.chars)

file(GLOB _src ./*.h ./*.c ./*.hpp ./*.cpp)

add_library(gyhcall ${_src} ${CMAKE_CURRENT_BINARY_DIR}/host.ll.chars
                    ${CMAKE_CURRENT_BINARY_DIR}/guest.ll.chars)

target_use_llvm(gyhcall)
target_include_directories(gyhcall PUBLIC .. ${CMAKE_CURRENT_BINARY_DIR})

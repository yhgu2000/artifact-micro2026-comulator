# app 可执行目标名统一采用小写。
#
# 有些构建系统大小写不敏感，为了避免跨平台兼容性问题，不应与库目标重名。

# funcnum
add_executable(funcnum funcnum.cpp)
target_link_libraries(funcnum gyhcall)

# inspect
add_executable(inspect inspect.cpp)
target_link_libraries(inspect gyhcall)

# globhash
add_executable(globhash globhash.cpp)
target_link_libraries(globhash gyhcall)

# config
add_executable(config config.cpp)
target_link_libraries(config gyhcall)

# pick-up
add_executable(pick-up pick-up.cpp)
target_link_libraries(pick-up gyhcall)

# outline
add_executable(outline outline.cpp)
target_link_libraries(outline gyhcall)

# analyze
add_executable(analyze analyze.cpp)
target_link_libraries(analyze gyhcall)

# chop-off
add_executable(chop-off chop-off.cpp)
target_link_libraries(chop-off gyhcall)

# take-out
add_executable(take-out take-out.cpp)
target_link_libraries(take-out gyhcall)

# lto-plugin
add_library(lto-plugin SHARED lto-plugin.cpp)
target_link_libraries(lto-plugin gyhcall)
set_target_properties(lto-plugin PROPERTIES PREFIX "")

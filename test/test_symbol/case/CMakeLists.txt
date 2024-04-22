set(CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} -g -O0")
set(CMAKE_CXX_FLAGS_RELEASE "")
set(CMAKE_EXE_LINKER_FLAGS_RELEASE "")

file(GLOB_RECURSE SRC_LIST libsym_case.cpp)
# Loop through each source file
add_executable(libsym_case ${SRC_LIST})
target_link_libraries(libsym_case pthread)

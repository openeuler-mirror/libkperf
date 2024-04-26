file(GLOB source_files ./*.cpp)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -O0")
set(CMAKE_CXX_FLAGS_RELEASE "")
set(CMAKE_EXE_LINKER_FLAGS_RELEASE "")

foreach(source_file IN LISTS source_files)
    get_filename_component(target_name ${source_file} NAME_WE)

    add_executable("${target_name}" "${source_file}")
    target_link_libraries("${target_name}" pthread numa)
endforeach()

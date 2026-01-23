file(GLOB source_files ./*.cpp)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -O0")
set(CMAKE_CXX_FLAGS_RELEASE "")
set(CMAKE_EXE_LINKER_FLAGS_RELEASE "")

foreach(source_file IN LISTS source_files)
    get_filename_component(target_name ${source_file} NAME_WE)

    if (${target_name} STREQUAL "test_utrace_elf")
        add_library(${target_name} SHARED ${source_file})
        target_compile_options(${target_name} PRIVATE -O2 -g -fPIC)
    else()
        add_executable("${target_name}" "${source_file}")
        if (${target_name} STREQUAL "vectorized_loop")
            target_compile_options(${target_name} PRIVATE -g -O3)
        endif()

        target_link_libraries("${target_name}" pthread numa)
    endif()
endforeach()

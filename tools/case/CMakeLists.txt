file(GLOB EXAMPLE_SRCS "${CMAKE_CURRENT_LIST_DIR}/*.cpp")

set(CASE_BIN_DIR "${CMAKE_CURRENT_LIST_DIR}/../bin/case")
file(MAKE_DIRECTORY "${CASE_BIN_DIR}")

set(EXAMPLE_TARGETS "")
foreach(src ${EXAMPLE_SRCS})
  get_filename_component(name "${src}" NAME_WE)
  set(target "${name}")
  add_executable("${target}" "${src}")
  target_link_libraries("${target}" PRIVATE tools_common_deps)
  target_compile_options("${target}" PRIVATE -g)
  set_target_properties("${target}" PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CASE_BIN_DIR}"
  )
  list(APPEND EXAMPLE_TARGETS "${target}")
endforeach()

add_custom_target(examples_all DEPENDS ${EXAMPLE_TARGETS})

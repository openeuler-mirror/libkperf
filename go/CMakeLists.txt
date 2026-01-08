project(go)

set(TARGET_INCLUDE_DIR ${CMAKE_CURRENT_LIST_DIR}/src/libkperf/include)
set(TARGET_LIB_DIR ${CMAKE_CURRENT_LIST_DIR}/src/libkperf/lib)
set(TARGET_STATIC_LIB_DIR ${CMAKE_CURRENT_LIST_DIR}/src/libkperf/static_lib)
set(LLVM_LIB_DIR ${PROJECT_TOP_DIR}/llvm-symbolizer/build/lib)

file(MAKE_DIRECTORY ${TARGET_INCLUDE_DIR})
file(MAKE_DIRECTORY ${TARGET_LIB_DIR})
file(MAKE_DIRECTORY ${TARGET_STATIC_LIB_DIR})

file(GLOB HEAD_FILES "${CMAKE_CURRENT_LIST_DIR}/../include/*.h" "${CMAKE_CURRENT_LIST_DIR}/../symbol/symbol.h")
file(GLOB STATIC_ELF_FILES ${LLVM_LIB_DIR}/libLLVMDebugInfoDWARF.a
        ${LLVM_LIB_DIR}/libLLVMDemangle.a
        ${LLVM_LIB_DIR}/libLLVMObject.a
        ${LLVM_LIB_DIR}/libLLVMOption.a
        ${LLVM_LIB_DIR}/libLLVMSupport.a
        ${LLVM_LIB_DIR}/libLLVMSymbolize.a
        ${LLVM_LIB_DIR}/libLLVMDebugInfoDWARF.a
        ${LLVM_LIB_DIR}/libLLVMObject.a
        ${LLVM_LIB_DIR}/libLLVMBitReader.a
        ${LLVM_LIB_DIR}/libLLVMCore.a
        ${LLVM_LIB_DIR}/libLLVMMCParser.a
        ${LLVM_LIB_DIR}/libLLVMMC.a
        ${LLVM_LIB_DIR}/libLLVMDebugInfoCodeView.a
        ${LLVM_LIB_DIR}/libLLVMBinaryFormat.a
        ${LLVM_LIB_DIR}/libLLVMSupport.a
        ${LLVM_LIB_DIR}/libLLVMDemangle.a)

install(FILES ${HEAD_FILES} DESTINATION ${TARGET_INCLUDE_DIR})
install(TARGETS sym kperf DESTINATION ${TARGET_LIB_DIR})
install(TARGETS sym_static kperf_static DESTINATION ${TARGET_STATIC_LIB_DIR})
install(FILES ${STATIC_ELF_FILES} DESTINATION ${TARGET_STATIC_LIB_DIR})
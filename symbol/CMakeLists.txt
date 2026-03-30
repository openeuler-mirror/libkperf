if (POLICY CMP0048)
    cmake_policy(SET CMP0048 NEW)
endif (POLICY CMP0048)
project(libkprof)
cmake_minimum_required (VERSION 3.12.0)
set(CMAKE_CXX_STANDARD 11)
set(SYMBOL_FILE_DIR ${PROJECT_TOP_DIR}/symbol)
set(INCLUDE_DIR ${PROJECT_TOP_DIR}/include)
set(UTIL_FILE_DIR ${PROJECT_TOP_DIR}/util)
set(LLVM_LIB_DIR ${PROJECT_TOP_DIR}/llvm-symbolizer/build/lib)

file(GLOB SYMBOL_SRC ${SYMBOL_FILE_DIR}/*c ${SYMBOL_FILE_DIR}/*cpp ${UTIL_FILE_DIR}/pcerr.cpp ${UTIL_FILE_DIR}/common.cpp)

include_directories(${UTIL_FILE_DIR})
include_directories(${SYMBOL_FILE_DIR})
include_directories(${INCLUDE_DIR})

ADD_LIBRARY(sym SHARED ${SYMBOL_SRC})
ADD_LIBRARY(sym_static STATIC ${SYMBOL_SRC})
set_target_properties(sym_static PROPERTIES OUTPUT_NAME "sym")

target_link_libraries(sym
        pthread
        ${LLVM_LIB_DIR}/libLLVMTarget.a
        ${LLVM_LIB_DIR}/libLLVMDebugInfoDWARF.a
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
        ${LLVM_LIB_DIR}/libLLVMDemangle.a
        ${LLVM_LIB_DIR}/libLLVMMCDisassembler.a

        ${LLVM_LIB_DIR}/libLLVM${ARCH_TARGET}AsmPrinter.a
        ${LLVM_LIB_DIR}/libLLVM${ARCH_TARGET}AsmParser.a
        ${LLVM_LIB_DIR}/libLLVM${ARCH_TARGET}Desc.a
        ${LLVM_LIB_DIR}/libLLVM${ARCH_TARGET}Disassembler.a
        ${LLVM_LIB_DIR}/libLLVM${ARCH_TARGET}Info.a
        ${LLVM_LIB_DIR}/libLLVM${ARCH_TARGET}Utils.a
        z)

file(GLOB STATIC_LLVM_FILES 
        ${LLVM_LIB_DIR}/libLLVM${ARCH_TARGET}AsmPrinter.a
        ${LLVM_LIB_DIR}/libLLVM${ARCH_TARGET}AsmParser.a
        ${LLVM_LIB_DIR}/libLLVM${ARCH_TARGET}Desc.a
        ${LLVM_LIB_DIR}/libLLVM${ARCH_TARGET}Disassembler.a
        ${LLVM_LIB_DIR}/libLLVM${ARCH_TARGET}Info.a
        ${LLVM_LIB_DIR}/libLLVM${ARCH_TARGET}Utils.a

        ${LLVM_LIB_DIR}/libLLVMTarget.a
        ${LLVM_LIB_DIR}/libLLVMDebugInfoDWARF.a
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
        ${LLVM_LIB_DIR}/libLLVMDemangle.a
        ${LLVM_LIB_DIR}/libLLVMMCDisassembler.a)

list(APPEND CMAKE_PREFIX_PATH "${PROJECT_TOP_DIR}/third_party/local/capstone")
find_package(Capstone REQUIRED)
target_link_libraries(sym capstone::capstone_static)
target_link_libraries(sym_static capstone::capstone_static)

install(TARGETS sym DESTINATION ${CMAKE_INSTALL_PREFIX}/lib)
install(TARGETS sym_static DESTINATION ${CMAKE_INSTALL_PREFIX}/lib)
install(FILES ${SYMBOL_FILE_DIR}/symbol.h DESTINATION ${CMAKE_INSTALL_PREFIX}/include)
install(FILES ${STATIC_LLVM_FILES} DESTINATION  ${CMAKE_INSTALL_PREFIX}/lib)
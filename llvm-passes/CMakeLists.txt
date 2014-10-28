set(EPONA_LLVM_MODULES
    ObfuscateZero
)

# automatically create modules based on EPONA_LLVM_MODULES and directory content
foreach(MODULE ${EPONA_LLVM_MODULES})
    aux_source_directory(${CMAKE_SOURCE_DIR}/llvm-passes/${MODULE} ${MODULE}_SRC)
    add_llvm_loadable_module(LLVM${MODULE} ${${MODULE}_SRC})
endforeach()


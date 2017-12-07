add_library(dynarmic
    # Source files
    common/memory_pool.cpp
    frontend/arm/types.cpp
    frontend/disassembler/disassembler_arm.cpp
    frontend/disassembler/disassembler_thumb.cpp
    frontend/ir/basic_block.cpp
    frontend/ir/ir_emitter.cpp
    frontend/ir/location_descriptor.cpp
    frontend/ir/microinstruction.cpp
    frontend/ir/opcodes.cpp
    frontend/ir/value.cpp
    frontend/translate/translate.cpp
    frontend/translate/translate_arm.cpp
    frontend/translate/translate_arm/branch.cpp
    frontend/translate/translate_arm/coprocessor.cpp
    frontend/translate/translate_arm/data_processing.cpp
    frontend/translate/translate_arm/exception_generating.cpp
    frontend/translate/translate_arm/extension.cpp
    frontend/translate/translate_arm/load_store.cpp
    frontend/translate/translate_arm/misc.cpp
    frontend/translate/translate_arm/multiply.cpp
    frontend/translate/translate_arm/packing.cpp
    frontend/translate/translate_arm/parallel.cpp
    frontend/translate/translate_arm/reversal.cpp
    frontend/translate/translate_arm/saturated.cpp
    frontend/translate/translate_arm/status_register_access.cpp
    frontend/translate/translate_arm/synchronization.cpp
    frontend/translate/translate_arm/vfp2.cpp
    frontend/translate/translate_thumb.cpp
    ir_opt/constant_propagation_pass.cpp
    ir_opt/dead_code_elimination_pass.cpp
    ir_opt/get_set_elimination_pass.cpp
    ir_opt/verification_pass.cpp

    # Header files
    ../include/dynarmic/callbacks.h
    ../include/dynarmic/coprocessor.h
    ../include/dynarmic/coprocessor_util.h
    ../include/dynarmic/disassembler.h
    ../include/dynarmic/dynarmic.h
    common/address_range.h
    common/assert.h
    common/bit_util.h
    common/common_types.h
    common/intrusive_list.h
    common/iterator_util.h
    common/memory_pool.h
    common/mp.h
    common/scope_exit.h
    common/string_util.h
    common/variant_util.h
    frontend/arm/FPSCR.h
    frontend/arm/PSR.h
    frontend/arm/types.h
    frontend/decoder/arm.h
    frontend/decoder/decoder_detail.h
    frontend/decoder/matcher.h
    frontend/decoder/thumb16.h
    frontend/decoder/thumb32.h
    frontend/decoder/vfp2.h
    frontend/disassembler/disassembler.h
    frontend/ir/basic_block.h
    frontend/ir/ir_emitter.h
    frontend/ir/location_descriptor.h
    frontend/ir/microinstruction.h
    frontend/ir/opcodes.h
    frontend/ir/terminal.h
    frontend/ir/value.h
    frontend/translate/translate.h
    frontend/translate/translate_arm/translate_arm.h
    ir_opt/passes.h
)

if (ARCHITECTURE_x86_64)
    target_sources(dynarmic PRIVATE
         # Source files
         backend_x64/abi.cpp
         backend_x64/block_of_code.cpp
         backend_x64/constant_pool.cpp
         backend_x64/emit_x64.cpp
         backend_x64/hostloc.cpp
         backend_x64/interface_x64.cpp
         backend_x64/jitstate.cpp
         backend_x64/reg_alloc.cpp

         # Headers
         backend_x64/abi.h
         backend_x64/block_of_code.h
         backend_x64/constant_pool.h
         backend_x64/emit_x64.h
         backend_x64/hostloc.h
         backend_x64/jitstate.h
         backend_x64/oparg.h
         backend_x64/reg_alloc.h
    )

    if (WIN32)
        target_sources(dynarmic PRIVATE backend_x64/exception_handler_windows.cpp)
    else()
        target_sources(dynarmic PRIVATE backend_x64/exception_handler_generic.cpp)
    endif()
else()
    message(FATAL_ERROR "Unsupported architecture")
endif()

include(CreateDirectoryGroups)
create_target_directory_groups(dynarmic)

target_include_directories(dynarmic
                           PUBLIC ../include
                           PRIVATE .)
target_compile_options(dynarmic PRIVATE ${DYNARMIC_CXX_FLAGS})
target_link_libraries(dynarmic
    PUBLIC
        boost
    PRIVATE
        fmt::fmt
        xbyak
        $<$<BOOL:DYNARMIC_USE_LLVM>:${llvm_libs}>
)
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_compile_definitions(dynarmic PRIVATE FMT_USE_WINDOWS_H=0)
endif()

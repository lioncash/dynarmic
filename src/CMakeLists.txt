add_library(dynarmic
    ../include/dynarmic/A32/a32.h
    ../include/dynarmic/A32/callbacks.h
    ../include/dynarmic/A32/coprocessor.h
    ../include/dynarmic/A32/coprocessor_util.h
    ../include/dynarmic/A32/disassembler.h
    common/address_range.h
    common/assert.h
    common/bit_util.h
    common/common_types.h
    common/intrusive_list.h
    common/iterator_util.h
    common/memory_pool.cpp
    common/memory_pool.h
    common/mp.h
    common/scope_exit.h
    common/string_util.h
    common/variant_util.h
    frontend/A32/decoder/arm.h
    frontend/A32/decoder/thumb16.h
    frontend/A32/decoder/thumb32.h
    frontend/A32/decoder/vfp2.h
    frontend/A32/disassembler/disassembler.h
    frontend/A32/disassembler/disassembler_arm.cpp
    frontend/A32/disassembler/disassembler_thumb.cpp
    frontend/A32/FPSCR.h
    frontend/A32/ir_emitter.cpp
    frontend/A32/ir_emitter.h
    frontend/A32/location_descriptor.cpp
    frontend/A32/location_descriptor.h
    frontend/A32/PSR.h
    frontend/A32/translate/translate.cpp
    frontend/A32/translate/translate.h
    frontend/A32/translate/translate_arm.cpp
    frontend/A32/translate/translate_arm/branch.cpp
    frontend/A32/translate/translate_arm/coprocessor.cpp
    frontend/A32/translate/translate_arm/data_processing.cpp
    frontend/A32/translate/translate_arm/exception_generating.cpp
    frontend/A32/translate/translate_arm/extension.cpp
    frontend/A32/translate/translate_arm/load_store.cpp
    frontend/A32/translate/translate_arm/misc.cpp
    frontend/A32/translate/translate_arm/multiply.cpp
    frontend/A32/translate/translate_arm/packing.cpp
    frontend/A32/translate/translate_arm/parallel.cpp
    frontend/A32/translate/translate_arm/reversal.cpp
    frontend/A32/translate/translate_arm/saturated.cpp
    frontend/A32/translate/translate_arm/status_register_access.cpp
    frontend/A32/translate/translate_arm/synchronization.cpp
    frontend/A32/translate/translate_arm/translate_arm.h
    frontend/A32/translate/translate_arm/vfp2.cpp
    frontend/A32/translate/translate_thumb.cpp
    frontend/A32/types.cpp
    frontend/A32/types.h
    frontend/A64/decoder/a64.h
    frontend/A64/imm.h
    frontend/A64/location_descriptor.cpp
    frontend/A64/location_descriptor.h
    frontend/A64/translate/impl/impl.h
    frontend/A64/translate/translate.cpp
    frontend/A64/translate/translate.h
    frontend/A64/types.cpp
    frontend/A64/types.h
    frontend/decoder/decoder_detail.h
    frontend/decoder/matcher.h
    frontend/ir/basic_block.cpp
    frontend/ir/basic_block.h
    frontend/ir/ir_emitter.cpp
    frontend/ir/ir_emitter.h
    frontend/ir/location_descriptor.cpp
    frontend/ir/location_descriptor.h
    frontend/ir/microinstruction.cpp
    frontend/ir/microinstruction.h
    frontend/ir/opcodes.cpp
    frontend/ir/opcodes.h
    frontend/ir/terminal.h
    frontend/ir/value.cpp
    frontend/ir/value.h
    ir_opt/constant_propagation_pass.cpp
    ir_opt/dead_code_elimination_pass.cpp
    ir_opt/get_set_elimination_pass.cpp
    ir_opt/passes.h
    ir_opt/verification_pass.cpp
)

if (ARCHITECTURE_x86_64)
    target_sources(dynarmic PRIVATE
         backend_x64/a32_emit_x64.cpp
         backend_x64/a32_emit_x64.h
         backend_x64/a32_interface.cpp
         backend_x64/a32_jitstate.cpp
         backend_x64/a32_jitstate.h
         backend_x64/a64_emit_x64.cpp
         backend_x64/a64_emit_x64.h
         backend_x64/a64_interface.cpp
         backend_x64/a64_jitstate.cpp
         backend_x64/a64_jitstate.h
         backend_x64/abi.cpp
         backend_x64/abi.h
         backend_x64/block_of_code.cpp
         backend_x64/block_of_code.h
         backend_x64/callback.cpp
         backend_x64/callback.h
         backend_x64/constant_pool.cpp
         backend_x64/constant_pool.h
         backend_x64/emit_x64.cpp
         backend_x64/emit_x64.h
         backend_x64/hostloc.cpp
         backend_x64/hostloc.h
         backend_x64/jitstate_info.h
         backend_x64/oparg.h
         backend_x64/reg_alloc.cpp
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

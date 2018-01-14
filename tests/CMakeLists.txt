add_executable(dynarmic_tests
    A32/fuzz_arm.cpp
    A32/fuzz_thumb.cpp
    A32/skyeye_interpreter/dyncom/arm_dyncom_dec.cpp
    A32/skyeye_interpreter/dyncom/arm_dyncom_dec.h
    A32/skyeye_interpreter/dyncom/arm_dyncom_interpreter.cpp
    A32/skyeye_interpreter/dyncom/arm_dyncom_interpreter.h
    A32/skyeye_interpreter/dyncom/arm_dyncom_run.h
    A32/skyeye_interpreter/dyncom/arm_dyncom_thumb.cpp
    A32/skyeye_interpreter/dyncom/arm_dyncom_thumb.h
    A32/skyeye_interpreter/dyncom/arm_dyncom_trans.cpp
    A32/skyeye_interpreter/dyncom/arm_dyncom_trans.h
    A32/skyeye_interpreter/skyeye_common/arm_regformat.h
    A32/skyeye_interpreter/skyeye_common/armstate.cpp
    A32/skyeye_interpreter/skyeye_common/armstate.h
    A32/skyeye_interpreter/skyeye_common/armsupp.cpp
    A32/skyeye_interpreter/skyeye_common/armsupp.h
    A32/skyeye_interpreter/skyeye_common/vfp/asm_vfp.h
    A32/skyeye_interpreter/skyeye_common/vfp/vfp.cpp
    A32/skyeye_interpreter/skyeye_common/vfp/vfp.h
    A32/skyeye_interpreter/skyeye_common/vfp/vfp_helper.h
    A32/skyeye_interpreter/skyeye_common/vfp/vfpdouble.cpp
    A32/skyeye_interpreter/skyeye_common/vfp/vfpinstr.cpp
    A32/skyeye_interpreter/skyeye_common/vfp/vfpsingle.cpp
    A32/test_arm_disassembler.cpp
    A32/test_thumb_instructions.cpp
    A64/a64.cpp
    A64/inst_gen.cpp
    A64/inst_gen.h
    A64/testenv.h
    main.cpp
    rand_int.h
)

if (DYNARMIC_TESTS_USE_UNICORN)
    target_sources(dynarmic_tests PRIVATE
        A64/fuzz_with_unicorn.cpp
        A64/unicorn_emu/unicorn.cpp
        A64/unicorn_emu/unicorn.h
        A64/unicorn_emu/unicorn_load.cpp
    )
    target_link_libraries(dynarmic_tests PRIVATE Unicorn)
endif()

include(CreateDirectoryGroups)
create_target_directory_groups(dynarmic_tests)

target_link_libraries(dynarmic_tests PRIVATE dynarmic boost catch)
target_include_directories(dynarmic_tests PRIVATE . ../src)
target_compile_options(dynarmic_tests PRIVATE ${DYNARMIC_CXX_FLAGS})

add_test(dynarmic_tests dynarmic_tests)

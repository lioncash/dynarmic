add_executable(dynarmic_tests
    # Source files
    arm/fuzz_arm.cpp
    arm/fuzz_thumb.cpp
    arm/test_arm_disassembler.cpp
    arm/test_thumb_instructions.cpp
    main.cpp
    skyeye_interpreter/dyncom/arm_dyncom_dec.cpp
    skyeye_interpreter/dyncom/arm_dyncom_interpreter.cpp
    skyeye_interpreter/dyncom/arm_dyncom_thumb.cpp
    skyeye_interpreter/dyncom/arm_dyncom_trans.cpp
    skyeye_interpreter/skyeye_common/armstate.cpp
    skyeye_interpreter/skyeye_common/armsupp.cpp
    skyeye_interpreter/skyeye_common/vfp/vfp.cpp
    skyeye_interpreter/skyeye_common/vfp/vfpdouble.cpp
    skyeye_interpreter/skyeye_common/vfp/vfpinstr.cpp
    skyeye_interpreter/skyeye_common/vfp/vfpsingle.cpp

    # Header files
    rand_int.h
    skyeye_interpreter/dyncom/arm_dyncom_dec.h
    skyeye_interpreter/dyncom/arm_dyncom_interpreter.h
    skyeye_interpreter/dyncom/arm_dyncom_run.h
    skyeye_interpreter/dyncom/arm_dyncom_thumb.h
    skyeye_interpreter/dyncom/arm_dyncom_trans.h
    skyeye_interpreter/skyeye_common/arm_regformat.h
    skyeye_interpreter/skyeye_common/armstate.h
    skyeye_interpreter/skyeye_common/armsupp.h
    skyeye_interpreter/skyeye_common/vfp/asm_vfp.h
    skyeye_interpreter/skyeye_common/vfp/vfp.h
    skyeye_interpreter/skyeye_common/vfp/vfp_helper.h
)

include(CreateDirectoryGroups)
create_target_directory_groups(dynarmic_tests)

target_link_libraries(dynarmic_tests PRIVATE dynarmic boost catch)
target_include_directories(dynarmic_tests PRIVATE . ../src)
target_compile_options(dynarmic_tests PRIVATE ${DYNARMIC_CXX_FLAGS})

add_test(dynarmic_tests dynarmic_tests)

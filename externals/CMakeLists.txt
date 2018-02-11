# For libraries that already come with a CMakeLists file,
# simply add the directory to that file as a subdirectory
# to have CMake automatically recognize them.

if (NOT DYNARMIC_NO_BUNDLED_FMT)
    # fmtlib formatting library
    add_subdirectory(fmt)
endif()

if (NOT TARGET xbyak)
    if (ARCHITECTURE_x86 OR ARCHITECTURE_x86_64)
        add_library(xbyak INTERFACE)
        target_include_directories(xbyak SYSTEM INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/xbyak/xbyak)
        target_compile_definitions(xbyak INTERFACE XBYAK_NO_OP_NAMES)
    endif()
endif()

add_library(catch INTERFACE)
target_include_directories(catch INTERFACE
                           $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/catch>)

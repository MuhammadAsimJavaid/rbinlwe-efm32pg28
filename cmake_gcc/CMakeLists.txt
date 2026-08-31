# Define minimal required version of CMake.
cmake_minimum_required(VERSION "3.25")

# Project definition
project(
	RBLWE_on_EFM32_ShAd_CBD0
	VERSION 1.0
	LANGUAGES C CXX ASM
)

set(RLWE_PARAMETER_SET "3" CACHE STRING
    "RLWE parameter set: 1=N256/Q256, 2=N256/Q128, 3=N512/Q256")
set_property(CACHE RLWE_PARAMETER_SET PROPERTY STRINGS 1 2 3)
if(NOT RLWE_PARAMETER_SET MATCHES "^[123]$")
    message(FATAL_ERROR "RLWE_PARAMETER_SET must be 1, 2, or 3")
endif()

set(RLWE_ERROR_DISTRIBUTION "0" CACHE STRING
    "Error distribution: 0=paper Bernoulli CBD0, 1=centered CBD1")
set_property(CACHE RLWE_ERROR_DISTRIBUTION PROPERTY STRINGS 0 1)
if(NOT RLWE_ERROR_DISTRIBUTION MATCHES "^[01]$")
    message(FATAL_ERROR "RLWE_ERROR_DISTRIBUTION must be 0 or 1")
endif()

set(RLWE_BRANCH_FREE_MUL_BINARY "1" CACHE STRING
    "Use branch-free binary polynomial multiplication (0 or 1)")
set(RLWE_PACKED_ARITHMETIC "1" CACHE STRING
    "Use packed UADD8/USUB8 binary polynomial arithmetic (0 or 1)")
set(RLWE_PACKED_CODEC "1" CACHE STRING
    "Use packed message encoding and decoding (0 or 1)")
set(RLWE_BUFFERED_TRNG "1" CACHE STRING
    "Pre-buffer Secure Element TRNG output outside measured operations (0 or 1)")
set(RLWE_COMBINED_BENCHMARK "0" CACHE STRING
    "Run target 10 with normalized combined-campaign modes (0 or 1)")
set(RLWE_FULL_MASKED_DECRYPT_ENABLE "1" CACHE STRING
    "Use first-order fully masked decryption and shared secret keys (0 or 1)")
set(RLWE_CCA2_PKE_ENABLE "1" CACHE STRING
    "Enable the CCA2 PKE wrapper (0 or 1)")
set(RLWE_CCA2_PKE_SE_ORACLES_ENABLE "1" CACHE STRING
    "Use Secure Element SHA-256 oracles (0 or 1)")
set(FI_TEST_ENABLE "0" CACHE STRING
    "Build the debugger-assisted fault-injection harness (0 or 1)")
set(TIMING_TEST_TARGET "5" CACHE STRING
    "Target: 1=encrypt mul, 2=decrypt mul, 3=CCA2 encrypt, 4=CCA2 decrypt, 5=selectable CPA/CCA2 round-trip benchmark, 6=masked component cycle probe, 7=one-coefficient multiplication TVLA, 8=one-coefficient decoding TVLA, 9=PD12 pulse diagnostic, 10=buffered/TRNG-only/cold operation cycles")
set_property(CACHE TIMING_TEST_TARGET PROPERTY STRINGS 1 2 3 4 5 6 7 8 9 10)
if(NOT TIMING_TEST_TARGET MATCHES "^(10|[1-9])$")
    message(FATAL_ERROR "TIMING_TEST_TARGET must be in the range 1 to 10")
endif()

set(TIMING_TEST_TRIALS "100" CACHE STRING
    "Number of constant-time measurement trials")
if(NOT TIMING_TEST_TRIALS MATCHES "^[1-9][0-9]*$")
    message(FATAL_ERROR "TIMING_TEST_TRIALS must be a positive integer")
endif()

set(EFFECTIVE_TIMING_TEST_TRIALS "${TIMING_TEST_TRIALS}")
if(TIMING_TEST_TARGET MATCHES "^[78]$")
    if(RLWE_FULL_MASKED_DECRYPT_ENABLE STREQUAL "1")
        set(EFFECTIVE_TIMING_TEST_TRIALS "100000")
    else()
        set(EFFECTIVE_TIMING_TEST_TRIALS "10000")
    endif()
    message(STATUS "Component TVLA trace count: ${EFFECTIVE_TIMING_TEST_TRIALS}")
endif()

foreach(toggle
        RLWE_BRANCH_FREE_MUL_BINARY
        RLWE_PACKED_ARITHMETIC
        RLWE_PACKED_CODEC
        RLWE_BUFFERED_TRNG
        RLWE_COMBINED_BENCHMARK
        RLWE_FULL_MASKED_DECRYPT_ENABLE
        RLWE_CCA2_PKE_ENABLE
        RLWE_CCA2_PKE_SE_ORACLES_ENABLE
        FI_TEST_ENABLE)
    set_property(CACHE ${toggle} PROPERTY STRINGS 0 1)
    if(NOT ${toggle} MATCHES "^[01]$")
        message(FATAL_ERROR "${toggle} must be 0 or 1")
    endif()
endforeach()

# Include the definition of the slc_RBLWE_on_EFM32_ShAd_CBD0 target,
# which contains the content of the SLC project
include(RBLWE_on_EFM32_ShAd_CBD0.cmake)

# Keep generated application sources and handwritten crypto sources on the
# same wire-format and object-layout parameter set.
target_compile_definitions(slc PUBLIC
    RLWE_PARAMETER_SET=${RLWE_PARAMETER_SET}
    RLWE_ERROR_DISTRIBUTION=${RLWE_ERROR_DISTRIBUTION}
    RLWE_BRANCH_FREE_MUL_BINARY=${RLWE_BRANCH_FREE_MUL_BINARY}
    RLWE_FULL_MASKED_DECRYPT_ENABLE=${RLWE_FULL_MASKED_DECRYPT_ENABLE}
    RLWE_CCA2_PKE_ENABLE=${RLWE_CCA2_PKE_ENABLE}
    RLWE_CCA2_PKE_SE_ORACLES_ENABLE=${RLWE_CCA2_PKE_SE_ORACLES_ENABLE}
    FI_TEST_ENABLE=${FI_TEST_ENABLE}
)

add_executable(RBLWE_on_EFM32_ShAd_CBD0
    # Add additional sources here
    ../keygen.c
    ../encrypt.c
    ../decrypt.c
    ../ntt.c
    ../sampling.c
    ../rlwe_cca2_pke.c
    ../masked_gadgets.S
    ../fault_injection_test.c
)

target_include_directories(RBLWE_on_EFM32_ShAd_CBD0 PUBLIC
    # Add additional include paths here
    ..
)

target_compile_definitions(RBLWE_on_EFM32_ShAd_CBD0 PUBLIC
    # Add additional macros here
    RLWE_PARAMETER_SET=${RLWE_PARAMETER_SET}
    RLWE_ERROR_DISTRIBUTION=${RLWE_ERROR_DISTRIBUTION}
    RLWE_BRANCH_FREE_MUL_BINARY=${RLWE_BRANCH_FREE_MUL_BINARY}
    RLWE_PACKED_ARITHMETIC=${RLWE_PACKED_ARITHMETIC}
    RLWE_PACKED_CODEC=${RLWE_PACKED_CODEC}
    RLWE_BUFFERED_TRNG=${RLWE_BUFFERED_TRNG}
    RLWE_COMBINED_BENCHMARK=${RLWE_COMBINED_BENCHMARK}
    RLWE_FULL_MASKED_DECRYPT_ENABLE=${RLWE_FULL_MASKED_DECRYPT_ENABLE}
    RLWE_CCA2_PKE_ENABLE=${RLWE_CCA2_PKE_ENABLE}
    RLWE_CCA2_PKE_SE_ORACLES_ENABLE=${RLWE_CCA2_PKE_SE_ORACLES_ENABLE}
    FI_TEST_ENABLE=${FI_TEST_ENABLE}
    TIMING_TEST_TARGET=${TIMING_TEST_TARGET}
    TIMING_TEST_TRIALS=${EFFECTIVE_TIMING_TEST_TRIALS}
)

target_compile_options(RBLWE_on_EFM32_ShAd_CBD0 PUBLIC
    # Set additional compiler flags here
)

target_link_options(RBLWE_on_EFM32_ShAd_CBD0 PUBLIC
    # Set additional linker flags here
)

# Link with the content defined in the SLC project
target_link_libraries(RBLWE_on_EFM32_ShAd_CBD0 PRIVATE
    slc
)

# Include managed project content if available
include(RBLWE_on_EFM32_ShAd_CBD0_project.cmake OPTIONAL RESULT_VARIABLE managed_project)
if(managed_project)
    message(STATUS "Using managed project content from ${managed_project}")
endif()

# Force the gcc linker command
set_target_properties(RBLWE_on_EFM32_ShAd_CBD0 PROPERTIES LINKER_LANGUAGE C)

# Create .bin, .hex and .s37 artifacts after building the project
add_custom_command(TARGET RBLWE_on_EFM32_ShAd_CBD0
    POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} ${OBJCOPY_SREC_CMD} "$<TARGET_FILE:RBLWE_on_EFM32_ShAd_CBD0>" "$<TARGET_FILE_DIR:RBLWE_on_EFM32_ShAd_CBD0>/$<TARGET_FILE_BASE_NAME:RBLWE_on_EFM32_ShAd_CBD0>.s37"
    COMMAND ${CMAKE_OBJCOPY} ${OBJCOPY_IHEX_CMD} "$<TARGET_FILE:RBLWE_on_EFM32_ShAd_CBD0>" "$<TARGET_FILE_DIR:RBLWE_on_EFM32_ShAd_CBD0>/$<TARGET_FILE_BASE_NAME:RBLWE_on_EFM32_ShAd_CBD0>.hex"
    COMMAND ${CMAKE_OBJCOPY} ${OBJCOPY_BIN_CMD}  "$<TARGET_FILE:RBLWE_on_EFM32_ShAd_CBD0>" "$<TARGET_FILE_DIR:RBLWE_on_EFM32_ShAd_CBD0>/$<TARGET_FILE_BASE_NAME:RBLWE_on_EFM32_ShAd_CBD0>.bin" 
)

# Run post-build pipeline to perform additional post-processing
if(post_build_command)
add_custom_command(TARGET RBLWE_on_EFM32_ShAd_CBD0
    POST_BUILD
    WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/..
    COMMAND ${post_build_command}
)
endif()

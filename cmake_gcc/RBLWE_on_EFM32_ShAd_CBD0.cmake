####################################################################
# Automatically-generated file. Do not edit!                       #
####################################################################

set(SIMPLICITY_SDK_DIR "" CACHE PATH
    "Path to the Silicon Labs Simplicity SDK 2025.12.2 root")
if(SIMPLICITY_SDK_DIR)
    set(SDK_PATH "${SIMPLICITY_SDK_DIR}")
    cmake_path(NORMAL_PATH SDK_PATH)
elseif(DEFINED ENV{SIMPLICITY_SDK_DIR})
    set(SDK_PATH "$ENV{SIMPLICITY_SDK_DIR}")
    cmake_path(NORMAL_PATH SDK_PATH)
else()
    message(FATAL_ERROR
        "Set SIMPLICITY_SDK_DIR (cache variable or environment variable) "
        "to the Simplicity SDK 2025.12.2 root")
endif()

set(COPIED_SDK_PATH "simplicity_sdk_2025.12.2")
set(PKG_PATH "")

add_library(slc OBJECT
    "${SDK_PATH}/boards/hardware/board/src/sl_board_control_gpio.c"
    "${SDK_PATH}/boards/hardware/board/src/sl_board_init.c"
    "${SDK_PATH}/compute/driver/mvp/src/sl_mvp.c"
    "${SDK_PATH}/compute/driver/mvp/src/sl_mvp_hal_efr32.c"
    "${SDK_PATH}/compute/driver/mvp/src/sl_mvp_program_area.c"
    "${SDK_PATH}/compute/driver/mvp/src/sl_mvp_util.c"
    "${SDK_PATH}/compute/math/mvp/src/sl_math_mvp_complex_matrix_mult.c"
    "${SDK_PATH}/compute/math/mvp/src/sl_math_mvp_complex_vector_conjugate.c"
    "${SDK_PATH}/compute/math/mvp/src/sl_math_mvp_complex_vector_dot_product.c"
    "${SDK_PATH}/compute/math/mvp/src/sl_math_mvp_complex_vector_magnitude_squared.c"
    "${SDK_PATH}/compute/math/mvp/src/sl_math_mvp_complex_vector_mult.c"
    "${SDK_PATH}/compute/math/mvp/src/sl_math_mvp_matrix_add.c"
    "${SDK_PATH}/compute/math/mvp/src/sl_math_mvp_matrix_mult.c"
    "${SDK_PATH}/compute/math/mvp/src/sl_math_mvp_matrix_scale.c"
    "${SDK_PATH}/compute/math/mvp/src/sl_math_mvp_matrix_sub.c"
    "${SDK_PATH}/compute/math/mvp/src/sl_math_mvp_matrix_transpose.c"
    "${SDK_PATH}/compute/math/mvp/src/sl_math_mvp_matrix_vector_mult.c"
    "${SDK_PATH}/compute/math/mvp/src/sl_math_mvp_util.c"
    "${SDK_PATH}/compute/math/mvp/src/sl_math_mvp_vector_abs.c"
    "${SDK_PATH}/compute/math/mvp/src/sl_math_mvp_vector_add.c"
    "${SDK_PATH}/compute/math/mvp/src/sl_math_mvp_vector_clamp.c"
    "${SDK_PATH}/compute/math/mvp/src/sl_math_mvp_vector_clip.c"
    "${SDK_PATH}/compute/math/mvp/src/sl_math_mvp_vector_copy.c"
    "${SDK_PATH}/compute/math/mvp/src/sl_math_mvp_vector_dot_product.c"
    "${SDK_PATH}/compute/math/mvp/src/sl_math_mvp_vector_fill.c"
    "${SDK_PATH}/compute/math/mvp/src/sl_math_mvp_vector_mult.c"
    "${SDK_PATH}/compute/math/mvp/src/sl_math_mvp_vector_negate.c"
    "${SDK_PATH}/compute/math/mvp/src/sl_math_mvp_vector_offset.c"
    "${SDK_PATH}/compute/math/mvp/src/sl_math_mvp_vector_scale.c"
    "${SDK_PATH}/compute/math/mvp/src/sl_math_mvp_vector_sub.c"
    "${SDK_PATH}/compute/math/src/sl_math_matrix.c"
    "${SDK_PATH}/devices/platform/Device/SiliconLabs/EFM32PG28/Source/startup_efm32pg28.c"
    "${SDK_PATH}/devices/platform/Device/SiliconLabs/EFM32PG28/Source/system_efm32pg28.c"
    "${SDK_PATH}/platform_common/platform/common/src/sl_assert.c"
    "${SDK_PATH}/platform_common/platform/common/src/sl_slist.c"
    "${SDK_PATH}/platform_common/platform/common/src/sl_string.c"
    "${SDK_PATH}/platform_common/platform/common/src/sl_syscalls.c"
    "${SDK_PATH}/platform_core/hardware/driver/configuration_over_swo/src/sl_cos.c"
    "${SDK_PATH}/platform_core/platform/common/src/sl_core_cortexm.c"
    "${SDK_PATH}/platform_core/platform/driver/cycle_counter/src/sl_cycle_counter.c"
    "${SDK_PATH}/platform_core/platform/driver/debug/src/sl_debug_swo.c"
    "${SDK_PATH}/platform_core/platform/driver/gpio/src/sl_gpio.c"
    "${SDK_PATH}/platform_core/platform/emdrv/dmadrv/src/dmadrv.c"
    "${SDK_PATH}/platform_core/platform/emlib/src/em_burtc.c"
    "${SDK_PATH}/platform_core/platform/emlib/src/em_cmu.c"
    "${SDK_PATH}/platform_core/platform/emlib/src/em_emu.c"
    "${SDK_PATH}/platform_core/platform/emlib/src/em_gpio.c"
    "${SDK_PATH}/platform_core/platform/emlib/src/em_ldma.c"
    "${SDK_PATH}/platform_core/platform/emlib/src/em_msc.c"
    "${SDK_PATH}/platform_core/platform/emlib/src/em_prs.c"
    "${SDK_PATH}/platform_core/platform/emlib/src/em_system.c"
    "${SDK_PATH}/platform_core/platform/emlib/src/em_timer.c"
    "${SDK_PATH}/platform_core/platform/emlib/src/em_usart.c"
    "${SDK_PATH}/platform_core/platform/peripheral/src/sl_hal_gpio.c"
    "${SDK_PATH}/platform_core/platform/peripheral/src/sl_hal_prs.c"
    "${SDK_PATH}/platform_core/platform/peripheral/src/sl_hal_syscfg.c"
    "${SDK_PATH}/platform_core/platform/peripheral/src/sl_hal_sysrtc.c"
    "${SDK_PATH}/platform_core/platform/peripheral/src/sl_hal_sysrtc_subsystem.c"
    "${SDK_PATH}/platform_core/platform/peripheral/src/sl_hal_system.c"
    "${SDK_PATH}/platform_core/platform/peripheral/src/sl_hal_usart.c"
    "${SDK_PATH}/platform_core/platform/service/clock_manager/src/sl_clock_manager.c"
    "${SDK_PATH}/platform_core/platform/service/clock_manager/src/sl_clock_manager_hal_s2.c"
    "${SDK_PATH}/platform_core/platform/service/clock_manager/src/sl_clock_manager_init.c"
    "${SDK_PATH}/platform_core/platform/service/clock_manager/src/sl_clock_manager_init_hal_s2.c"
    "${SDK_PATH}/platform_core/platform/service/device_init/src/sl_device_init_dcdc_s2.c"
    "${SDK_PATH}/platform_core/platform/service/device_init/src/sl_device_init_emu_s2.c"
    "${SDK_PATH}/platform_core/platform/service/device_manager/clocks/sl_device_clock_efr32xg28.c"
    "${SDK_PATH}/platform_core/platform/service/device_manager/devices/sl_device_peripheral_hal_efr32xg28.c"
    "${SDK_PATH}/platform_core/platform/service/device_manager/dma/sl_device_dma_s2.c"
    "${SDK_PATH}/platform_core/platform/service/device_manager/src/sl_device_clock.c"
    "${SDK_PATH}/platform_core/platform/service/device_manager/src/sl_device_dma.c"
    "${SDK_PATH}/platform_core/platform/service/device_manager/src/sl_device_gpio.c"
    "${SDK_PATH}/platform_core/platform/service/device_manager/src/sl_device_peripheral.c"
    "${SDK_PATH}/platform_core/platform/service/interrupt_manager/src/sl_interrupt_manager_cortexm.c"
    "${SDK_PATH}/platform_core/platform/service/iostream/src/sl_iostream.c"
    "${SDK_PATH}/platform_core/platform/service/iostream/src/sl_iostream_retarget_stdio.c"
    "${SDK_PATH}/platform_core/platform/service/iostream/src/sl_iostream_uart.c"
    "${SDK_PATH}/platform_core/platform/service/iostream/src/sl_iostream_usart.c"
    "${SDK_PATH}/platform_core/platform/service/memory_manager/src/sl_memory_manager.c"
    "${SDK_PATH}/platform_core/platform/service/memory_manager/src/sl_memory_manager_dynamic_reservation.c"
    "${SDK_PATH}/platform_core/platform/service/memory_manager/src/sl_memory_manager_pool.c"
    "${SDK_PATH}/platform_core/platform/service/memory_manager/src/sl_memory_manager_pool_common.c"
    "${SDK_PATH}/platform_core/platform/service/memory_manager/src/sl_memory_manager_region.c"
    "${SDK_PATH}/platform_core/platform/service/memory_manager/src/sl_memory_manager_retarget.c"
    "${SDK_PATH}/platform_core/platform/service/memory_manager/src/sli_memory_manager_common.c"
    "${SDK_PATH}/platform_core/platform/service/sl_main/src/sl_main_init.c"
    "${SDK_PATH}/platform_core/platform/service/sl_main/src/sl_main_init_memory.c"
    "${SDK_PATH}/platform_core/platform/service/sl_main/src/sl_main_process_action.c"
    "${SDK_PATH}/platform_core/platform/service/sleeptimer/src/sl_sleeptimer.c"
    "${SDK_PATH}/platform_core/platform/service/sleeptimer/src/sl_sleeptimer_hal_burtc.c"
    "${SDK_PATH}/platform_core/platform/service/sleeptimer/src/sl_sleeptimer_hal_sysrtc.c"
    "${SDK_PATH}/platform_core/platform/service/sleeptimer/src/sl_sleeptimer_hal_timer.c"
    "${SDK_PATH}/security_mbedtls/platform/security/sl_component/sl_mbedtls_support/src/mbedtls_sha.c"
    "${SDK_PATH}/security_mbedtls/platform/security/sl_component/sl_mbedtls_support/src/se_aes.c"
    "${SDK_PATH}/security_mbedtls/platform/security/sl_component/sl_mbedtls_support/src/sl_entropy_hardware.c"
    "${SDK_PATH}/security_mbedtls/platform/security/sl_component/sl_mbedtls_support/src/sl_mbedtls.c"
    "${SDK_PATH}/security_mbedtls/platform/security/sl_component/sl_mbedtls_support/src/sl_psa_crypto.c"
    "${SDK_PATH}/security_mbedtls/platform/security/sl_component/sl_mbedtls_support/src/sli_psa_crypto.c"
    "${SDK_PATH}/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_psa_driver_common.c"
    "${SDK_PATH}/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_psa_driver_init.c"
    "${SDK_PATH}/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_psa_trng.c"
    "${SDK_PATH}/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_se_driver_aead.c"
    "${SDK_PATH}/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_se_driver_builtin_keys.c"
    "${SDK_PATH}/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_se_driver_cipher.c"
    "${SDK_PATH}/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_se_driver_key_derivation.c"
    "${SDK_PATH}/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_se_driver_key_management.c"
    "${SDK_PATH}/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_se_driver_mac.c"
    "${SDK_PATH}/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_se_driver_signature.c"
    "${SDK_PATH}/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_se_opaque_driver_aead.c"
    "${SDK_PATH}/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_se_opaque_driver_cipher.c"
    "${SDK_PATH}/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_se_opaque_driver_mac.c"
    "${SDK_PATH}/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_se_opaque_key_derivation.c"
    "${SDK_PATH}/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_se_transparent_driver_aead.c"
    "${SDK_PATH}/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_se_transparent_driver_cipher.c"
    "${SDK_PATH}/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_se_transparent_driver_hash.c"
    "${SDK_PATH}/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_se_transparent_driver_mac.c"
    "${SDK_PATH}/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_se_transparent_key_derivation.c"
    "${SDK_PATH}/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_se_version_dependencies.c"
    "${SDK_PATH}/security_mbedtls_source/library/aes.c"
    "${SDK_PATH}/security_mbedtls_source/library/cipher.c"
    "${SDK_PATH}/security_mbedtls_source/library/cipher_wrap.c"
    "${SDK_PATH}/security_mbedtls_source/library/constant_time.c"
    "${SDK_PATH}/security_mbedtls_source/library/ctr_drbg.c"
    "${SDK_PATH}/security_mbedtls_source/library/entropy.c"
    "${SDK_PATH}/security_mbedtls_source/library/entropy_poll.c"
    "${SDK_PATH}/security_mbedtls_source/library/hmac_drbg.c"
    "${SDK_PATH}/security_mbedtls_source/library/md.c"
    "${SDK_PATH}/security_mbedtls_source/library/platform.c"
    "${SDK_PATH}/security_mbedtls_source/library/platform_util.c"
    "${SDK_PATH}/security_mbedtls_source/library/psa_crypto.c"
    "${SDK_PATH}/security_mbedtls_source/library/psa_crypto_aead.c"
    "${SDK_PATH}/security_mbedtls_source/library/psa_crypto_cipher.c"
    "${SDK_PATH}/security_mbedtls_source/library/psa_crypto_client.c"
    "${SDK_PATH}/security_mbedtls_source/library/psa_crypto_driver_wrappers_no_static.c"
    "${SDK_PATH}/security_mbedtls_source/library/psa_crypto_ecp.c"
    "${SDK_PATH}/security_mbedtls_source/library/psa_crypto_ffdh.c"
    "${SDK_PATH}/security_mbedtls_source/library/psa_crypto_hash.c"
    "${SDK_PATH}/security_mbedtls_source/library/psa_crypto_mac.c"
    "${SDK_PATH}/security_mbedtls_source/library/psa_crypto_pake.c"
    "${SDK_PATH}/security_mbedtls_source/library/psa_crypto_rsa.c"
    "${SDK_PATH}/security_mbedtls_source/library/psa_crypto_se.c"
    "${SDK_PATH}/security_mbedtls_source/library/psa_crypto_slot_management.c"
    "${SDK_PATH}/security_mbedtls_source/library/psa_crypto_storage.c"
    "${SDK_PATH}/security_mbedtls_source/library/psa_util.c"
    "${SDK_PATH}/security_mbedtls_source/library/sha256.c"
    "${SDK_PATH}/security_mbedtls_source/library/threading.c"
    "${SDK_PATH}/security_se_manager/platform/security/sl_component/se_manager/src/sl_se_manager.c"
    "${SDK_PATH}/security_se_manager/platform/security/sl_component/se_manager/src/sl_se_manager_attestation.c"
    "${SDK_PATH}/security_se_manager/platform/security/sl_component/se_manager/src/sl_se_manager_cipher.c"
    "${SDK_PATH}/security_se_manager/platform/security/sl_component/se_manager/src/sl_se_manager_entropy.c"
    "${SDK_PATH}/security_se_manager/platform/security/sl_component/se_manager/src/sl_se_manager_hash.c"
    "${SDK_PATH}/security_se_manager/platform/security/sl_component/se_manager/src/sl_se_manager_key_derivation.c"
    "${SDK_PATH}/security_se_manager/platform/security/sl_component/se_manager/src/sl_se_manager_key_handling.c"
    "${SDK_PATH}/security_se_manager/platform/security/sl_component/se_manager/src/sl_se_manager_signature.c"
    "${SDK_PATH}/security_se_manager/platform/security/sl_component/se_manager/src/sl_se_manager_util.c"
    "${SDK_PATH}/security_se_manager/platform/security/sl_component/se_manager/src/sli_se_manager_mailbox.c"
    "../app.c"
    "../autogen/sl_board_default_init.c"
    "../autogen/sl_event_handler.c"
    "../autogen/sl_iostream_handles.c"
    "../autogen/sl_iostream_init_usart_instances.c"
    "../main.c"
)

target_include_directories(slc PUBLIC
   "../config"
   "../autogen"
   "../."
    "${SDK_PATH}/devices/platform/Device/SiliconLabs/EFM32PG28/Include"
    "${SDK_PATH}/platform_common/platform/common/inc"
    "${SDK_PATH}/boards/hardware/board/inc"
    "${SDK_PATH}/platform_core/platform/service/clock_manager/inc"
    "${SDK_PATH}/platform_core/platform/service/clock_manager/src"
    "${SDK_PATH}/cmsis/Core/Include"
    "${SDK_PATH}/platform_core/hardware/driver/configuration_over_swo/inc"
    "${SDK_PATH}/platform_core/platform/driver/cycle_counter/inc"
    "${SDK_PATH}/platform_core/platform/driver/debug/inc"
    "${SDK_PATH}/platform_core/platform/service/device_manager/inc"
    "${SDK_PATH}/platform_core/platform/service/device_init/inc"
    "${SDK_PATH}/platform_core/platform/emdrv/dmadrv/inc"
    "${SDK_PATH}/platform_core/platform/emdrv/dmadrv/inc/s2_signals"
    "${SDK_PATH}/compute/driver/mvp/inc"
    "${SDK_PATH}/platform_core/platform/emdrv/common/inc"
    "${SDK_PATH}/platform_core/platform/emlib/inc"
    "${SDK_PATH}/platform_core/platform/common/errno_error_codes/inc"
    "${SDK_PATH}/platform_core/platform/driver/gpio/inc"
    "${SDK_PATH}/platform_core/platform/peripheral/inc"
    "${SDK_PATH}/platform_core/platform/service/interrupt_manager/inc"
    "${SDK_PATH}/platform_core/platform/service/interrupt_manager/src"
    "${SDK_PATH}/platform_core/platform/service/interrupt_manager/inc/arm"
    "${SDK_PATH}/platform_core/platform/service/iostream/inc"
    "${SDK_PATH}/compute/math/inc"
    "${SDK_PATH}/compute/math/mvp/inc"
    "${SDK_PATH}/security_mbedtls/platform/security/sl_component/sl_mbedtls_support/config"
    "${SDK_PATH}/security_mbedtls/platform/security/sl_component/sl_mbedtls_support/config/preset"
    "${SDK_PATH}/security_mbedtls_source/include"
    "${SDK_PATH}/security_mbedtls/platform/security/sl_component/sl_mbedtls_support/inc"
    "${SDK_PATH}/security_mbedtls_source/library"
    "${SDK_PATH}/platform_core/platform/service/memory_manager/inc"
    "${SDK_PATH}/platform_core/platform/service/memory_manager/src"
    "${SDK_PATH}/security_mbedtls/platform/security/sl_component/sl_psa_driver/inc"
    "${SDK_PATH}/security_se_manager/platform/security/sl_component/se_manager/inc"
    "${SDK_PATH}/platform_core/platform/common/inc"
    "${SDK_PATH}/platform_core/platform/service/sl_main/inc"
    "${SDK_PATH}/platform_core/platform/service/sl_main/src"
    "${SDK_PATH}/platform_core/platform/service/sleeptimer/inc"
    "${SDK_PATH}/platform_core/platform/service/sleeptimer/src"
    "${SDK_PATH}/security_se_manager/platform/security/sl_component/sli_psec_osal/inc"
)

target_compile_definitions(slc PUBLIC
    "DEBUG_EFM=1"
    "EFM32PG28B310F1024IM68=1"
    "SL_CODE_COMPONENT_SYSTEM=system"
    "HFXO_FREQ=39000000"
    "SL_BOARD_NAME=\"BRD2506A\""
    "SL_BOARD_REV=\"A03\""
    "SL_CODE_COMPONENT_CLOCK_MANAGER=clock_manager"
    "SL_COMPONENT_CATALOG_PRESENT=1"
    "SL_CODE_COMPONENT_DEVICE_PERIPHERAL=device_peripheral"
    "SL_CODE_COMPONENT_DMADRV=dmadrv"
    "SL_CODE_COMPONENT_GPIO=gpio"
    "SL_CODE_COMPONENT_HAL_COMMON=hal_common"
    "SL_CODE_COMPONENT_HAL_GPIO=hal_gpio"
    "SL_CODE_COMPONENT_HAL_SYSRTC=hal_sysrtc"
    "SL_CODE_COMPONENT_INTERRUPT_MANAGER=interrupt_manager"
    "CMSIS_NVIC_VIRTUAL=1"
    "CMSIS_NVIC_VIRTUAL_HEADER_FILE=\"cmsis_nvic_virtual.h\""
    "MBEDTLS_CONFIG_FILE=<sl_mbedtls_config.h>"
    "SL_CODE_COMPONENT_MEMORY_MANAGER=memory_manager"
    "MBEDTLS_PSA_CRYPTO_CONFIG_FILE=<psa_crypto_config.h>"
    "SL_CODE_COMPONENT_SE_MANAGER=se_manager"
    "SL_CODE_COMPONENT_CORE=core"
    "SL_CODE_COMPONENT_SLEEPTIMER=sleeptimer"
    "SL_CODE_COMPONENT_PSEC_OSAL=psec_osal"
)

target_link_libraries(slc PUBLIC
    "-Wl,--start-group"
    "gcc"
    "c"
    "m"
    "nosys"
    "-Wl,--end-group"
)
target_compile_options(slc PUBLIC
    $<$<COMPILE_LANGUAGE:C>:-mcpu=cortex-m33>
    $<$<COMPILE_LANGUAGE:C>:-mthumb>
    $<$<COMPILE_LANGUAGE:C>:-mfpu=fpv5-sp-d16>
    $<$<COMPILE_LANGUAGE:C>:-mfloat-abi=hard>
    $<$<COMPILE_LANGUAGE:C>:-mcmse>
    $<$<COMPILE_LANGUAGE:C>:-Wall>
    $<$<COMPILE_LANGUAGE:C>:-Wextra>
    $<$<COMPILE_LANGUAGE:C>:-Os>
    $<$<COMPILE_LANGUAGE:C>:-fdata-sections>
    $<$<COMPILE_LANGUAGE:C>:-ffunction-sections>
    $<$<COMPILE_LANGUAGE:C>:-fomit-frame-pointer>
    $<$<COMPILE_LANGUAGE:C>:-g>
    $<$<COMPILE_LANGUAGE:C>:--specs=nano.specs>
    $<$<COMPILE_LANGUAGE:C>:-mfp16-format=ieee>
    $<$<COMPILE_LANGUAGE:C>:-fno-lto>
    $<$<COMPILE_LANGUAGE:CXX>:-mcpu=cortex-m33>
    $<$<COMPILE_LANGUAGE:CXX>:-mthumb>
    $<$<COMPILE_LANGUAGE:CXX>:-mfpu=fpv5-sp-d16>
    $<$<COMPILE_LANGUAGE:CXX>:-mfloat-abi=hard>
    $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>
    $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>
    $<$<COMPILE_LANGUAGE:CXX>:-mcmse>
    $<$<COMPILE_LANGUAGE:CXX>:-Wall>
    $<$<COMPILE_LANGUAGE:CXX>:-Wextra>
    $<$<COMPILE_LANGUAGE:CXX>:-Os>
    $<$<COMPILE_LANGUAGE:CXX>:-fdata-sections>
    $<$<COMPILE_LANGUAGE:CXX>:-ffunction-sections>
    $<$<COMPILE_LANGUAGE:CXX>:-fomit-frame-pointer>
    $<$<COMPILE_LANGUAGE:CXX>:-g>
    $<$<COMPILE_LANGUAGE:CXX>:--specs=nano.specs>
    $<$<COMPILE_LANGUAGE:CXX>:-mfp16-format=ieee>
    $<$<COMPILE_LANGUAGE:CXX>:-fno-lto>
    $<$<COMPILE_LANGUAGE:ASM>:-mcpu=cortex-m33>
    $<$<COMPILE_LANGUAGE:ASM>:-mthumb>
    $<$<COMPILE_LANGUAGE:ASM>:-mfpu=fpv5-sp-d16>
    $<$<COMPILE_LANGUAGE:ASM>:-mfloat-abi=hard>
    "$<$<COMPILE_LANGUAGE:ASM>:SHELL:-x assembler-with-cpp>"
)

set(post_build_command )
set_property(TARGET slc PROPERTY C_STANDARD 17)
set_property(TARGET slc PROPERTY CXX_STANDARD 17)
set_property(TARGET slc PROPERTY CXX_EXTENSIONS OFF)

target_link_options(slc INTERFACE
    -mcpu=cortex-m33
    -mthumb
    -mfpu=fpv5-sp-d16
    -mfloat-abi=hard
    -T${CMAKE_CURRENT_LIST_DIR}/../autogen/linkerfile.ld
    --specs=nano.specs
    "SHELL:-Xlinker -Map=$<TARGET_FILE_DIR:RBLWE_on_EFM32_ShAd_CBD0>/RBLWE_on_EFM32_ShAd_CBD0.map"
    "SHELL:-Wl,--wrap=_free_r -Wl,--wrap=_malloc_r -Wl,--wrap=_calloc_r -Wl,--wrap=_realloc_r"
    -fno-lto
    -Wl,--gc-sections
)

# BEGIN_SIMPLICITY_STUDIO_METADATA=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=END_SIMPLICITY_STUDIO_METADATA

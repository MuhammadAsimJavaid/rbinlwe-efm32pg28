cmake_minimum_required(VERSION "3.25")

project(
  MLKEM512_on_EFM32PG28
  VERSION 1.0
  LANGUAGES C CXX ASM
)

# Reuse the generated SLC content from the EFM32PG28 bare-metal template.
include(MLKEM512_on_EFM32PG28.cmake)

set(PQCLEAN_ROOT "${CMAKE_CURRENT_LIST_DIR}/../third_party/pqclean")
set(PQCLEAN_MLKEM_DIR "${PQCLEAN_ROOT}/crypto_kem/ml-kem-512/clean")
set(PQCLEAN_COMMON_DIR "${PQCLEAN_ROOT}/common")

set(PQCLEAN_MLKEM_SOURCES
  ${PQCLEAN_MLKEM_DIR}/cbd.c
  ${PQCLEAN_MLKEM_DIR}/indcpa.c
  ${PQCLEAN_MLKEM_DIR}/kem.c
  ${PQCLEAN_MLKEM_DIR}/ntt.c
  ${PQCLEAN_MLKEM_DIR}/poly.c
  ${PQCLEAN_MLKEM_DIR}/polyvec.c
  ${PQCLEAN_MLKEM_DIR}/reduce.c
  ${PQCLEAN_MLKEM_DIR}/symmetric-shake.c
  ${PQCLEAN_MLKEM_DIR}/verify.c
  ${PQCLEAN_COMMON_DIR}/fips202.c
  ${PQCLEAN_COMMON_DIR}/randombytes.c
)

add_executable(MLKEM512_on_EFM32PG28
  ../mlkem_bench.c
  ${PQCLEAN_MLKEM_SOURCES}
)

target_include_directories(MLKEM512_on_EFM32PG28 PUBLIC
  ..
  ${PQCLEAN_MLKEM_DIR}
  ${PQCLEAN_COMMON_DIR}
)

target_compile_options(MLKEM512_on_EFM32PG28 PRIVATE
  -O2
  -std=c99
)

target_link_libraries(MLKEM512_on_EFM32PG28 PRIVATE
  slc
)

# Do not include the old RBLWE_*_project.cmake; it targets a different executable.

set_target_properties(MLKEM512_on_EFM32PG28 PROPERTIES LINKER_LANGUAGE C)

add_custom_command(TARGET MLKEM512_on_EFM32PG28
  POST_BUILD
  COMMAND ${CMAKE_OBJCOPY} ${OBJCOPY_SREC_CMD} "$<TARGET_FILE:MLKEM512_on_EFM32PG28>" "$<TARGET_FILE_DIR:MLKEM512_on_EFM32PG28>/$<TARGET_FILE_BASE_NAME:MLKEM512_on_EFM32PG28>.s37"
  COMMAND ${CMAKE_OBJCOPY} ${OBJCOPY_IHEX_CMD} "$<TARGET_FILE:MLKEM512_on_EFM32PG28>" "$<TARGET_FILE_DIR:MLKEM512_on_EFM32PG28>/$<TARGET_FILE_BASE_NAME:MLKEM512_on_EFM32PG28>.hex"
  COMMAND ${CMAKE_OBJCOPY} ${OBJCOPY_BIN_CMD}  "$<TARGET_FILE:MLKEM512_on_EFM32PG28>" "$<TARGET_FILE_DIR:MLKEM512_on_EFM32PG28>/$<TARGET_FILE_BASE_NAME:MLKEM512_on_EFM32PG28>.bin"
)

if(post_build_command)
  add_custom_command(TARGET MLKEM512_on_EFM32PG28
    POST_BUILD
    WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/..
    COMMAND ${post_build_command}
  )
endif()

cmake_minimum_required(VERSION 3.13)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE INTERNAL "")
set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/tools")
include(arm-none-eabi-gcc)

# TinyUSB 의 tinyusb_target_add() 함수 정의를 로드한다. (5단계에서 벤더링)
set(HW_USE_TINYUSB 0)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/lib/tinyusb/src/CMakeLists.txt")
  include("src/lib/tinyusb/src/CMakeLists.txt")
  set(HW_USE_TINYUSB 1)
endif()


# 프로젝트 이름은 최종 출력 파일 이름으로 사용된다.
#
set(PRJ_NAME weact-h750-fw)


project(${PRJ_NAME}
  LANGUAGES ASM C
)


set(EXECUTABLE ${PRJ_NAME}.elf)


# 지정한 폴더에 있는 파일만 포함한다.
#
file(GLOB SRC_FILES CONFIGURE_DEPENDS
  src/*.c
  src/ap/*.c
  src/bsp/*.c
  src/bsp/device/*.c
)

if(HW_USE_TINYUSB)
  # TinyUSB DCD 포트. STM32H750 의 OTG_FS 는 Synopsys DWC2 계열이다.
  list(APPEND SRC_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/lib/tinyusb/src/portable/synopsys/dwc2/dcd_dwc2.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/lib/tinyusb/src/portable/synopsys/dwc2/dwc2_common.c
  )
endif()

# 하위폴더에 있는 파일까지 포함한다.
#
file(GLOB_RECURSE SRC_FILES_RECURSE CONFIGURE_DEPENDS
  src/ap/*.c
  src/bsp/*.s
  src/common/*.c
  src/hw/*.c
  )

file(GLOB_RECURSE SRC_FILES_SPECIAL CONFIGURE_DEPENDS
  src/lib/ST/STM32H7xx_HAL_Driver/Src/*.c
  )


# 특정 폴더를 빌드에서 제외한다.
#
set(EXCLUDE_PATHS
)

foreach(TMP_PATH ${SRC_FILES_RECURSE})
  foreach(EXCLUDE_PATH ${EXCLUDE_PATHS})
    string(FIND ${TMP_PATH} ${EXCLUDE_PATH} RESULT)
    if(NOT ${RESULT} EQUAL -1)
      list(REMOVE_ITEM SRC_FILES_RECURSE ${TMP_PATH})
    endif()
  endforeach(EXCLUDE_PATH)
endforeach(TMP_PATH)


add_executable(${EXECUTABLE}
  ${SRC_FILES}
  ${SRC_FILES_RECURSE}
  ${SRC_FILES_SPECIAL}
)

if(HW_USE_TINYUSB)
  tinyusb_target_add(${EXECUTABLE})
endif()


target_include_directories(${EXECUTABLE} PRIVATE
  src
  src/ap
  src/ap/modules
  src/ap/modules/common
  src/ap/modules/cmd
  src/bsp
  src/bsp/device
  src/common
  src/common/core
  src/common/hw/include
  src/hw
  src/hw/driver
  src/lib

  src/lib/ST/CMSIS/Include
  src/lib/ST/CMSIS/Device/ST/STM32H7xx/Include
  src/lib/ST/STM32H7xx_HAL_Driver/Inc

  # USB
  src/hw/driver/usb
)

target_compile_definitions(${EXECUTABLE} PRIVATE
  -DSTM32H750xx
  -DUSE_HAL_DRIVER
  )

target_compile_options(${EXECUTABLE} PRIVATE
  -mcpu=cortex-m7
  -mthumb
  -mfpu=fpv5-d16
  -mfloat-abi=hard

  -fdata-sections
  -ffunction-sections

  -fstack-usage
  -Wmissing-field-initializers
  -Wuninitialized

  -Wall
  -g3
  -Os
  )

target_link_options(${EXECUTABLE} PRIVATE
  -T../src/bsp/ldscript/STM32H750VBTx_BOOT.ld

  -mcpu=cortex-m7
  -mthumb
  -mfpu=fpv5-d16
  -mfloat-abi=hard
  -specs=nano.specs
  -lc
  -lm

  -Wl,-Map=${PRJ_NAME}.map,--cref
  -Wl,--gc-sections
  -Xlinker -print-memory-usage -Xlinker
  )

set (CMAKE_C_FLAGS "-Wenum-conversion")

# QSPI 브링업 시험. 부팅할 때마다 QSPI 마지막 섹터를 지우고 쓰므로 평소에는 꺼둔다.
# QSPI SCK 를 올리거나 배선을 건드린 뒤 XiP 무결성을 확인할 때만 켠다.
#   target_compile_definitions(${EXECUTABLE} PRIVATE -DQSPI_SELF_TEST)
# 이미지 식별 3단계 시험. QSPI 에 가짜 이미지를 쓰므로 평소에는 꺼둔다.
#   target_compile_definitions(${EXECUTABLE} PRIVATE -DBOOT_SELF_TEST)

if(CMAKE_C_COMPILER_VERSION VERSION_GREATER_EQUAL 11)
  string(APPEND CMAKE_EXE_LINKER_FLAGS "-Wl,--no-warn-rwx-segments")
endif()

add_custom_command(TARGET ${EXECUTABLE}
  POST_BUILD
  COMMAND ${CMAKE_OBJCOPY} ARGS -O binary ${EXECUTABLE} ${PROJECT_NAME}.bin
  COMMENT "Invoking: Make Bin"
  )

# .hex 도 만든다.
#
#   .bin 은 주소가 없는 날바이트라 "0x08000000 에 굽는다" 를 툴에 따로 알려줘야 한다.
#   .hex 는 레코드마다 주소를 갖고 있어서 굽는 쪽이 주소를 몰라도 된다.
#   ST-LINK Utility / CubeProgrammer GUI / 아두이노 코어의 Burn Bootloader 레시피처럼
#   주소 인자를 받지 않는 경로에서 필요하다.
add_custom_command(TARGET ${EXECUTABLE}
  POST_BUILD
  COMMAND ${CMAKE_OBJCOPY} ARGS -O ihex ${EXECUTABLE} ${PROJECT_NAME}.hex
  COMMENT "Invoking: Make Hex"
  )

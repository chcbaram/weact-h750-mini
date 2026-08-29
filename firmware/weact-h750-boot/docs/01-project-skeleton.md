# 01. 프로젝트 골격

## 목적

`stm32h5-w6300` 의 프로젝트 구조를 STM32H750 으로 옮긴다. 새로 만드는 것은 최소화하고,
이미 검증된 계층 구조와 빌드 방식을 그대로 쓴다.

## 대상 파일

- `CMakeLists.txt`, `tools/arm-none-eabi-gcc.cmake`
- `src/bsp/ldscript/STM32H750VBTx_BOOT.ld`
- `src/bsp/startup/startup_stm32h750xx.s`
- `src/lib/ST/**` (벤더 SDK)

## 계층 구조

호출 순서는 `main → bsp → hw → ap` 로 고정이다.

| 계층 | 역할 |
|---|---|
| `src/main.c` | `bspInit(); hwInit(); apInit(); apMain();` |
| `src/bsp/` | 칩 브링업 — 클럭, 캐시, MPU, `delay/millis/micros`, 벡터, startup, 링커스크립트 |
| `src/hw/` | 보드별 드라이버 **구현** + `hw_def.h` (단일 보드 설정 파일) |
| `src/common/` | 보드 비의존 — `def.h`, `err_code.h`, `core/`, `hw/include/` (드라이버 **API 카탈로그**), `hw/src/` |
| `src/ap/` | 애플리케이션 — `ap.c` + 자가등록 `modules/` |
| `src/lib/` | 벤더 SDK. 손대지 않는다 |

핵심 규칙: **헤더는 `common/hw/include/`, 구현은 `hw/driver/`.** 헤더 카탈로그는 프로젝트
간에 그대로 복사되며, `hw_def.h` 의 `_USE_HW_*` 로 켠 것만 컴파일된다.

## 벤더 SDK 이식

`/Users/hancheol/hdd/tools/STM32/STM32Cube/STM32Cube_FW_H7_V1.12.1` 에서 가져왔다.

| 원본 | 대상 |
|---|---|
| `Drivers/CMSIS/Include/` | `src/lib/ST/CMSIS/Include/` |
| `Drivers/CMSIS/Device/ST/STM32H7xx/Include/` | 같은 경로 (`stm32h750xx.h` 포함) |
| `Drivers/STM32H7xx_HAL_Driver/{Inc,Src}/` | `src/lib/ST/STM32H7xx_HAL_Driver/` |
| `.../Source/Templates/gcc/startup_stm32h750xx.s` | `src/bsp/startup/` |
| `.../Source/Templates/system_stm32h7xx.c` | `src/bsp/device/` |
| `Inc/stm32h7xx_hal_conf_template.h` | `src/bsp/device/stm32h7xx_hal_conf.h` |

`*_template.c` 와 `stm32h7xx_hal_conf_template.h` 는 `src/lib` 에서 지웠다. 남겨두면
`file(GLOB_RECURSE ... Src/*.c)` 에 걸려 중복 정의가 난다.

### HAL 모듈 정리

템플릿은 63개 모듈을 전부 켠다. 부트로더에 필요한 15개만 남기고 나머지 48개는 주석 처리했다.

```
HAL_MODULE_ENABLED     CORTEX  RCC   GPIO  FLASH  PWR
DMA  MDMA  EXTI  QSPI  SPI  TIM  UART  RTC  HSEM
```

`HSE_VALUE` 는 템플릿 기본값이 이미 `25000000UL` 이라 이 보드와 맞는다.

## startup 파일 수정

ST 원본에 **폴트 트램폴린 4개**를 추가했다. 예외 진입 시 어느 스택(MSP/PSP)이 쓰였는지
`EXC_RETURN(LR)` 의 bit2 로 판별해 R0 로 넘긴다. 그래야 `*_Handler_C()` 가 스택 프레임에서
R0-R3/R12/LR/PC/PSR 을 꺼낼 수 있다.

```asm
HardFault_Asm_Handler:
  tst    LR, #4
  ite    EQ
  mrseq  R0, MSP
  mrsne  R0, PSP
  b      HardFault_Handler_C
```

그리고 weak alias 4개를 `Default_Handler` 에서 각 트램폴린으로 바꿨다.

> **함정**: BSD sed 는 `\s` 를 모른다. `[[:space:]]` 를 쓰거나 패턴에서 빼야 한다.
> 처음에 `\s` 로 치환했다가 조용히 아무것도 안 바뀌었다.

## 링커스크립트

`MEMORY` 블록은 00 문서 참고. 새 프로젝트가 반드시 유지해야 하는 세 가지:

1. **`.module` 섹션이 `.data` 안, `_smodule`/`_emodule` 사이**에 있을 것.
   `moduleInit()` 이 `(_emodule - _smodule) / sizeof(module_t)` 로 모듈 개수를 센다.
2. **`.version` 섹션이 자기 `VER` 영역**에 있을 것.
3. **`.noinit` 이 SRAM 최하단 8KB 에 별도 영역**으로 있을 것.

그리고 이 프로젝트가 추가한 것:

4. **`.non_cache` 를 반드시 선언**할 것. D-Cache 를 켜고 쓰므로 DMA 버퍼가 여기 들어간다.

## CMake

`stm32h5-boot` 의 것을 그대로 가져오고 H7 로 바꿨다.

```cmake
target_compile_definitions(... -DSTM32H750xx -DUSE_HAL_DRIVER)
target_compile_options(... -mcpu=cortex-m7 -mfpu=fpv5-d16 -mfloat-abi=hard -Os)
target_link_options(... -T../src/bsp/ldscript/STM32H750VBTx_BOOT.ld)
```

`-Os` 는 전역이다 (`stm32h5-fw` 처럼 HAL 만 `-O0` 로 낮추지 않는다). 128KB 안에 들어가야 하기
때문이다.

TinyUSB 는 5단계에서 벤더링하므로, 그때까지 빌드가 깨지지 않도록 CMakeLists 에서
존재 여부로 감쌌다.

```cmake
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/lib/tinyusb/src/CMakeLists.txt")
  include("src/lib/tinyusb/src/CMakeLists.txt")
  set(HW_USE_TINYUSB 1)
endif()
```

## 함정

- **`-T` 경로가 `../src/...` 상대경로다.** `build/` 가 프로젝트 루트 바로 아래에 있어야 한다.
- **Ninja 제너레이터를 쓸 수 없다.** `tools/arm-none-eabi-gcc.cmake` 가 `CMAKE_MAKE_PROGRAM` 을
  `make` 로 찾아 넣기 때문에 `-G Ninja` 와 충돌한다
  (`The detected version of Ninja (GNU Make 4.4.1...) is less than ...`).
  기본 Unix Makefiles 를 쓸 것.
- **아직 없는 드라이버는 `hw_def.h` 에서 주석 처리**해 두었다. `hw.h` 가
  `#ifdef _USE_HW_USB` 로 `usb.h` 를 포함하므로, 정의만 해두고 파일이 없으면 빌드가 깨진다.
  각 단계에서 켠다.

## 검증 방법

```bash
cmake -S . -B build && cmake --build build -j8
```

## 실측 결과

빌드 성공. `-print-memory-usage` 결과는 [STATUS.md](STATUS.md) 참고.
FLASH 42048 B / 126 KB (32.59%) — QSPI/LCD/USB/UF2 를 넣기 전 기준.

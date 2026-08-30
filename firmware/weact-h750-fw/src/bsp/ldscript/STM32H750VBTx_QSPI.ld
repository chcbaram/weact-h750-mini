/*
 ******************************************************************************
 **
 ** @file        : STM32H750VBTx_QSPI.ld
 **
 ** @brief       : WeAct STM32H750 Mini 앱 링커 스크립트 (QSPI XiP)
 **                앱은 내부 플래시가 아니라 **외부 QSPI 에서 직접 실행**된다.
 **                부트로더가 QUADSPI 를 memory-mapped 로 열어둔 채 점프하므로
 **                0x90000000 이 그대로 코드 영역이 된다.
 **
 **                QSPI 8MB 배치 (00 문서)
 **                  0x90000000  4KB      TAG   firm_tag_t. 부트로더가 쓴다
 **                  0x90001000  1KB      VECTOR 앱 벡터 테이블
 **                  0x90001400  1KB      VER   firm_ver_t
 **                  0x90001800  8M-6KB   FLASH 코드 + rodata + .data 의 LMA
 **
 **                TAG 섹터를 앱 링크 범위에서 **제외**하는 것이 중요하다.
 **                부트로더가 태그를 갱신할 때 그 4KB 섹터만 지우는데, 앱이
 **                거기 걸쳐 있으면 벡터 테이블이 함께 날아간다 (07 문서).
 **
 ******************************************************************************
 */

/* Entry Point */
ENTRY(Reset_Handler)

/* Highest address of the user mode stack */
_estack = ORIGIN(RAM) + LENGTH(RAM);    /* end of AXI SRAM */

_Min_Heap_Size  = 0x200;
_Min_Stack_Size = 0x1000;

/*
 * 메모리 배치 주의사항
 *
 *  NO_INIT : DTCM 최하단 8K. .noinit 전용. 리셋(NRST/소프트)으로는 지워지지 않아
 *            fault_log 처럼 리셋을 건너 살아남아야 하는 것들이 여기 들어간다.
 *            startup 이 복사하지도, 0으로 밀지도 않는다.
 *
 *  RAM     : AXI SRAM(D1). .data/.bss/heap/stack 이 여기 온다. DTCM 이 아니라
 *            AXI 를 주 RAM 으로 쓰는 이유는 크기(512K)와 DMA 접근성 때문이다.
 *
 *  D2RAM   : SRAM1+SRAM2+SRAM3(D2 도메인). .non_cache 전용.
 *            LCD 프레임버퍼와 SPI4 DMA 버퍼가 여기 온다. SPI4(APB2)와 DMA1/2 가
 *            D2 도메인에 있고, DTCM 은 DMA1/2 가 아예 접근하지 못한다.
 */
MEMORY
{
  NO_INIT   (xrw) : ORIGIN = 0x20000000, LENGTH = 8K
  DTCM      (xrw) : ORIGIN = 0x20002000, LENGTH = 128K - 8K
  RAM       (xrw) : ORIGIN = 0x24000000, LENGTH = 512K
  D2RAM     (xrw) : ORIGIN = 0x30000000, LENGTH = 288K

  VECTOR     (rx) : ORIGIN = 0x90001000, LENGTH = 1K
  VER        (rx) : ORIGIN = 0x90001400, LENGTH = 1K
  FLASH      (rx) : ORIGIN = 0x90001800, LENGTH = 8M - 6K
}

/*
 * 이미지 크기를 **절대 심볼**로 만든다.
 *
 * C 에서 `(uint32_t)&_fw_flash_end - (uint32_t)&_fw_flash_begin` 을 정적
 * 초기화식에 쓰면 **링커가 조용히 0 으로 접는다.** 심볼 두 개의 차이는 단일
 * relocation 으로 인코딩되지 않는데 경고조차 없다. 그래서 링커가 계산한 값을
 * 심볼 자리에 박아두고, C 에서는 그 **주소**를 값으로 읽는다.
 *
 *   extern uint32_t _fw_flash_size;
 *   .firm_size = (uint32_t)&_fw_flash_size;
 *
 * SECTIONS 바깥이 아니라 안쪽 끝에 두면 출력 섹션에 묶여 상대 심볼이 되므로,
 * 반드시 SECTIONS **바깥**에서 정의한다. 그래야 절대 심볼이 된다.
 * (12 문서)
 */

/* Sections */
SECTIONS
{
  .fw_flash_begin :
  {
    _fw_flash_begin = .;
  } >VECTOR

  .isr_vector :
  {
    KEEP(*(.isr_vector))
  } >VECTOR

  .version :
  {
    . = ALIGN(4);
    KEEP(*(.version))
    . = ALIGN(4);
  } >VER

  .text :
  {
    . = ALIGN(4);
    *(.text)
    *(.text*)
    *(.glue_7)
    *(.glue_7t)
    *(.eh_frame)

    KEEP (*(.init))
    KEEP (*(.fini))

    . = ALIGN(4);
    _etext = .;
  } >FLASH

  .rodata :
  {
    . = ALIGN(4);
    *(.rodata)
    *(.rodata*)
    . = ALIGN(4);
  } >FLASH

  .ARM.extab :
  {
    . = ALIGN(4);
    *(.ARM.extab* .gnu.linkonce.armextab.*)
    . = ALIGN(4);
  } >FLASH

  .ARM :
  {
    . = ALIGN(4);
    __exidx_start = .;
    *(.ARM.exidx*)
    __exidx_end = .;
    . = ALIGN(4);
  } >FLASH

  .preinit_array :
  {
    . = ALIGN(4);
    PROVIDE_HIDDEN (__preinit_array_start = .);
    KEEP (*(.preinit_array*))
    PROVIDE_HIDDEN (__preinit_array_end = .);
    . = ALIGN(4);
  } >FLASH

  .init_array :
  {
    . = ALIGN(4);
    PROVIDE_HIDDEN (__init_array_start = .);
    KEEP (*(SORT(.init_array.*)))
    KEEP (*(.init_array*))
    PROVIDE_HIDDEN (__init_array_end = .);
    . = ALIGN(4);
  } >FLASH

  .fini_array :
  {
    . = ALIGN(4);
    PROVIDE_HIDDEN (__fini_array_start = .);
    KEEP (*(SORT(.fini_array.*)))
    KEEP (*(.fini_array*))
    PROVIDE_HIDDEN (__fini_array_end = .);
    . = ALIGN(4);
  } >FLASH

  /* Used by the startup to initialize data */
  _sidata = LOADADDR(.data);

  .data :
  {
    . = ALIGN(8);
    _sdata = .;
    *(.data)
    *(.data*)
    *(.RamFunc)
    *(.RamFunc*)

    /*
     * MODULE_DEF() 로 자가등록된 module_t 들이 여기 모인다.
     * moduleInit() 이 (_emodule - _smodule) / sizeof(module_t) 로 개수를 센다.
     */
    . = ALIGN(8);
    _smodule = .;
    KEEP (*(.module))
    KEEP (*(.module*))
    _emodule = .;

    . = ALIGN(8);
    _edata = .;
  } >RAM AT> FLASH

  . = ALIGN(4);
  .bss :
  {
    _sbss = .;
    __bss_start__ = _sbss;
    *(.bss)
    *(.bss*)
    *(COMMON)

    . = ALIGN(4);
    _ebss = .;
    __bss_end__ = _ebss;
  } >RAM

  ._user_heap_stack :
  {
    . = ALIGN(8);
    PROVIDE ( end = . );
    PROVIDE ( _end = . );
    . = . + _Min_Heap_Size;
    . = . + _Min_Stack_Size;
    . = ALIGN(8);
  } >RAM

  /*
   * DMA 가 쓰는 버퍼(LCD 프레임버퍼 등). MPU 로 non-cacheable 로 잡는다.
   * NOLOAD 이므로 startup 이 0으로 밀지 않는다 - 쓰는 쪽에서 초기화할 것.
   *
   * 여기에 선언하지 않고 소스에서만 __attribute__((section(".non_cache"))) 를
   * 쓰면 orphan section 이 되어 startup 의 .data 복사에도 .bss 클리어에도
   * 포함되지 않는다. 실기에서 부팅 직후 쓰레기값으로 나온다.
   */
  .non_cache(NOLOAD) :
  {
    . = ALIGN(32);
    *(.non_cache)
    *(.non_cache*)
    . = ALIGN(32);
  } >D2RAM

  .dtcm(NOLOAD) :
  {
    . = ALIGN(4);
    *(.dtcm)
    *(.dtcm*)
    . = ALIGN(4);
  } >DTCM

  .noinit(NOLOAD) :
  {
    . = ALIGN(4);
    KEEP(*(.noinit))
    . = ALIGN(4);
  } >NO_INIT

  /*
   * 이미지의 끝. `.data` 의 LMA 가 FLASH 에 마지막으로 놓이므로 여기가
   * 곧 굽어야 할 바이트 수의 끝이다. CRC 범위도 이 값으로 정해진다.
   */
  .fw_flash_end :
  {
    _fw_flash_end = .;
  } >FLASH

  /DISCARD/ :
  {
    libc.a ( * )
    libm.a ( * )
    libgcc.a ( * )
  }

  .ARM.attributes 0 : { *(.ARM.attributes) }
}

/*
 * SECTIONS 바깥이어야 절대 심볼이 된다. 위 주석 참고.
 */
_fw_flash_size = _fw_flash_end - _fw_flash_begin;

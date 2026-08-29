/*
 ******************************************************************************
 **
 ** @file        : STM32H750VBTx_BOOT.ld
 **
 ** @brief       : WeAct STM32H750 Mini 부트로더 링커 스크립트
 **                내부 플래시 128KB @ 0x08000000 전체를 부트로더가 차지한다.
 **
 **                H750 의 내부 플래시는 128KB 짜리 섹터 하나뿐이라 부트로더가
 **                자기 자신을 지우지 않고는 아무것도 소거할 수 없다. 그래서
 **                내부 플래시에는 영구 저장소(부트로그/NVS)를 두지 않고,
 **                카운터류는 전부 RTC 백업 레지스터에 둔다.
 **
 **                앱은 내부 플래시가 아니라 QSPI 0x90000000 에서 XiP 로 돈다.
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

  VECTOR     (rx) : ORIGIN = 0x08000000, LENGTH = 1K
  VER        (rx) : ORIGIN = 0x08000400, LENGTH = 1K
  FLASH      (rx) : ORIGIN = 0x08000800, LENGTH = 128K - 2K
}

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

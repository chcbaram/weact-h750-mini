#ifndef _TUSB_CONFIG_H_
#define _TUSB_CONFIG_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "hw_def.h"


//--------------------------------------------------------------------
// 보드 설정
//--------------------------------------------------------------------

//-- STM32H750 의 USB 는 OTG_FS 하나뿐이고 RHPort 0 이다.
//   (H7RS/U5 처럼 HS PHY 를 가진 것과 다르다)
#define BOARD_TUD_RHPORT        0
#define BOARD_TUD_MAX_SPEED     OPT_MODE_FULL_SPEED

#define CFG_TUSB_MCU            OPT_MCU_STM32H7
#define CFG_TUSB_OS             OPT_OS_NONE

#ifndef CFG_TUSB_DEBUG
#define CFG_TUSB_DEBUG          0
#endif

#define CFG_TUD_ENABLED         1
#define CFG_TUD_MAX_SPEED       BOARD_TUD_MAX_SPEED

/*
 * D-Cache 를 켜고 쓰므로 DWC2 드라이버의 캐시 유지보수를 켠다.
 * USB DMA 가 건드리는 버퍼가 AXI SRAM(캐시 가능)에 있기 때문이다.
 */
#define CFG_TUD_MEM_DCACHE_ENABLE   1

#ifndef CFG_TUSB_MEM_ALIGN
#define CFG_TUSB_MEM_ALIGN      __attribute__ ((aligned(4)))
#endif

#define CFG_TUD_ENDPOINT0_SIZE  64


//--------------------------------------------------------------------
// 클래스 설정
//
//   CDC : CLI + cmd 패킷
//   HID : 펌웨어 업데이트 (vendor usage page)
//   MSC : UF2 드래그앤드롭
//
// 세 개 모두 컴파일 타임에는 켜둔다. **실제로 열거할지는 런타임에 정한다.**
// usb_desc.c 가 CDC+HID 용과 CDC+HID+MSC 용 디스크립터를 각각 갖고 있고,
// bootUp() 이 정한 값(boot_with_msc)으로 고른다.
//--------------------------------------------------------------------
#define CFG_TUD_CDC             1
#define CFG_TUD_MSC             1
#define CFG_TUD_HID             1
#define CFG_TUD_MIDI            0
#define CFG_TUD_VENDOR          0

#define CFG_TUD_CDC_RX_BUFSIZE  512
#define CFG_TUD_CDC_TX_BUFSIZE  512
#define CFG_TUD_CDC_EP_BUFSIZE  64

#define CFG_TUD_MSC_EP_BUFSIZE  512
#define CFG_TUD_HID_EP_BUFSIZE  64


#ifdef __cplusplus
}
#endif

#endif

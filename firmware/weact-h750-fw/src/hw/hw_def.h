#ifndef HW_DEF_H_
#define HW_DEF_H_


#include "bsp.h"
#include "assert_def.h"


#define _DEF_FIRMWATRE_VERSION    "V260830R4"
#define _DEF_BOARD_NAME           "WEACT-H750-FW"


//-- 하드웨어 핀맵 (WeAct STM32H7XX Board V1.2, 회로도 기준)
//
//   LED      : PE3   (R6 1.5K -> VT2 PDTC114ET NPN -> 파란 LED, active high)
//   KEY  K1  : PC13  (SW2, R8 330R)
//   UART1    : PA9(TX) / PA10(RX)  - P1/P2 헤더로 나와 있음
//   USB      : PA11(DM) / PA12(DP) - OTG_FS. VBUS 는 MCU 에 연결 안 됨
//   QSPI     : CLK=PB2  NCS=PB6  IO0=PD11 IO1=PD12 IO2=PE2 IO3=PD13  (W25Q64 8MB)
//   LCD      : SCL=PE12 SDA=PE14 DC=PE13 CS=PE11 BL=PE10 (SPI4, ST7735S 160x80)
//              RESET 은 보드 SYS_RESET 네트에 물려 있어 소프트웨어 라인이 없다.
//

#define _USE_HW_ASSERT
#define _USE_HW_FAULT

#define _USE_HW_LED
#define      HW_LED_MAX_CH          1
#define      HW_LED_TOGGLE_MS       500   // 부트로더 = 100ms, 앱 = 500ms 로 구분

#define _USE_HW_UART
#define      HW_UART_MAX_CH         3
#define      HW_UART_CH_SWD         _DEF_UART1    // USART1  PA9/PA10
#define      HW_UART_CH_USB         _DEF_UART2    // USB CDC (가상)
#define      HW_UART_CH_CMD         _DEF_UART3    // cmd 패킷 위의 CLI (가상)
#define      HW_UART_CH_CLI         HW_UART_CH_SWD

#define _USE_HW_CLI
#define      HW_CLI_CMD_LIST_MAX    32
#define      HW_CLI_CMD_NAME_MAX    16
#define      HW_CLI_LINE_HIS_MAX    8
#define      HW_CLI_LINE_BUF_MAX    64

#define _USE_HW_CLI_GUI
#define      HW_CLI_GUI_WIDTH       80
#define      HW_CLI_GUI_HEIGHT      24

#define _USE_HW_LOG
#define      HW_LOG_CH              HW_UART_CH_SWD
#define      HW_LOG_BOOT_BUF_MAX    2048
#define      HW_LOG_LIST_BUF_MAX    8192

#define _USE_HW_SWTIMER
#define      HW_SWTIMER_MAX_CH      8

#define _USE_HW_GPIO
#define      HW_GPIO_MAX_CH         GPIO_PIN_MAX

#define _USE_HW_FLASH

#define _USE_HW_RESET
#define      HW_RESET_BOOT          1     // 0 = 앱, 1 = 부트로더 (리셋 플래그 판정 주체)

#define _USE_HW_RTC
#define      HW_RTC_BOOT_MODE       RTC_BKP_DR3   // 앱과 반드시 동일해야 한다
#define      HW_RTC_RESET_BITS      RTC_BKP_DR4
#define      HW_RTC_RESET_CNT       RTC_BKP_DR5
#define      HW_RTC_BOOT_TRY        RTC_BKP_DR6
#define      HW_RTC_FAULT_CNT       RTC_BKP_DR7

//-- 리셋 더블클릭
//
//   이 보드에는 VBAT(BAT54C) 와 LSE 크리스탈이 있으므로 RTC 백업 레지스터가
//   전원이 끊겨도 살아남는다. 다만 백업 도메인이 처음부터 쓰레기일 수 있어
//   HW_RESET_CNT_MAGIC 으로 유효성을 표시한다.
//
#define      HW_RESET_CNT_MAGIC     0xA55A0000UL
#define      HW_RESET_CNT_MASK      0x000000FFUL
#define      HW_RESET_DBLCLK_MS     300
#define      HW_RESET_DBLCLK_CNT    2
//-- 아래 둘은 **부트로더 설정**이다. 앱에서는 아무 효과가 없고, 값을 맞춰둘
//   필요도 없다. 파일을 공유하므로 남겨만 둔다.
#define      HW_BOOT_TRY_MAX        3
#define      HW_BOOT_FAULT_MAX      3

//-- 앱이 이 시간 동안 살아 있으면 resetConfirmBoot() 으로 확인 신호를 보낸다.
//   부트로더의 HW_BOOT_TRY_MAX 를 켜려면 이 호출이 있어야 한다 (07 문서).
#define      HW_BOOT_CONFIRM_MS     3000

//-- 아래 블록들은 해당 단계에서 켠다. (드라이버가 아직 없다)
//   2단계 QSPI / 3단계 SPI,LCD / 5단계 USB,CDC / 6단계 CMD

/*
 * QSPI 드라이버는 **앱에 넣지 않는다** (`_USE_HW_QSPI` 를 정의하지 않는다).
 *
 * 앱은 QSPI 에서 XiP 로 실행 중이다. `qspiRead()` / `qspiErase()` / `qspiWrite()` 는
 * 전부 `qspiSetXipMode(false)` 로 memory-mapped 를 빠져나온 뒤 동작하는데, 그
 * 순간 자기 명령어 인출이 끊긴다. 즉 **부르는 즉시 죽는다** (04/12 문서).
 *
 * 앱이 QSPI 내용을 읽어야 하면 memory-mapped 주소를 그냥 역참조하면 된다.
 * 쓰기/소거는 부트로더에 요청하는 길뿐이다 (`resetToBoot()`).
 *
 * 주소 상수는 아래 메모리 맵에서 쓰므로 남겨둔다.
 */
#define      HW_QSPI_ADDR           0x90000000
#define      HW_QSPI_SIZE           (8*1024*1024)

#define _USE_HW_SPI
#define      HW_SPI_MAX_CH          1
#define      HW_SPI_CH_LCD          _DEF_SPI1     // SPI4  PE12(SCK)/PE14(MOSI)

#define _USE_HW_LCD
#define _USE_HW_ST7735
#define      HW_ST7735_MODEL        0     // 0 = 0.96" 160x80 (colstart 1, rowstart 26)
#define      HW_LCD_WIDTH           160
#define      HW_LCD_HEIGHT          80
#define      HW_LCD_LOGO            0
#define      HW_LCD_LVGL            0
#define      HW_LCD_SWAP_RGB        0
#define      HW_LCD_HANGUL          1     // 앱은 QSPI 8MB 라 한글 글리프 32KB 여유 있다

#define _USE_HW_USB
#define _USE_HW_CDC
#define      HW_USE_CDC             1
#define      HW_USE_MSC             1
#define      HW_USE_HID             1
#define      HW_USB_VID             0x1209        // pid.codes (오픈소스 하드웨어용 공용 VID)
#define      HW_USB_PID             0xB752        // 앱. 부트로더(B750/B751)와 반드시 달라야
                                                 // 호스트가 지금 어느 쪽인지 USB 만으로 안다.
#define      HW_USB_PID_MSC         0xB752        // 앱은 MSC 를 열지 않는다 (9단계에서 제거)

#define _USE_HW_CMD
#define      HW_CMD_MAX_DATA_LENGTH 1024


//-- 메모리 배치 : weact-h750-fw 의 hw_def.h 와 반드시 동일하게 유지할 것
//
//   내부 플래시 128KB 는 전부 부트로더 몫이다. H750 은 이 128KB 가 섹터
//   하나여서, 부트로더가 자기를 지우지 않고는 내부 플래시를 소거할 수 없다.
//   따라서 내부 플래시에는 어떤 영구 저장소도 두지 않는다.
//
//   앱은 QSPI 0x90000000 에서 XiP 로 실행된다.
//
//   FLASH_SIZE_TAG 가 4KB 인 것이 중요하다. W25Q64 의 소거 단위가 4KB 이므로
//   태그와 앱 벡터 테이블이 같은 섹터에 있으면 태그를 갱신하려고 섹터를
//   지우는 순간 벡터 테이블이 날아간다.
//
#define FLASH_SIZE_TAG              0x1000        // 4KB = QSPI 섹터 1개
#define FLASH_SIZE_VEC              0x400
#define FLASH_SIZE_VER              0x400

#define FLASH_ADDR_BOOT             0x08000000
#define FLASH_SIZE_BOOT             (128*1024)

#define FLASH_ADDR_FIRM             HW_QSPI_ADDR                        // 0x90000000 (TAG)
#define FLASH_SIZE_FIRM             HW_QSPI_SIZE                        // 8MB
#define FLASH_ADDR_FIRM_VEC         (FLASH_ADDR_FIRM + FLASH_SIZE_TAG)  // 0x90001000

#define BOARD_UF2_FAMILY_ID         0xFFFF0004UL

#define HW_DEV_MODE_BOOT            0
#define HW_DEV_MODE_APP             1
#define HW_DEV_MODE                 HW_DEV_MODE_APP

//-- 앱은 부트로더(내부 플래시)를 보호한다. 앱이 내부 플래시를 지울 일은 없다.
#define FLASH_PROTECT_ADDR          FLASH_ADDR_BOOT
#define FLASH_PROTECT_SIZE          FLASH_SIZE_BOOT

//-- 태그가 없는 이미지(아두이노 빌드 등)를 만나면 부트로더가 CRC 를 계산해
//   태그를 만들어 넣을지 여부. 0 이면 태그 없이 그냥 부팅만 한다.
#define HW_BOOT_AUTO_TAG            1


//-- CLI 기능 스위치 (CLI_USE(HW_XXX) 로 사용)
#define _USE_CLI_HW_LOG             1
#define _USE_CLI_HW_ASSERT          1
#define _USE_CLI_HW_UART            1
#define _USE_CLI_HW_RESET           1
#define _USE_CLI_HW_FLASH           1
#define _USE_CLI_HW_QSPI            1
#define _USE_CLI_HW_LCD             1
#define _USE_CLI_HW_USB             1
#define _USE_CLI_HW_BOOT            1


typedef enum
{
  LCD_CS,
  LCD_DC,
  LCD_BL,
  KEY_1,
  GPIO_PIN_MAX
} GpioPinName_t;


#endif

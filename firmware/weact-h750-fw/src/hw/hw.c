#include "hw.h"


extern uint32_t _fw_flash_begin;
extern uint32_t _fw_flash_size;


/*
 * 앱의 버전 정보. 링커스크립트의 VER 영역(0x90001400)에 놓인다.
 *
 * **`firm_size` 가 이 구조체의 존재 이유다.** 태그(`firm_tag_t`)가 없는 이미지를
 * 만나면 부트로더가 이걸 읽어 **2단계(VER)로 판정**하고, 크기를 알았으니 CRC 를
 * 직접 계산해 태그 섹터에 써넣어 1단계(TAG)로 승격한다. SWD 로 굽든 어떻게 굽든
 * 다음 부팅부터는 CRC 검증을 받게 된다 (07 문서).
 *
 * `firm_size` 를 `_fw_flash_end - _fw_flash_begin` 으로 쓰면 안 된다.
 * 심볼 두 개의 차이는 단일 relocation 으로 인코딩되지 않아 **링커가 조용히 0 으로
 * 접는다.** 경고도 없다. 링커스크립트가 만든 절대 심볼의 **주소**를 값으로 읽는다.
 */
volatile const firm_ver_t firm_ver __attribute__((section(".version"))) =
{
  .magic_number = VERSION_MAGIC_NUMBER,
  .version_str  = _DEF_FIRMWATRE_VERSION,
  .name_str     = _DEF_BOARD_NAME,
  .firm_addr    = (uint32_t)&_fw_flash_begin,
  .firm_size    = (uint32_t)&_fw_flash_size,
};


bool hwInit(void)
{
  cliInit();
  logInit();
  swtimerInit();
  ledInit();
  uartInit();

  for (int i=0; i<HW_UART_MAX_CH; i++)
  {
    uartOpen(i, 115200);
  }

  logOpen(HW_LOG_CH, 115200);

  logPrintf("\r\n[ Bootloader Begin... ]\r\n");
  logPrintf("Booting..Name \t\t: %s\r\n", _DEF_BOARD_NAME);
  logPrintf("Booting..Ver  \t\t: %s\r\n", _DEF_FIRMWATRE_VERSION);
  logPrintf("Booting..Clock\t\t: %d Mhz\r\n", (int)HAL_RCC_GetSysClockFreq()/1000000);
  logPrintf("Booting..HCLK \t\t: %d Mhz\r\n", (int)HAL_RCC_GetHCLKFreq()/1000000);
  logPrintf("Booting..Date \t\t: %s\r\n", __DATE__);
  logPrintf("Booting..Time \t\t: %s\r\n", __TIME__);
  logPrintf("Booting..Addr \t\t: 0x%X\r\n", (uint32_t)&_fw_flash_begin);
  logPrintf("\n");

  //-- rtcInit() 이 resetInit() 보다 먼저여야 한다.
  //   resetInit() 이 RTC 백업 레지스터로 리셋 카운트/부트 모드를 읽는다.
  rtcInit();
  resetInit();

  faultInit();
  assertInit();
  gpioInit();

  //-- qspiInit() 이 flashInit() 보다 먼저여야 한다.
  //   flash.c 가 0x90000000 대 주소를 qspi.c 로 넘기기 때문이다.
#ifdef _USE_HW_QSPI
  qspiInit();
#endif
  flashInit();

#ifdef _USE_HW_SPI
  spiInit();
#endif
#ifdef _USE_HW_LCD
  lcdInit();
#endif

  //-- usbInit() 은 여기서 부르지 않는다.
  //   bootUp() 이 "앱으로 점프하지 않는다"고 판단한 뒤 apInit() 에서 연다.
  //   그렇지 않으면 정상 부팅마다 호스트에 USB 장치가 나타났다 사라진다.

  logBoot(false);

  return true;
}

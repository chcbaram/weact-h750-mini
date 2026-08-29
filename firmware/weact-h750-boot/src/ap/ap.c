#include "ap.h"


//-- bootUp() 이 정한 USB 구성. usbInit() 이 이 값으로 디스크립터를 고른다. (5단계)
bool boot_with_msc = false;
int  boot_reason   = 2;    // UiReason_t. bootUp() 이 정한다


static void bootUp(void);
static void updateLED(void);
static void update(void const *arg);




void apInit(void)
{
#if defined(_USE_HW_QSPI) && defined(QSPI_SELF_TEST)
  qspiSelfTest();
#endif

  bootInit();

#ifdef BOOT_SELF_TEST
  bootSelfTest();
#endif

  //-- 여기서 앱으로 점프하면 아래는 실행되지 않는다.
  bootUp();

#ifdef _USE_HW_LCD
  uiSetReason((UiReason_t)boot_reason, boot_with_msc);
#endif

  /*
   * USB 는 여기서 연다. hwInit() 이 아니다.
   *
   * bootUp() 이 "앱으로 점프한다"고 판단하면 이 줄까지 오지 않으므로, 정상
   * 부팅에서는 USB 가 아예 열리지 않는다. 그래야 호스트에 장치가 나타났다
   * 사라지는 일이 없다.
   */
#ifdef _USE_HW_USB
  uf2Init();
  //-- MSC 볼륨은 더블탭 등으로 UF2 가 필요할 때만 노출한다.
  uf2DiskSetMedium(boot_with_msc);

  usbInit(boot_with_msc ? USB_MSC_MODE : USB_CDC_MODE);
  cdcInit();
#endif
#ifdef _USE_HW_CMD
  cmdTaskInit();
#endif

  moduleInit();
}

/*
 * 부팅 판정.
 *
 *  정상 리셋      : 유효한 이미지가 있으면 **USB 를 열지 않고** 바로 점프한다.
 *                   그래야 매 부팅마다 호스트에 장치가 나타났다 사라지지 않는다.
 *  더블클릭 리셋  : 부트로더에 머무르고 MSC(UF2)까지 연다.
 *  앱이 요청      : MODE_BIT_BOOT. MODE_BIT_MSC 로 MSC 를 열지 말지 앱이 고른다.
 *  이미지 없음    : 머무른다. MSC 도 연다 - 복구 수단이 있어야 하기 때문이다.
 *
 * 이 함수가 리턴하면 "부트로더에 머무른다"는 뜻이고, usbInit() 은 그 뒤에 부른다.
 */
void bootUp(void)
{
  BootImgType_t img  = bootGetImgType();
  uint32_t      mode = resetGetBootMode();
  bool          stay = false;
  bool          with_msc = false;
  const char   *reason = "";

  int ui_reason = 2;    // UI_REASON_NO_FIRM

  if (mode & (1<<MODE_BIT_BOOT))
  {
    stay      = true;
    with_msc  = (mode & (1<<MODE_BIT_MSC)) ? true : false;
    reason    = "app request";
    ui_reason = 1;      // UI_REASON_REQUEST
  }

  if (resetGetCount() >= HW_RESET_DBLCLK_CNT)
  {
    stay      = true;
    with_msc  = true;                 // 더블클릭은 항상 UF2 까지 연다
    reason    = "double reset";
    ui_reason = 0;      // UI_REASON_DBLCLK
  }

  if (img == BOOT_IMG_NONE)
  {
    stay      = true;
    with_msc  = true;                 // 복구 수단이 필요하다
    reason    = "no firmware";
    ui_reason = 2;      // UI_REASON_NO_FIRM
  }

  logPrintf("[  ] bootUp()\n");
  logPrintf("     image : %s\n", (const char *[]){"NONE","RAW","VER","TAG"}[img]);

  if (stay)
  {
    logPrintf("     stay  : %s (msc %s)\n", reason, with_msc ? "on":"off");
    boot_with_msc = with_msc;
    boot_reason   = ui_reason;
    return;
  }

  //-- 태그가 없는 이미지는 크기를 알 수 있을 때 태그를 만들어 둔다.
  //   다음 부팅부터 CRC 검증을 받게 된다.
#if HW_BOOT_AUTO_TAG > 0
  if (img == BOOT_IMG_VER)
  {
    bootPromoteTag();
  }
#endif

  if (img == BOOT_IMG_RAW)
  {
    logPrintf("     [!!] no tag, no version - jumping anyway\n");
  }

  bootJumpFirm();

  //-- 점프에 실패했다. 머무른다.
  logPrintf("     [E_] jump failed - stay in boot\n");
  boot_with_msc = true;
}

void apMain(void)
{
  while (1)
  {
    moduleUpdate();
  }
}


/*
 * 부트로더는 100ms 로 빠르게 점멸한다.
 * 앱(500ms)과 눈으로 바로 구분하기 위한 것이다 - 지금 어느 쪽이 돌고 있는지
 * USB 나 로그 없이도 알 수 있어야 한다.
 */
void updateLED(void)
{
  static uint32_t pre_time = 0;

  if (millis() - pre_time >= HW_LED_TOGGLE_MS)
  {
    pre_time = millis();
    ledToggle(_DEF_LED1);
  }
}

void update(void const *arg)
{
  (void)arg;

  updateLED();

#ifdef _USE_HW_USB
  usbUpdate();
  uf2Update();
#endif
#ifdef _USE_HW_CMD
  cmdTaskUpdate();
#endif
}

/*
 * delay() 안에서 불린다. 블로킹 구간에서도 모듈(=USB, CLI 등)이 계속 돌게 한다.
 * cliMgrEnable(false) 로 CLI 재진입을 막지 않으면 CLI 명령 안에서 delay() 를
 * 부를 때 CLI 가 자기 자신을 다시 파싱한다.
 */
void cliLoopIdle(void)
{
  cliMgrEnable(false);
  moduleUpdate();
  cliMgrEnable(true);
}


MODULE_DEF(ap)
{
  .name     = "ap",
  .priority = MODULE_PRI_LOW,
  .update   = update,
};

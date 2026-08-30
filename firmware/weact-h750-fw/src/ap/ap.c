#include "ap.h"


static void updateLED(void);
static void update(void const *arg);
static void confirmBoot(void);




/*
 * 앱 초기화.
 *
 * 부트로더와 달리 판정 로직이 없다. 여기까지 왔다는 것은 부트로더가 이미
 * 이미지를 검증하고(TAG/VER/RAW) 점프했다는 뜻이다.
 *
 * 부트로더가 넘겨준 상태(클럭, 캐시, MPU, QUADSPI memory-mapped)는 그대로
 * 물려받는다. 다시 잡지 않는다. 12 문서가 그 규약의 단일 출처다.
 */
void apInit(void)
{
#ifdef _USE_HW_USB
  /*
   * 앱은 CDC + HID 만 연다. MSC(UF2)는 부트로더 전용이다.
   *
   * UF2 드래그앤드롭으로 굽는 것은 QUADSPI 를 indirect 로 내려야 하는 작업이라
   * XiP 로 도는 앱에서는 원천적으로 불가능하다. 앱이 할 수 있는 것은
   * resetToBoot() 으로 부트로더에 요청하는 것뿐이다 (cmd_boot.c 의 FW_UPDATE).
   */
  usbInit(USB_CDC_MODE);
  cdcInit();
#endif
#ifdef _USE_HW_CMD
  cmdTaskInit();
#endif

  moduleInit();
}

void apMain(void)
{
  while (1)
  {
    moduleUpdate();
  }
}


/*
 * 앱은 500ms 로 점멸한다.
 * 부트로더(100ms)와 눈으로 바로 구분하기 위한 것이다 - 지금 어느 쪽이 돌고
 * 있는지 USB 나 로그 없이도 알 수 있어야 한다.
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

/*
 * 부팅 확인.
 *
 * 부트로더의 HW_BOOT_TRY_MAX 는 "점프 직전에 카운터를 올리고, 앱이 정상 동작을
 * 확인하면 0 으로 되돌린다" 방식이다. 앱이 이 함수를 부르지 않으면 정상 앱도
 * 몇 번 부팅한 뒤 막히므로, 부트로더에서 기본값이 0(비활성)이다.
 *
 * 여기서 확인 신호를 보내므로 부트로더 hw_def.h 의 HW_BOOT_TRY_MAX 를 켤 수 있다
 * (07 문서). 아두이노 코어는 이 호출이 없어서 켤 수 없다.
 *
 * 3초를 기다리는 이유: USB 열거와 모듈 초기화가 끝나고도 살아 있어야 "정상
 * 동작" 이라고 말할 수 있다. 그전에 죽는 이미지를 통과시키면 의미가 없다.
 */
void confirmBoot(void)
{
  static bool is_done = false;

  if (is_done == true) return;
  if (millis() < HW_BOOT_CONFIRM_MS) return;

  resetConfirmBoot();
  is_done = true;
  logPrintf("[  ] boot confirmed (%d ms)\n", (int)millis());
}

void update(void const *arg)
{
  (void)arg;

  updateLED();
  confirmBoot();

#ifdef _USE_HW_USB
  usbUpdate();
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

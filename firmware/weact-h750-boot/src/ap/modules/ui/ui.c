#include "ui/ui.h"


#ifdef _USE_HW_LCD

static bool       is_init   = false;
static bool       cmd_busy  = false;     // cmd 프로토콜(HID/CDC) 쪽 진행 상태
static uint8_t    cmd_pct   = 0;
static UiReason_t ui_reason = UI_REASON_NO_FIRM;
static bool       ui_msc    = false;
static const char *ui_error = NULL;    // 점프 실패 사유. NULL 이면 정상

static void uiUpdate(void const *arg);
static void uiDrawIdle(void);
static void uiDrawProgress(uint8_t percent);




bool uiInit(void)
{
  is_init = true;

  //-- 백라이트는 그린 뒤에 켠다. 먼저 켜면 초기화 안 된 프레임버퍼가 번쩍인다.
  uiDrawIdle();
  lcdUpdateDraw();
  lcdSetBackLight(100);
  return true;
}

void uiSetReason(UiReason_t reason, bool with_msc)
{
  ui_reason = reason;
  ui_msc    = with_msc;
}

void uiSetProgress(uint8_t percent)
{
  cmd_busy = true;
  cmd_pct  = percent > 100 ? 100 : percent;
}

void uiEndProgress(void)
{
  cmd_busy = false;
}

void uiSetError(const char *msg)
{
  ui_error = msg;
}

void uiShowJump(uint32_t entry_addr)
{
  const char   *img_str[] = {"NONE", "RAW", "VER", "TAG"};
  BootImgType_t img = bootGetImgType();
  firm_ver_t    ver;

  lcdClearBuffer(black);

  lcdSetFont(LCD_FONT_11x18);
  lcdPrintf(4, 2, green, "RUN APP");

  lcdSetFont(LCD_FONT_07x10);

  /*
   * 어느 앱이 떴는지 보여준다. 이게 이 화면의 실질적인 값이다 - 이미지를 몇 개씩
   * 번갈아 구울 때 지금 도는 게 뭔지 눈으로 바로 확인된다.
   *
   * name_str/version_str 은 char[32] 라 꽉 채우면 널 종단이 없다. %.20s 로 자른다
   * (07x10 폰트에서 160px 에 22글자가 들어간다).
   *
   * bootGetVer() 는 indirect 로 읽는다. 이 시점에는 XiP 가 꺼져 있어야 하는데,
   * bootJumpFirm() 이 이미 qspiSetXipMode(false) 를 해둔 뒤에 부르므로 성립한다.
   */
  if (bootGetVer(&ver) == true)
  {
    lcdPrintf(4, 26, white, "%.20s", ver.name_str);
    lcdPrintf(4, 38, gray,  "%.20s", ver.version_str);
  }
  else
  {
    lcdPrintf(4, 26, yellow, "NO VERSION INFO");
  }

  lcdPrintf(4, 52, green, "FW:%s 0x%08X", img_str[img], (unsigned int)entry_addr);

  lcdDrawFillRect(0, HW_LCD_HEIGHT-3, HW_LCD_WIDTH, 3, green);

  //-- 동기로 그린다. lcdRequestDraw() 는 DMA 를 걸어두기만 하므로 여기서
  //   전송 완료를 기다려야 한다. bspDeInit() 이 지나면 완료 인터럽트가 없다.
  lcdUpdateDraw();
}

/*
 * 그리기는 **메인 루프에서만** 한다.
 *
 * USB 콜백(tud_msc_write10_cb 등) 안에서 그리면 SPI DMA 를 기다리는 동안 USB
 * 스택이 멈춰 호스트가 전송을 실패로 본다. 상태만 읽어와서 여기서 그린다.
 */
void uiUpdate(void const *arg)
{
  static uint32_t pre_time = 0;
  static bool     was_busy = false;
  static uint8_t  pre_pct  = 0xFF;

  (void)arg;

  if (is_init != true) return;

  //-- 10Hz 로 제한한다. 매 루프 그리면 SPI 가 USB/플래시 시간을 다 먹는다.
  if (millis() - pre_time < 100) return;
  pre_time = millis();

  if (lcdDrawAvailable() != true) return;

  {
    //-- UF2(MSC)와 cmd(HID/CDC) 두 경로 중 진행 중인 쪽을 쓴다.
    bool    busy = uf2IsBusy() || cmd_busy;
    uint8_t pct  = uf2IsBusy() ? uf2GetPercent() : cmd_pct;

    if (busy)
    {
      //-- 진행률이 바뀔 때만 다시 그린다.
      if (pct != pre_pct)
      {
        pre_pct  = pct;
        was_busy = true;
        uiDrawProgress(pct);
      }
      return;
    }

    if (was_busy)
    {
      //-- 방금 끝났다. 결과 화면으로 되돌린다.
      was_busy = false;
      pre_pct  = 0xFF;
      uiDrawIdle();
    }
  }
}

void uiDrawIdle(void)
{
  const char   *reason_str[] = {"DOUBLE RESET", "APP REQUEST", "NO FIRMWARE", "FAULT LOOP"};
  const char   *img_str[]    = {"NO FIRMWARE", "FW: RAW", "FW: VER", "FW: TAG"};
  BootImgType_t img = bootGetImgType();
  bool          err = (ui_error != NULL);

  lcdClearBuffer(black);

  lcdSetFont(LCD_FONT_11x18);
  lcdPrintf(4, 2, err ? red : green, "%s", err ? "JUMP FAIL" : "H750 BOOT");

  lcdSetFont(LCD_FONT_07x10);
  if (err)
  {
    //-- 왜 못 갔는지, 그리고 지금 뭘 하면 되는지.
    lcdPrintf(4, 24, red,    "%.22s", ui_error);
    lcdPrintf(4, 36, gray,   "%s", img_str[img]);
    lcdPrintf(4, 52, yellow, "%s", ui_msc ? "DROP .UF2 TO FIX" : "WAITING UPDATE");
  }
  else
  {
    lcdPrintf(4, 24, white, "%s", _DEF_FIRMWATRE_VERSION);
    lcdPrintf(4, 36, ui_reason == UI_REASON_FAULT ? red : gray, "%s", reason_str[ui_reason]);
    lcdPrintf(4, 48, ui_msc ? yellow : gray, "USB CDC+HID%s", ui_msc ? "+MSC" : "");
    lcdPrintf(4, 60, img == BOOT_IMG_NONE ? red : green, "%s", img_str[img]);
  }

  lcdDrawFillRect(0, HW_LCD_HEIGHT-3, HW_LCD_WIDTH, 3, err ? red : blue);
  lcdRequestDraw();
}

void uiDrawProgress(uint8_t percent)
{
  const int16_t bar_x = 6;
  const int16_t bar_y = 44;
  const int16_t bar_w = HW_LCD_WIDTH - 12;
  const int16_t bar_h = 16;
  int16_t fill;

  if (percent > 100) percent = 100;
  fill = (int16_t)(((int32_t)(bar_w - 4) * percent) / 100);

  lcdClearBuffer(black);

  /*
   * 라벨과 퍼센트를 같은 크기(11x18)로 한 줄에 놓는다.
   *
   *   "UPDATING" 8글자 x 11 = 88px  -> x=4 에서 92 까지
   *   "%3d%%"    4글자 x 11 = 44px  -> 오른쪽 정렬로 x=112
   * 서로 겹치지 않는다.
   */
  lcdSetFont(LCD_FONT_11x18);
  lcdPrintf(4, 4, yellow, "UPDATING");
  lcdPrintf(HW_LCD_WIDTH - 4*11 - 4, 4, white, "%3d%%", percent);

  //-- 진행률 바
  lcdDrawRoundRect(bar_x, bar_y, bar_w, bar_h, 3, white);
  if (fill > 0)
  {
    lcdDrawFillRect(bar_x + 2, bar_y + 2, fill, bar_h - 4, green);
  }

  lcdRequestDraw();
}


MODULE_DEF(ui)
{
  .name     = "ui",
  .priority = MODULE_PRI_LOW,
  .init     = uiInit,
  .update   = uiUpdate,
};

#endif

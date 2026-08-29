#include "ui/ui.h"


#ifdef _USE_HW_LCD

static bool       is_init   = false;
static bool       cmd_busy  = false;     // cmd 프로토콜(HID/CDC) 쪽 진행 상태
static uint8_t    cmd_pct   = 0;
static UiReason_t ui_reason = UI_REASON_NO_FIRM;
static bool       ui_msc    = false;

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
  const char   *reason_str[] = {"DOUBLE RESET", "APP REQUEST", "NO FIRMWARE"};
  const char   *img_str[]    = {"NO FIRMWARE", "FW: RAW", "FW: VER", "FW: TAG"};
  BootImgType_t img = bootGetImgType();

  lcdClearBuffer(black);

  lcdSetFont(LCD_FONT_11x18);
  lcdPrintf(4, 2, green, "H750 BOOT");

  lcdSetFont(LCD_FONT_07x10);
  lcdPrintf(4, 24, white, "%s", _DEF_FIRMWATRE_VERSION);
  lcdPrintf(4, 36, gray,  "%s", reason_str[ui_reason]);
  lcdPrintf(4, 48, ui_msc ? yellow : gray, "USB CDC+HID%s", ui_msc ? "+MSC" : "");
  lcdPrintf(4, 60, img == BOOT_IMG_NONE ? red : green, "%s", img_str[img]);

  lcdDrawFillRect(0, HW_LCD_HEIGHT-3, HW_LCD_WIDTH, 3, blue);
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

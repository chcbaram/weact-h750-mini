#ifndef UI_H_
#define UI_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "ap_def.h"


#ifdef _USE_HW_LCD

typedef enum
{
  UI_REASON_DBLCLK = 0,     // 리셋 더블클릭
  UI_REASON_REQUEST,        // 앱이 요청
  UI_REASON_NO_FIRM,        // 유효한 펌웨어 없음
} UiReason_t;


bool uiInit(void);
void uiSetReason(UiReason_t reason, bool with_msc);

/*
 * 진행률 표시. 두 경로가 이걸 먹인다.
 *   UF2(MSC)      : ui.c 가 uf2IsBusy()/uf2GetPercent() 를 폴링한다
 *   cmd(HID/CDC)  : cmd_boot.c 가 FW_WRITE 마다 uiSetProgress() 를 부른다
 * 어느 쪽이든 그리기는 메인 루프에서만 일어난다.
 */
void uiSetProgress(uint8_t percent);
void uiEndProgress(void);

#endif

#ifdef __cplusplus
}
#endif

#endif

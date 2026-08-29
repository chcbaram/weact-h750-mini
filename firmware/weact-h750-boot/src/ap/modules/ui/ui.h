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

/*
 * 앱으로 점프하기 직전 화면.
 *
 * 정상 부팅에서는 uiInit() 이 아예 불리지 않는다 (bootUp() 이 moduleInit() 앞에서
 * 점프해 버린다). 그래서 지금까지는 lcdInit() 이 만든 **검은 화면이 켜진 채로**
 * 앱에 넘어갔다. 이 함수가 그 자리를 채운다.
 *
 * 반드시 bspDeInit() **앞에서** 부를 것. 그 뒤에는 NVIC 가 전부 마스크되어
 * SPI DMA 완료 인터럽트가 오지 않아 전송이 끝나지 않는다.
 */
void uiShowJump(uint32_t entry_addr);

/*
 * 점프 실패를 기록한다. 실제 표시는 uiDrawIdle() 이 한다.
 *
 * 여기서 직접 그리면 안 된다. 점프에 실패하면 부트로더에 머무르고, 그 뒤
 * moduleInit() -> uiInit() -> uiDrawIdle() 이 화면을 덮어쓰기 때문이다.
 * msg 는 문자열 리터럴만 준다 (포인터만 보관한다).
 */
void uiSetError(const char *msg);

#endif

#ifdef __cplusplus
}
#endif

#endif

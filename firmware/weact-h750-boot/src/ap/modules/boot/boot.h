#ifndef BOOT_H_
#define BOOT_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "hw.h"


/*
 * 펌웨어 이미지 식별 단계.
 *
 * CRC 는 자기 이미지 안에 넣을 수 없지만(닭-달걀) 크기는 링커가 링크 시점에 안다.
 * 이 비대칭을 이용해 세 단계로 우아하게 낮아진다.
 *
 *   TAG : 0x90000000 에 "TAG " 매직. fw_size/fw_crc 로 전체 CRC 검증. 가장 강하다.
 *   VER : TAG 는 없지만 0x90001400 에 "VER " 매직 + firm_size 가 있다.
 *         크기를 아니 부트로더가 CRC 를 계산해 TAG 를 만들어 넣고 승격시킨다.
 *   RAW : 둘 다 없다. 벡터 테이블만 검사하고 경고와 함께 점프한다.
 *         (아두이노 코어 등 아무 정보도 안 남기는 빌드)
 */
typedef enum
{
  BOOT_IMG_NONE = 0,    // 유효한 이미지가 없다
  BOOT_IMG_RAW,         // 벡터 테이블만 그럴듯하다
  BOOT_IMG_VER,         // firm_ver_t 가 크기를 신고했다
  BOOT_IMG_TAG,         // CRC 검증까지 통과했다
} BootImgType_t;


bool          bootInit(void);
#ifdef BOOT_SELF_TEST
void          bootSelfTest(void);   // 브링업용. 3단계 판정 검증
#endif
BootImgType_t bootVerifyFirm(void);
BootImgType_t bootGetImgType(void);
uint16_t      bootJumpFirm(void);
bool          bootPromoteTag(void);
bool          bootGetVer(firm_ver_t *p_ver);
bool          bootGetTag(firm_tag_t *p_tag);


#ifdef __cplusplus
}
#endif

#endif

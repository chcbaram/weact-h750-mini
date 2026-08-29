/*
 * han.c
 *
 *  Created on: 2020. 12. 27.
 *      Author: baram
 */



#include "han.h"
#include "font_eng.h"

/*
 * HW_LCD_HANGUL 로 한글 글리프 테이블을 뺄 수 있게 했다.
 *
 *   font_han.h 의 K_font(23KB) + wWanToJohabTable(9.4KB) = 약 32KB.
 *   부트로더는 내부 플래시 128KB 안에 USB/UF2 까지 넣어야 해서 이 크기를 감당할 수 없다.
 *   ASCII(font_eng.h, 6KB)는 그대로 두므로 LCD_FONT_HAN 으로도 영문은 정상 출력된다.
 *   한글은 빈 사각형으로 그려서 "빠졌다"는 것이 화면에 드러나게 한다.
 */
#ifndef HW_LCD_HANGUL
#define HW_LCD_HANGUL   1
#endif

#if HW_LCD_HANGUL > 0
#include "font_han.h"
#endif


void hanEngFontLoad(char *HanCode, han_font_t *FontPtr);
void hanWanFontLoad(char *HanCode, han_font_t *FontPtr);
void hanUniFontLoad(char *HanCode, han_font_t *FontPtr);
uint16_t hanCnvCodeWan2Johab(uint16_t WanCode);




uint16_t hanFontLoad(char *HanCode, han_font_t *FontPtr )
{
  // 버퍼 초기화
  memset(FontPtr->FontBuffer, 0x00, 32);


  FontPtr->Code_Type = PHAN_NULL_CODE;
  // 한글코드인지 감별
  //
  if( !HanCode[0] || HanCode[0] == 0x0A )   // 문자열 마지막
  {
    FontPtr->Code_Type = PHAN_END_CODE;
    FontPtr->Size_Char = 1;
    return PHAN_END_CODE;
  }
  else if( HanCode[0] & 0x80 )              // 한글 코드인경우
  {
    uint32_t utf8_code;

    utf8_code = ((uint8_t)HanCode[0]<<16) | ((uint8_t)HanCode[1]<<8) | ((uint8_t)HanCode[2]<<0);

    if (utf8_code >= 0xEAB080 && utf8_code <= 0xED9FB0)
    {
      FontPtr->Code_Type = PHAN_HANGUL_CODE;
      FontPtr->Size_Char = 3;
      hanUniFontLoad(HanCode, FontPtr);
    }
    else
    {
      FontPtr->Code_Type = PHAN_HANGUL_CODE;
      FontPtr->Size_Char = 2;
      hanWanFontLoad(HanCode, FontPtr );
    }
    return PHAN_HANGUL_CODE;
  }
  else                                      // 영문 코드
  {
    FontPtr->Code_Type = PHAN_ENG_CODE;
    FontPtr->Size_Char = 1;
    hanEngFontLoad(HanCode, FontPtr);
    return PHAN_ENG_CODE;
  }


  return FontPtr->Code_Type;

}

#if HW_LCD_HANGUL > 0
void hanWanFontLoad(char *HanCode, han_font_t *FontPtr )   /* 한글 일반 폰트 생성 */
{
  uint16_t i;
  uint16_t wHanCode;
  //static declaration 은 속도를 높이기 위한것임.
  static uint16_t uChosung, uJoongsung, uJongsung, uChoType, uJooType,uJonType;

  wHanCode = (uint16_t)HanCode[0]<<8  | ((uint16_t)HanCode[1] & 0x00FF);


  wHanCode = hanCnvCodeWan2Johab(wHanCode);

  //seperate phoneme code
  uChosung   = (wHanCode>>10)&0x001F;//Chosung code
  uJoongsung = (wHanCode>>5) &0x001F;//Joongsung code
  uJongsung  = wHanCode & 0x001F;    //Jongsung code

  //make font index
  uChosung   = ChoIdxTbl[uChosung];    //Chosung index
  uJoongsung = JooIdxTbl[uJoongsung];  //Joongsung index
  uJongsung  = JonIdxTbl[uJongsung];   //Jongsung index

  //decide a character type (몇번째 벌을 사용할지 결정)
  uChoType = uJongsung ? ChoTypeCaseJongYes[uJoongsung]:ChoTypeCaseJongNo [uJoongsung];
    //'ㄱ'(1) 이나 'ㅋ'(16) 인경우는
  uJooType = ((uChosung == 0 || uChosung == 1 ||uChosung == 16 ) ? 0: 1) + (uJongsung ? 2: 0);
  uJonType = JonType[uJoongsung];

  for(i = 0; i<32; i++)
  {
    FontPtr->FontBuffer[i]  = K_font[uChoType*20+uChosung][i];
    FontPtr->FontBuffer[i] |= K_font[160 + uJooType*22+uJoongsung][i];
  }

  //combine Jongsung
  if(uJongsung)
  {
    for(i = 0; i < 32; i++)   FontPtr->FontBuffer[i] |= K_font[248 + uJonType*28+uJongsung][i];
  }
}
#else
void hanWanFontLoad(char *HanCode, han_font_t *FontPtr )
{
  (void)HanCode;
  //-- 글리프 테이블이 없다. 빠졌다는 것이 눈에 보이도록 빈 사각형을 그린다.
  FontPtr->FontBuffer[0]  = 0xFF; FontPtr->FontBuffer[1]  = 0xFF;
  for (int i=2; i<30; i+=2) { FontPtr->FontBuffer[i] = 0x80; FontPtr->FontBuffer[i+1] = 0x01; }
  FontPtr->FontBuffer[30] = 0xFF; FontPtr->FontBuffer[31] = 0xFF;
}
#endif

#if HW_LCD_HANGUL > 0
void hanUniFontLoad(char *HanCode, han_font_t *FontPtr)
{
  uint16_t i;
  uint16_t utf16;

  //static declaration 은 속도를 높이기 위한것임.
  static uint16_t uChosung, uJoongsung, uJongsung, uChoType, uJooType,uJonType;




  utf16 = (uint8_t)(HanCode[0] & 0x0f) << 12 | (uint8_t)(HanCode[1] & 0x3f) << 6 | (uint8_t)(HanCode[2] & 0x3f);


  //seperate phoneme code
  utf16 -= 0xac00;
  uJongsung  = utf16 % 28;
  utf16 /= 28;
  uJoongsung = utf16 % 21;
  uChosung   = utf16 / 21;


  //make font index
  uChosung   = UniChoIdxTbl[uChosung];    //Chosung index
  uJoongsung = UniJooIdxTbl[uJoongsung];  //Joongsung index
  uJongsung  = UniJonIdxTbl[uJongsung];   //Jongsung index


  //decide a character type (몇번째 벌을 사용할지 결정)
  uChoType = uJongsung ? ChoTypeCaseJongYes[uJoongsung]:ChoTypeCaseJongNo [uJoongsung];
    //'ㄱ'(1) 이나 'ㅋ'(16) 인경우는
  uJooType = ((uChosung == 0 || uChosung == 1 ||uChosung == 16 ) ? 0: 1) + (uJongsung ? 2: 0);
  uJonType = JonType[uJoongsung];

  for(i = 0; i<32; i++)
  {
    FontPtr->FontBuffer[i]  = (uint8_t)K_font[uChoType*20+uChosung][i];
    FontPtr->FontBuffer[i] |= (uint8_t)K_font[160 + uJooType*22+uJoongsung][i];
  }

  //combine Jongsung
  if(uJongsung)
  {
    for(i = 0; i < 32; i++)   FontPtr->FontBuffer[i] |= K_font[248 + uJonType*28+uJongsung][i];
  }
}
#else
void hanUniFontLoad(char *HanCode, han_font_t *FontPtr)
{
  (void)HanCode;
  //-- 글리프 테이블이 없다. 빠졌다는 것이 눈에 보이도록 빈 사각형을 그린다.
  FontPtr->FontBuffer[0]  = 0xFF; FontPtr->FontBuffer[1]  = 0xFF;
  for (int i=2; i<30; i+=2) { FontPtr->FontBuffer[i] = 0x80; FontPtr->FontBuffer[i+1] = 0x01; }
  FontPtr->FontBuffer[30] = 0xFF; FontPtr->FontBuffer[31] = 0xFF;
}
#endif

void hanEngFontLoad(char *HanCode, han_font_t *FontPtr)
{
  uint16_t i;
  char EngCode;

  EngCode = *HanCode;

  EngCode -= 0x20;  // FONT는 스페이스 부터 시작한다.

  for ( i = 0 ; i < 16 ; i++ )
  {
     FontPtr->FontBuffer[ i ] = (char)wEngFon[(int)EngCode][i];
  }
}

#if HW_LCD_HANGUL > 0
uint16_t hanCnvCodeWan2Johab(uint16_t WanCode)
{
  int index;
  uint16_t hcode, lcode;

  hcode = (WanCode >> 8) & 0xFF;
  lcode = WanCode & 0x0ff;

  index = (hcode - 0x0B0) * 94 + (lcode - 0x0A1);

  return wWanToJohabTable[index];
}
#endif

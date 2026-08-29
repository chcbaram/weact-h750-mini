#include "gpio.h"



#ifdef _USE_HW_GPIO
#include "cli.h"


#define NAME_DEF(x)  x, #x

typedef struct
{
  GPIO_TypeDef *port;
  uint32_t      pin;
  uint8_t       mode;
  GPIO_PinState on_state;
  GPIO_PinState off_state;
  bool          init_value;
  GpioPinName_t pin_name;
  const char   *p_name;
} gpio_tbl_t;

/*
 * on_state = gpioPinWrite(ch, true) 일 때 실제로 출력할 레벨.
 *
 * CS/DC 는 pass-through(true -> HIGH). st7735.c 가 명시적 레벨을 쓰기 때문이다.
 *
 * 백라이트(PE10)는 **active LOW** 다.
 *   회로도 06-TFT-LCD : SI2301(P채널) 소스가 3V3, 게이트가 R37 10K 로 3V3 에 풀업.
 *   P채널은 게이트를 소스보다 낮춰야 켜지므로 LCD_LED 를 LOW 로 당겨야 점등된다.
 *   on_state 를 RESET 으로 두면 lcdSetBackLight(100) -> gpioPinWrite(BL, true)
 *   -> 핀 LOW -> 점등 이 되어 상위 API 의미("true = 켜짐")가 유지된다.
 *
 *   주의 : chcbaram/stm32h750mv 의 gpio_tbl 에도 on_state 필드가 있지만 그 쪽
 *   gpioPinWrite() 는 on_state 를 무시하고 생짜 레벨을 쓴다. 그 테이블 값으로
 *   극성을 추론하면 안 된다. (그렇게 했다가 한 번 틀렸다)
 */
const gpio_tbl_t gpio_tbl[GPIO_MAX_CH] =
{
  {GPIOE, GPIO_PIN_11, _DEF_OUTPUT, GPIO_PIN_SET, GPIO_PIN_RESET, _DEF_HIGH, NAME_DEF(LCD_CS)},
  {GPIOE, GPIO_PIN_13, _DEF_OUTPUT, GPIO_PIN_SET, GPIO_PIN_RESET, _DEF_LOW,  NAME_DEF(LCD_DC)},
  {GPIOE, GPIO_PIN_10, _DEF_OUTPUT, GPIO_PIN_RESET, GPIO_PIN_SET, _DEF_LOW,  NAME_DEF(LCD_BL)},
  {GPIOC, GPIO_PIN_13, _DEF_INPUT,  GPIO_PIN_SET, GPIO_PIN_RESET, _DEF_LOW,  NAME_DEF(KEY_1)},
};


#ifdef _USE_HW_CLI
static void cliGpio(cli_args_t *args);
#endif



bool gpioInit(void)
{
  bool ret = true;

  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOE_CLK_ENABLE();


  for (int i=0; i<GPIO_MAX_CH; i++)
  {
    gpioPinMode(i, gpio_tbl[i].mode);
    gpioPinWrite(i, gpio_tbl[i].init_value);
  }

#ifdef _USE_HW_CLI
  cliAdd("gpio", cliGpio);
#endif

  return ret;
}

bool gpioPinMode(uint8_t ch, uint8_t mode)
{
  bool ret = true;
  GPIO_InitTypeDef GPIO_InitStruct = {0};


  if (ch >= GPIO_MAX_CH)
  {
    return false;
  }

  switch(mode)
  {
    case _DEF_INPUT:
      GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
      GPIO_InitStruct.Pull = GPIO_NOPULL;
      break;

    case _DEF_INPUT_PULLUP:
      GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
      GPIO_InitStruct.Pull = GPIO_PULLUP;
      break;

    case _DEF_INPUT_PULLDOWN:
      GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
      GPIO_InitStruct.Pull = GPIO_PULLDOWN;
      break;

    case _DEF_OUTPUT:
      GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
      GPIO_InitStruct.Pull = GPIO_NOPULL;
      break;

    case _DEF_OUTPUT_PULLUP:
      GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
      GPIO_InitStruct.Pull = GPIO_PULLUP;
      break;

    case _DEF_OUTPUT_PULLDOWN:
      GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
      GPIO_InitStruct.Pull = GPIO_PULLDOWN;
      break;
  }

  GPIO_InitStruct.Pin = gpio_tbl[ch].pin;
  HAL_GPIO_Init(gpio_tbl[ch].port, &GPIO_InitStruct);

  return ret;
}

void gpioPinWrite(uint8_t ch, bool value)
{
  if (ch >= GPIO_MAX_CH)
  {
    return;
  }

  if (value)
  {
    HAL_GPIO_WritePin(gpio_tbl[ch].port, gpio_tbl[ch].pin, gpio_tbl[ch].on_state);
  }
  else
  {
    HAL_GPIO_WritePin(gpio_tbl[ch].port, gpio_tbl[ch].pin, gpio_tbl[ch].off_state);
  }
}

bool gpioPinRead(uint8_t ch)
{
  bool ret = false;

  if (ch >= GPIO_MAX_CH)
  {
    return false;
  }

  if (HAL_GPIO_ReadPin(gpio_tbl[ch].port, gpio_tbl[ch].pin) == gpio_tbl[ch].on_state)
  {
    ret = true;
  }

  return ret;
}

void gpioPinToggle(uint8_t ch)
{
  if (ch >= GPIO_MAX_CH)
  {
    return;
  }

  HAL_GPIO_TogglePin(gpio_tbl[ch].port, gpio_tbl[ch].pin);
}





#ifdef _USE_HW_CLI
void cliGpio(cli_args_t *args)
{
  bool ret = false;


  if (args->argc == 1 && args->isStr(0, "info") == true)
  {
    for (int i=0; i<GPIO_MAX_CH; i++)
    {
      cliPrintf("%d %-16s - %d\n", i, gpio_tbl[i].p_name, gpioPinRead(i));
    }
    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "show") == true)
  {
    while(cliKeepLoop())
    {
      for (int i=0; i<GPIO_MAX_CH; i++)
      {        
        cliPrintf("%02d %-16s - %d\n", i, gpio_tbl[i].p_name, gpioPinRead(i));
      }      
      delay(100);
      cliMoveUp(GPIO_MAX_CH);
    }
    cliMoveDown(GPIO_MAX_CH);
    ret = true;
  }

  if (args->argc == 2 && args->isStr(0, "read") == true)
  {
    uint8_t ch;

    ch = (uint8_t)args->getData(1);

    while(cliKeepLoop())
    {
      cliPrintf("gpio read %d : %d\n", ch, gpioPinRead(ch));
      delay(100);
    }

    ret = true;
  }

  if (args->argc == 3 && args->isStr(0, "write") == true)
  {
    uint8_t ch;
    uint8_t data;

    ch   = (uint8_t)args->getData(1);
    data = (uint8_t)args->getData(2);

    gpioPinWrite(ch, data);

    cliPrintf("gpio write %d : %d\n", ch, data);
    ret = true;
  }

  if (ret != true)
  {
    cliPrintf("gpio info\n");
    cliPrintf("gpio show\n");
    cliPrintf("gpio read ch[0~%d]\n", GPIO_MAX_CH-1);
    cliPrintf("gpio write ch[0~%d] 0:1\n", GPIO_MAX_CH-1);
  }
}
#endif


#endif
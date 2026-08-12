#include "dac8564.h"
#include "app_config.h"
#include "main.h"

static SPI_HandleTypeDef *s_spi;
static uint32_t s_error_count;

static bool Transmit24(uint32_t value)
{
  uint8_t data[3];
  HAL_StatusTypeDef status;

  data[0] = (uint8_t)(value >> 16);
  data[1] = (uint8_t)(value >> 8);
  data[2] = (uint8_t)value;

  HAL_GPIO_WritePin(DAC_CS_N_GPIO_Port, DAC_CS_N_Pin, GPIO_PIN_RESET);
  status = HAL_SPI_Transmit(s_spi, data, sizeof(data), APP_DAC_SPI_TIMEOUT_MS);
  HAL_GPIO_WritePin(DAC_CS_N_GPIO_Port, DAC_CS_N_Pin, GPIO_PIN_SET);
  if (status != HAL_OK)
  {
    s_error_count++;
    return false;
  }
  return true;
}

bool DAC8564_Init(SPI_HandleTypeDef *spi)
{
  s_spi = spi;
  s_error_count = 0u;
  HAL_GPIO_WritePin(ADC_CS_N_GPIO_Port, ADC_CS_N_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(DAC_CS_N_GPIO_Port, DAC_CS_N_Pin, GPIO_PIN_SET);

  /* Keep the 2.5-V internal reference powered, independent of DAC state. */
  if (!Transmit24(0x011000u))
  {
    return false;
  }
  return DAC8564_WriteSafe();
}

bool DAC8564_WriteChannel(DAC8564_Channel channel, uint16_t code)
{
  uint8_t control;
  uint32_t frame;

  if ((uint32_t)channel >= DAC8564_CHANNEL_COUNT)
  {
    s_error_count++;
    return false;
  }

  /* A1/A0 = 00, LD1/LD0 = 01 (write and update selected channel). */
  control = (uint8_t)(0x10u | ((uint8_t)channel << 1));
  frame = ((uint32_t)control << 16) | code;
  return Transmit24(frame);
}

bool DAC8564_WriteAll(const uint16_t code[DAC8564_CHANNEL_COUNT])
{
  bool ok = true;

  if (code == NULL)
  {
    s_error_count++;
    return false;
  }

  for (uint32_t channel = 0u; channel < DAC8564_CHANNEL_COUNT; ++channel)
  {
    if (!DAC8564_WriteChannel((DAC8564_Channel)channel, code[channel]))
    {
      ok = false;
    }
  }
  return ok;
}

bool DAC8564_WriteSafe(void)
{
  const uint16_t safe[DAC8564_CHANNEL_COUNT] =
  {
    APP_DAC_SAFE_CODE, APP_DAC_SAFE_CODE,
    APP_DAC_SAFE_CODE, APP_DAC_SAFE_CODE
  };
  return DAC8564_WriteAll(safe);
}

uint32_t DAC8564_GetErrorCount(void)
{
  return s_error_count;
}

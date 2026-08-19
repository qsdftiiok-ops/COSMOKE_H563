#ifndef DAC8564_H
#define DAC8564_H

#include "stm32h5xx_hal.h"
#include <stdbool.h>
#include <stdint.h>

typedef enum
{
  DAC8564_CHANNEL_A_I1 = 0,
  DAC8564_CHANNEL_B_V1 = 1,
  DAC8564_CHANNEL_C_I2 = 2,
  DAC8564_CHANNEL_D_V2 = 3,
  DAC8564_CHANNEL_COUNT = 4
} DAC8564_Channel;

bool DAC8564_Init(SPI_HandleTypeDef *spi);
bool DAC8564_WriteChannel(DAC8564_Channel channel, uint16_t code);
bool DAC8564_WriteAll(const uint16_t code[DAC8564_CHANNEL_COUNT]);
bool DAC8564_WriteSafe(void);
bool DAC8564_IsHealthy(void);
bool DAC8564_HasFault(void);
uint32_t DAC8564_GetChannelErrors(void);
uint32_t DAC8564_GetFailedCycles(void);
uint32_t DAC8564_GetErrorCount(void);

#endif /* DAC8564_H */

#ifndef ADS131M08_H
#define ADS131M08_H

#include "stm32h5xx_hal.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ADS131M08_CHANNEL_COUNT   (8u)
#define ADS131M08_FRAME_WORDS     (10u)
#define ADS131M08_WORD_BYTES      (3u)
#define ADS131M08_FRAME_BYTES     (ADS131M08_FRAME_WORDS * ADS131M08_WORD_BYTES)

typedef struct
{
  uint32_t sequence;
  uint32_t timestamp_ms;
  uint16_t response_status;
  int32_t channel[ADS131M08_CHANNEL_COUNT];
  uint16_t received_crc;
  bool crc_ok;
} ADS131M08_Frame;

typedef struct
{
  uint32_t drdy_count;
  uint32_t completed_frames;
  uint32_t dropped_frames;
  uint32_t spi_errors;
  uint32_t crc_errors;
  uint16_t id_register;
  uint16_t mode_register;
  uint16_t clock_register;
  uint16_t gain1_register;
} ADS131M08_Diagnostics;

bool ADS131M08_Init(SPI_HandleTypeDef *spi);
void ADS131M08_Start(void);
void ADS131M08_Stop(void);
bool ADS131M08_ReadFrame(ADS131M08_Frame *frame);
void ADS131M08_GetDiagnostics(ADS131M08_Diagnostics *diagnostics);

void ADS131M08_OnDrdyInterrupt(void);
void ADS131M08_OnSpiCompleteInterrupt(SPI_HandleTypeDef *spi);
void ADS131M08_OnSpiErrorInterrupt(SPI_HandleTypeDef *spi);

bool ADS131M08_TryLockBus(void);
void ADS131M08_UnlockBus(void);
bool ADS131M08_IsOnline(void);

int32_t ADS131M08_Decode24(const uint8_t word[3]);
uint16_t ADS131M08_Crc16Ccitt(const uint8_t *data, size_t length);

#endif /* ADS131M08_H */

#ifndef TELEMETRY_H
#define TELEMETRY_H

#include "measurement.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TELEMETRY_BINARY_PACKET_SIZE (64u)
#define TELEMETRY_MAX_FRAME_SIZE     (256u)

enum
{
  TELEMETRY_FLAG_VALID          = (1u << 0),
  TELEMETRY_FLAG_AFE_FAULT      = (1u << 1),
  TELEMETRY_FLAG_ADC_CRC_ERROR  = (1u << 2),
  TELEMETRY_FLAG_ADC_FRAME_DROP = (1u << 3),
  TELEMETRY_FLAG_ADC_SPI_ERROR  = (1u << 4),
  TELEMETRY_FLAG_ADC_OFFLINE    = (1u << 5),
  TELEMETRY_FLAG_DAC_ERROR      = (1u << 6),
  TELEMETRY_FLAG_OUTPUT_CLIPPED = (1u << 7),
  TELEMETRY_FLAG_LAN_LINK_DOWN  = (1u << 8),
  TELEMETRY_FLAG_LAN_NO_ADDRESS = (1u << 9),
  TELEMETRY_FLAG_LAN_TX_ERROR   = (1u << 10),
  TELEMETRY_FLAG_LAN_FRAME_DROP = (1u << 11),
  TELEMETRY_FLAG_MUTED          = (1u << 12),
  TELEMETRY_FLAG_WARMUP         = (1u << 13)
};

typedef struct
{
  MeasurementResult measurement;
  uint16_t flags;
  uint32_t dropped_frames;
  uint32_t dropped_dma_busy;
  uint32_t dropped_ring_full;
  uint32_t dropped_spi_start;
  uint32_t dropped_spi_error;
  uint32_t dropped_bus_locked;
  uint32_t adc_crc_errors;
  uint32_t adc_spi_errors;
  uint32_t dac_errors;
  uint8_t system_state;
} TelemetrySnapshot;

void Telemetry_Init(void);
bool Telemetry_Send(const TelemetrySnapshot *snapshot);
bool Telemetry_SendFrame(const uint8_t *frame, size_t length);
size_t Telemetry_BuildFrame(const TelemetrySnapshot *snapshot,
                            uint8_t frame[TELEMETRY_MAX_FRAME_SIZE]);
size_t Telemetry_BuildBinaryPacket(const TelemetrySnapshot *snapshot,
                                   uint8_t packet[TELEMETRY_BINARY_PACKET_SIZE]);
uint16_t Telemetry_Crc16Ccitt(const uint8_t *data, size_t length);

#endif /* TELEMETRY_H */

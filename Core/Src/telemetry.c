#include "telemetry.h"
#include "app_config.h"
#include "main.h"
#include <stdio.h>
#include <string.h>

static void PutU16Le(uint8_t *buffer, uint16_t value)
{
  buffer[0] = (uint8_t)value;
  buffer[1] = (uint8_t)(value >> 8);
}

static void PutU32Le(uint8_t *buffer, uint32_t value)
{
  buffer[0] = (uint8_t)value;
  buffer[1] = (uint8_t)(value >> 8);
  buffer[2] = (uint8_t)(value >> 16);
  buffer[3] = (uint8_t)(value >> 24);
}

uint16_t Telemetry_Crc16Ccitt(const uint8_t *data, size_t length)
{
  uint16_t crc = 0xFFFFu;

  for (size_t i = 0; i < length; ++i)
  {
    crc ^= (uint16_t)data[i] << 8;
    for (uint32_t bit = 0u; bit < 8u; ++bit)
    {
      crc = ((crc & 0x8000u) != 0u) ?
            (uint16_t)((crc << 1) ^ 0x1021u) : (uint16_t)(crc << 1);
    }
  }
  return crc;
}

void Telemetry_Init(void)
{
#if (APP_TELEMETRY_UART_ENABLE != 0u)
  (void)BSP_COM_SelectLogPort(COM1);
#if (APP_TELEMETRY_FORMAT == APP_TELEMETRY_FORMAT_CSV)
  printf("seq,ms,flags,adc_status,i1_mA,v1_mV,i2_mA,v2_mV,"
         "raw_i1,raw_v1,raw_i2,raw_v2,dac_i1,dac_v1,dac_i2,dac_v2,"
         "dropped,dropped_dma_busy,dropped_ring_full,dropped_spi_start,"
         "dropped_spi_error,dropped_bus_locked,crc_error,spi_error,dac_error\r\n");
#endif
#endif
}

size_t Telemetry_BuildBinaryPacket(const TelemetrySnapshot *snapshot,
                                   uint8_t packet[TELEMETRY_BINARY_PACKET_SIZE])
{
  size_t offset = 0u;

  if (snapshot == NULL || packet == NULL)
  {
    return 0u;
  }

  memset(packet, 0, TELEMETRY_BINARY_PACKET_SIZE);
  PutU16Le(&packet[offset], 0xA55Au); offset += 2u;
  packet[offset++] = APP_TELEMETRY_BINARY_VERSION;
  packet[offset++] = APP_TELEMETRY_BINARY_TYPE_MEASUREMENT;
  PutU16Le(&packet[offset], TELEMETRY_BINARY_PACKET_SIZE); offset += 2u;
  PutU16Le(&packet[offset], snapshot->flags); offset += 2u;
  PutU32Le(&packet[offset], snapshot->measurement.sequence); offset += 4u;
  PutU32Le(&packet[offset], snapshot->measurement.timestamp_ms); offset += 4u;
  PutU16Le(&packet[offset], snapshot->measurement.adc_status); offset += 2u;
  PutU16Le(&packet[offset], (uint16_t)snapshot->adc_crc_errors); offset += 2u;

  for (uint32_t channel = 0u; channel < MEAS_CHANNEL_COUNT; ++channel)
  {
    PutU32Le(&packet[offset], (uint32_t)snapshot->measurement.raw[channel]);
    offset += 4u;
  }
  for (uint32_t channel = 0u; channel < MEAS_CHANNEL_COUNT; ++channel)
  {
    PutU32Le(&packet[offset],
             (uint32_t)Measurement_UnitsToMilli(
               snapshot->measurement.filtered[channel]));
    offset += 4u;
  }
  for (uint32_t channel = 0u; channel < MEAS_CHANNEL_COUNT; ++channel)
  {
    PutU16Le(&packet[offset], snapshot->measurement.dac_code[channel]);
    offset += 2u;
  }
  PutU16Le(&packet[offset], (uint16_t)snapshot->dropped_frames); offset += 2u;
  PutU16Le(&packet[offset], Telemetry_Crc16Ccitt(packet, offset)); offset += 2u;
  return offset;
}

#if (APP_TELEMETRY_FORMAT == APP_TELEMETRY_FORMAT_CSV)
static size_t BuildCsv(const TelemetrySnapshot *snapshot,
                       uint8_t frame[TELEMETRY_MAX_FRAME_SIZE])
{
  int length = snprintf(
    (char *)frame, TELEMETRY_MAX_FRAME_SIZE,
    "%lu,%lu,0x%04X,0x%04X,%ld,%ld,%ld,%ld,%ld,%ld,%ld,%ld,"
    "%u,%u,%u,%u,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu\r\n",
    (unsigned long)snapshot->measurement.sequence,
    (unsigned long)snapshot->measurement.timestamp_ms,
    snapshot->flags,
    snapshot->measurement.adc_status,
    (long)Measurement_UnitsToMilli(snapshot->measurement.filtered[0]),
    (long)Measurement_UnitsToMilli(snapshot->measurement.filtered[1]),
    (long)Measurement_UnitsToMilli(snapshot->measurement.filtered[2]),
    (long)Measurement_UnitsToMilli(snapshot->measurement.filtered[3]),
    (long)snapshot->measurement.raw[0],
    (long)snapshot->measurement.raw[1],
    (long)snapshot->measurement.raw[2],
    (long)snapshot->measurement.raw[3],
    snapshot->measurement.dac_code[0],
    snapshot->measurement.dac_code[1],
    snapshot->measurement.dac_code[2],
    snapshot->measurement.dac_code[3],
    (unsigned long)snapshot->dropped_frames,
    (unsigned long)snapshot->dropped_dma_busy,
    (unsigned long)snapshot->dropped_ring_full,
    (unsigned long)snapshot->dropped_spi_start,
    (unsigned long)snapshot->dropped_spi_error,
    (unsigned long)snapshot->dropped_bus_locked,
    (unsigned long)snapshot->adc_crc_errors,
    (unsigned long)snapshot->adc_spi_errors,
    (unsigned long)snapshot->dac_errors);

  if (length <= 0)
  {
    return 0u;
  }
  if ((size_t)length >= TELEMETRY_MAX_FRAME_SIZE)
  {
    return 0u;
  }
  return (size_t)length;
}
#endif

size_t Telemetry_BuildFrame(const TelemetrySnapshot *snapshot,
                            uint8_t frame[TELEMETRY_MAX_FRAME_SIZE])
{
  if (snapshot == NULL || frame == NULL)
  {
    return 0u;
  }

#if (APP_TELEMETRY_FORMAT == APP_TELEMETRY_FORMAT_BINARY)
  return Telemetry_BuildBinaryPacket(snapshot, frame);
#else
  return BuildCsv(snapshot, frame);
#endif
}

bool Telemetry_SendFrame(const uint8_t *frame, size_t length)
{
  if (frame == NULL || length == 0u || length > UINT16_MAX)
  {
    return false;
  }

#if (APP_TELEMETRY_UART_ENABLE != 0u)
  return HAL_UART_Transmit(&hcom_uart[COM1], (uint8_t *)frame,
                           (uint16_t)length,
                           APP_TELEMETRY_UART_TIMEOUT_MS) == HAL_OK;
#else
  return true;
#endif
}

bool Telemetry_Send(const TelemetrySnapshot *snapshot)
{
  uint8_t frame[TELEMETRY_MAX_FRAME_SIZE];
  size_t length;

  if (snapshot == NULL)
  {
    return false;
  }

  length = Telemetry_BuildFrame(snapshot, frame);
  return Telemetry_SendFrame(frame, length);
}

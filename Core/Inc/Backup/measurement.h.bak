#ifndef MEASUREMENT_H
#define MEASUREMENT_H

#include <stdbool.h>
#include <stdint.h>

typedef enum
{
  MEAS_CH1_CURRENT = 0,
  MEAS_CH1_VOLTAGE = 1,
  MEAS_CH2_CURRENT = 2,
  MEAS_CH2_VOLTAGE = 3,
  MEAS_CHANNEL_COUNT = 4
} MeasurementChannel;

typedef struct
{
  bool initialized;
  float filtered[MEAS_CHANNEL_COUNT];
} MeasurementFilterState;

typedef struct
{
  uint32_t sequence;
  uint32_t timestamp_ms;
  uint16_t adc_status;
  int32_t raw[MEAS_CHANNEL_COUNT];
  bool crc_ok;
} MeasurementRawFrame;

typedef struct
{
  uint32_t sequence;
  uint32_t timestamp_ms;
  uint16_t adc_status;
  int32_t raw[MEAS_CHANNEL_COUNT];
  float value[MEAS_CHANNEL_COUNT];
  float filtered[MEAS_CHANNEL_COUNT];
  uint16_t dac_code[MEAS_CHANNEL_COUNT];
  bool clipped;
} MeasurementResult;

void Measurement_Init(MeasurementFilterState *state);
bool Measurement_ProcessFrame(MeasurementFilterState *state,
                              const MeasurementRawFrame *frame,
                              MeasurementResult *result);
void Measurement_MakeDacCodes(MeasurementResult *result);
int32_t Measurement_UnitsToMilli(float value);

#endif /* MEASUREMENT_H */

#include "app_config.h"
#include "measurement.h"
#include <stdio.h>

static int s_failures;

static int32_t RawFromAdcVolts(float volts)
{
  float raw = volts * 8388608.0f * APP_ADC_PGA_GAIN / APP_ADC_REFERENCE_V;
  return (int32_t)(raw + ((raw >= 0.0f) ? 0.5f : -0.5f));
}

static void ExpectNear(const char *name, float actual, float expected, float tolerance)
{
  float error = actual - expected;
  if (error < 0.0f)
  {
    error = -error;
  }
  if (error > tolerance)
  {
    printf("FAIL %-28s actual=%f expected=%f\n", name, actual, expected);
    s_failures++;
  }
}

static void ExpectCode(const char *name, uint16_t actual, uint16_t expected)
{
  int difference = (int)actual - (int)expected;
  if (difference < 0)
  {
    difference = -difference;
  }
  if (difference > 2)
  {
    printf("FAIL %-28s actual=%u expected=%u\n", name, actual, expected);
    s_failures++;
  }
}

static void ExpectTrue(const char *name, int condition)
{
  if (!condition)
  {
    printf("FAIL %s\n", name);
    s_failures++;
  }
}

static MeasurementResult RunSingle(float i1, float v1, float i2, float v2)
{
  MeasurementFilterState state;
  MeasurementRawFrame frame = {0};
  MeasurementResult result = {0};

  Measurement_Init(&state);
  frame.crc_ok = true;
  frame.raw[0] = RawFromAdcVolts(i1 * APP_CH0_VOLTS_PER_UNIT);
  frame.raw[1] = RawFromAdcVolts(v1 * APP_CH1_VOLTS_PER_UNIT);
  frame.raw[2] = RawFromAdcVolts(i2 * APP_CH2_VOLTS_PER_UNIT);
  frame.raw[3] = RawFromAdcVolts(v2 * APP_CH3_VOLTS_PER_UNIT);
  if (!Measurement_ProcessFrame(&state, &frame, &result))
  {
    printf("FAIL Measurement_ProcessFrame rejected valid frame\n");
    s_failures++;
  }
  return result;
}

int main(void)
{
  MeasurementResult zero = RunSingle(0.0f, 0.0f, 0.0f, 0.0f);
  MeasurementResult positive = RunSingle(30.0f, 250.0f, 30.0f, 250.0f);
  MeasurementResult negative = RunSingle(-30.0f, -250.0f, -30.0f, -250.0f);

  for (unsigned channel = 0; channel < MEAS_CHANNEL_COUNT; ++channel)
  {
    ExpectNear("zero engineering value", zero.filtered[channel], 0.0f, 0.001f);
    ExpectCode("zero DAC code", zero.dac_code[channel], APP_DAC_CENTER_CODE);
    ExpectCode("positive full-scale DAC", positive.dac_code[channel], (uint16_t)(APP_DAC_CENTER_CODE + APP_DAC_SPAN_CODE));
    ExpectCode("negative full-scale DAC", negative.dac_code[channel], (uint16_t)(APP_DAC_CENTER_CODE - APP_DAC_SPAN_CODE));
  }
  ExpectNear("CH1 current +FS", positive.filtered[0], 30.0f, 0.002f);
  ExpectNear("CH1 voltage +FS", positive.filtered[1], 250.0f, 0.02f);
  ExpectNear("CH2 current -FS", negative.filtered[2], -30.0f, 0.002f);
  ExpectNear("CH2 voltage -FS", negative.filtered[3], -250.0f, 0.02f);

  if (Measurement_UnitsToMilli(1.2345f) != 1235)
  {
    printf("FAIL milli-unit rounding\n");
    s_failures++;
  }

  {
    MeasurementFilterState state;
    MeasurementRawFrame frame = {0};
    MeasurementResult result = {0};

    Measurement_Init(&state);
    frame.crc_ok = false;
    ExpectTrue("CRC-invalid frame rejected",
               !Measurement_ProcessFrame(&state, &frame, &result));
  }

  {
    MeasurementFilterState state;
    MeasurementRawFrame frame = {0};
    MeasurementResult first = {0};
    MeasurementResult second = {0};

    Measurement_Init(&state);
    frame.crc_ok = true;
    ExpectTrue("zero filter seed accepted",
               Measurement_ProcessFrame(&state, &frame, &first));
    frame.raw[0] = RawFromAdcVolts(10.0f * APP_CH0_VOLTS_PER_UNIT);
    ExpectTrue("IIR update accepted",
               Measurement_ProcessFrame(&state, &frame, &second));
    ExpectNear("IIR alpha applied", second.filtered[0],
               10.0f * APP_CH0_FILTER_ALPHA, 0.002f);
  }

  {
    MeasurementResult clipped = RunSingle(60.0f, 500.0f, 60.0f, 500.0f);
    ExpectTrue("over-range result clipped", clipped.clipped);
    for (unsigned channel = 0; channel < MEAS_CHANNEL_COUNT; ++channel)
    {
      ExpectCode("positive clip DAC", clipped.dac_code[channel], 58982u);
    }
  }

  if (s_failures == 0)
  {
    printf("PASS all measurement conversion tests\n");
  }
  return s_failures;
}

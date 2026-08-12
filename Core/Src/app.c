#include "app.h"
#include "ads131m08.h"
#include "app_config.h"
#include "dac8564.h"
#include "eth.h"
#include "main.h"
#include "measurement.h"
#include "app_netxduo.h"
#include "spi.h"
#include "telemetry.h"
#include <stdio.h>
#include <string.h>

static MeasurementFilterState s_filter;
static TelemetrySnapshot s_snapshot;
static bool s_adc_online;
static bool s_dac_online;
static bool s_have_measurement;
static uint32_t s_last_adc_retry_ms;
static uint32_t s_last_adc_progress_ms;
static uint32_t s_last_dac_update_ms;
static uint32_t s_last_telemetry_ms;
static uint32_t s_consecutive_crc_errors;

static bool TimeElapsed(uint32_t now, uint32_t then, uint32_t period)
{
  return (uint32_t)(now - then) >= period;
}

#if (APP_NETWORK_ENABLE != 0u)
/*
 * One boot-time MDIO report makes a PHY wiring/configuration fault visible
 * through the ST-LINK VCP.  A valid LAN8742 responds at exactly one address;
 * no line means the MCU cannot communicate with the PHY over MDIO.
 */
static void PrintPhyDiagnostics(void)
{
  for (uint32_t address = 0u; address < 32u; ++address)
  {
    uint32_t id1 = 0u;
    uint32_t id2 = 0u;
    uint32_t status = 0u;

    if (HAL_ETH_ReadPHYRegister(&heth, address, 2u, &id1) == HAL_OK &&
        HAL_ETH_ReadPHYRegister(&heth, address, 3u, &id2) == HAL_OK &&
        HAL_ETH_ReadPHYRegister(&heth, address, 31u, &status) == HAL_OK &&
        id1 != 0xFFFFu && id1 != 0u)
    {
      printf("# phy addr=%lu id=0x%04lX%04lX status=0x%04lX\r\n",
             (unsigned long)address, (unsigned long)id1,
             (unsigned long)id2, (unsigned long)status);
    }
  }
}
#endif

static void RefreshFlags(void)
{
  ADS131M08_Diagnostics diagnostics;
  NetXDuoStatus network_status;
  uint16_t flags = 0u;

  ADS131M08_GetDiagnostics(&diagnostics);
  s_snapshot.dropped_frames = diagnostics.dropped_frames;
  s_snapshot.adc_crc_errors = diagnostics.crc_errors;
  s_snapshot.adc_spi_errors = diagnostics.spi_errors;
  s_snapshot.dac_errors = DAC8564_GetErrorCount();

  if (s_have_measurement)
  {
    flags |= TELEMETRY_FLAG_VALID;
  }
  if (HAL_GPIO_ReadPin(SENSOR_FAULT_N_GPIO_Port, SENSOR_FAULT_N_Pin) == GPIO_PIN_RESET)
  {
    flags |= TELEMETRY_FLAG_AFE_FAULT;
  }
  if (diagnostics.crc_errors != 0u)
  {
    flags |= TELEMETRY_FLAG_ADC_CRC_ERROR;
  }
  if (diagnostics.dropped_frames != 0u)
  {
    flags |= TELEMETRY_FLAG_ADC_FRAME_DROP;
  }
  if (diagnostics.spi_errors != 0u)
  {
    flags |= TELEMETRY_FLAG_ADC_SPI_ERROR;
  }
  if (!s_adc_online)
  {
    flags |= TELEMETRY_FLAG_ADC_OFFLINE;
  }
  if (!s_dac_online || s_snapshot.dac_errors != 0u)
  {
    flags |= TELEMETRY_FLAG_DAC_ERROR;
  }
  if (s_snapshot.measurement.clipped)
  {
    flags |= TELEMETRY_FLAG_OUTPUT_CLIPPED;
  }

#if (APP_NETWORK_ENABLE != 0u)
  NetXDuo_GetStatus(&network_status);
  if (!network_status.link_up)
  {
    flags |= TELEMETRY_FLAG_LAN_LINK_DOWN;
  }
  if (!network_status.address_ready)
  {
    flags |= TELEMETRY_FLAG_LAN_NO_ADDRESS;
  }
  if (network_status.send_errors != 0u)
  {
    flags |= TELEMETRY_FLAG_LAN_TX_ERROR;
  }
  if (network_status.telemetry_drops != 0u)
  {
    flags |= TELEMETRY_FLAG_LAN_FRAME_DROP;
  }
#else
  (void)network_status;
#endif
  s_snapshot.flags = flags;
}

static void SetSafeDacCodes(void)
{
  for (uint32_t channel = 0u; channel < MEAS_CHANNEL_COUNT; ++channel)
  {
    s_snapshot.measurement.dac_code[channel] = APP_DAC_SAFE_CODE;
  }
}

static bool InitializeAdc(void)
{
  bool ok;

  HAL_NVIC_DisableIRQ(EXTI3_IRQn);
  ADS131M08_Stop();
  ok = ADS131M08_Init(&hspi1);
  __HAL_GPIO_EXTI_CLEAR_IT(ADC_DRDY_N_Pin);
  if (ok)
  {
    ADS131M08_Start();
  }
  HAL_NVIC_EnableIRQ(EXTI3_IRQn);
  return ok;
}

void App_Init(void)
{
  uint32_t now = HAL_GetTick();

  memset(&s_snapshot, 0, sizeof(s_snapshot));
  Measurement_Init(&s_filter);
  SetSafeDacCodes();

  s_dac_online = DAC8564_Init(&hspi1);
  s_adc_online = InitializeAdc();

  s_have_measurement = false;
  s_last_adc_retry_ms = now;
  s_last_adc_progress_ms = now;
  s_last_dac_update_ms = now;
  s_last_telemetry_ms = now;
  s_consecutive_crc_errors = 0u;
  Telemetry_Init();

#if (APP_NETWORK_ENABLE != 0u)
  {
    NetXDuoStatus network_status;
    NetXDuo_GetStatus(&network_status);
    printf("# network init=%u link=%u address=%u error=0x%08lX\r\n",
           network_status.initialized ? 1u : 0u,
           network_status.link_up ? 1u : 0u,
           network_status.address_ready ? 1u : 0u,
           (unsigned long)network_status.last_error);
    PrintPhyDiagnostics();
  }
#endif

  if (s_adc_online && s_dac_online)
  {
    BSP_LED_On(LED_GREEN);
    BSP_LED_Off(LED_RED);
  }
  else
  {
    BSP_LED_Off(LED_GREEN);
    BSP_LED_On(LED_RED);
  }
}

void App_Task(void)
{
  ADS131M08_Frame frame;
  uint32_t now = HAL_GetTick();

  if (!s_adc_online && TimeElapsed(now, s_last_adc_retry_ms, APP_ADC_RETRY_PERIOD_MS))
  {
    s_last_adc_retry_ms = now;
    s_adc_online = InitializeAdc();
    if (s_adc_online)
    {
      Measurement_Init(&s_filter);
      s_have_measurement = false;
      s_last_adc_progress_ms = now;
      s_consecutive_crc_errors = 0u;
      BSP_LED_On(LED_GREEN);
    }
  }

  while (s_adc_online && ADS131M08_ReadFrame(&frame))
  {
    MeasurementRawFrame raw_frame;
    MeasurementResult result;

    raw_frame.sequence = frame.sequence;
    raw_frame.timestamp_ms = frame.timestamp_ms;
    raw_frame.adc_status = frame.response_status;
    raw_frame.crc_ok = frame.crc_ok;
    s_last_adc_progress_ms = now;
    if (frame.crc_ok)
    {
      s_consecutive_crc_errors = 0u;
    }
    else
    {
      s_consecutive_crc_errors++;
    }
    for (uint32_t channel = 0u; channel < MEAS_CHANNEL_COUNT; ++channel)
    {
      raw_frame.raw[channel] = frame.channel[channel];
    }

    if (Measurement_ProcessFrame(&s_filter, &raw_frame, &result))
    {
      s_snapshot.measurement = result;
      s_have_measurement = true;
    }
  }

  if (s_adc_online)
  {
    ADS131M08_Diagnostics diagnostics;

    ADS131M08_GetDiagnostics(&diagnostics);
    if (diagnostics.spi_errors >= APP_ADC_MAX_SPI_ERRORS_PER_RUN ||
        s_consecutive_crc_errors >= APP_ADC_MAX_CONSECUTIVE_CRC_ERRORS ||
        TimeElapsed(now, s_last_adc_progress_ms,
                    APP_ADC_NO_PROGRESS_TIMEOUT_MS))
    {
      ADS131M08_Stop();
      s_adc_online = false;
      s_have_measurement = false;
      s_last_adc_retry_ms = now;
      SetSafeDacCodes();
      BSP_LED_Off(LED_GREEN);
    }
  }

  RefreshFlags();

  if (TimeElapsed(now, s_last_dac_update_ms, APP_DAC_UPDATE_PERIOD_MS))
  {
    s_last_dac_update_ms = now;
    if ((s_snapshot.flags & (TELEMETRY_FLAG_AFE_FAULT |
                             TELEMETRY_FLAG_ADC_OFFLINE)) != 0u ||
        !s_have_measurement)
    {
      SetSafeDacCodes();
    }

    if (ADS131M08_TryLockBus())
    {
      s_dac_online = DAC8564_WriteAll(s_snapshot.measurement.dac_code);
      ADS131M08_UnlockBus();
    }
  }

  if (TimeElapsed(now, s_last_telemetry_ms, APP_TELEMETRY_PERIOD_MS))
  {
    uint8_t frame[TELEMETRY_MAX_FRAME_SIZE];
    size_t length;

    s_last_telemetry_ms = now;
    RefreshFlags();
    length = Telemetry_BuildFrame(&s_snapshot, frame);
    if (length != 0u)
    {
#if (APP_NETWORK_ENABLE != 0u)
      (void)NetXDuo_PublishTelemetry(frame, length);
#endif
#if (APP_TELEMETRY_UART_ENABLE != 0u)
      (void)Telemetry_SendFrame(frame, length);
#endif
    }
    BSP_LED_Toggle(LED_YELLOW);
  }

  if ((s_snapshot.flags & (TELEMETRY_FLAG_AFE_FAULT |
                           TELEMETRY_FLAG_ADC_OFFLINE |
                           TELEMETRY_FLAG_DAC_ERROR)) != 0u)
  {
    BSP_LED_On(LED_RED);
  }
  else
  {
    BSP_LED_Off(LED_RED);
  }
}

void HAL_GPIO_EXTI_Falling_Callback(uint16_t GPIO_Pin)
{
  if (GPIO_Pin == ADC_DRDY_N_Pin)
  {
    ADS131M08_OnDrdyInterrupt();
  }
}

void HAL_SPI_TxRxCpltCallback(SPI_HandleTypeDef *spi)
{
  ADS131M08_OnSpiCompleteInterrupt(spi);
}

void HAL_SPI_ErrorCallback(SPI_HandleTypeDef *spi)
{
  ADS131M08_OnSpiErrorInterrupt(spi);
}

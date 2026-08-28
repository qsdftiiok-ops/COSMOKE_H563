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
#include "stm32h5xx_ll_rcc.h"
#include <stdio.h>
#include <string.h>

static MeasurementFilterState s_filter;
static TelemetrySnapshot s_snapshot;
static AppState s_app_state = APP_STATE_MUTED;
static AppHeartbeat s_heartbeat;
static bool s_adc_online;
static bool s_dac_online;
static bool s_have_measurement;
static uint32_t s_last_recovery_attempt_ms;
static uint32_t s_last_adc_progress_ms;
static uint32_t s_last_dac_update_ms;
static uint32_t s_last_telemetry_ms;
static uint32_t s_consecutive_crc_errors;
static uint32_t s_warmup_frames_count;
static uint32_t s_warmup_start_ms;

#if (APP_WATCHDOG_ENABLE != 0u)
typedef struct
{
  __IO uint32_t KR;
  __IO uint32_t PR;
  __IO uint32_t RLR;
  __IO uint32_t SR;
  __IO uint32_t WINR;
} AppIwdgRegs;

#define APP_IWDG ((AppIwdgRegs *)0x40003000UL)

static void Watchdog_Init(void)
{
  /* 1. Ensure LSI oscillator is enabled and ready */
  LL_RCC_LSI_Enable();
  while (LL_RCC_LSI_IsReady() == 0u)
  {
  }

  /* 2. Configure IWDG: LSI (~32 kHz), Prescaler 64 (~500 Hz / 2 ms tick), Reload 500 = 1000 ms */
  APP_IWDG->KR = 0x5555u;
  APP_IWDG->PR = 0x04u;
  APP_IWDG->RLR = 500u;

  /* Wait for register update flags in SR (PVU bit 0, RVU bit 1) to clear */
  while ((APP_IWDG->SR & 0x03u) != 0u)
  {
  }

  APP_IWDG->KR = 0xAAAAu;
  APP_IWDG->KR = 0xCCCCu;
}

static void Watchdog_Refresh(void)
{
  APP_IWDG->KR = 0xAAAAu;
}
#endif

static bool TimeElapsed(uint32_t now, uint32_t then, uint32_t period)
{
  return (uint32_t)(now - then) >= period;
}

static void Hardware_Mute_Assert(void)
{
  /* If dedicated hardware mute/clear GPIO is available on schematic, assert here */
}

static void Hardware_Mute_Release(void)
{
  /* If dedicated hardware mute/clear GPIO is available on schematic, release here */
}

static void SetSafeDacCodes(void)
{
  for (uint32_t channel = 0u; channel < MEAS_CHANNEL_COUNT; ++channel)
  {
    s_snapshot.measurement.dac_code[channel] = APP_DAC_SAFE_CODE;
  }
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
  s_snapshot.dropped_dma_busy = diagnostics.dropped_dma_busy;
  s_snapshot.dropped_ring_full = diagnostics.dropped_ring_full;
  s_snapshot.dropped_spi_start = diagnostics.dropped_spi_start;
  s_snapshot.dropped_spi_error = diagnostics.dropped_spi_error;
  s_snapshot.dropped_bus_locked = diagnostics.dropped_bus_locked;
  s_snapshot.adc_crc_errors = diagnostics.crc_errors;
  s_snapshot.adc_spi_errors = diagnostics.spi_errors;
  s_snapshot.dac_errors = DAC8564_GetFailedCycles();
  s_snapshot.system_state = (uint8_t)s_app_state;

  if (s_have_measurement && (s_app_state == APP_STATE_ACTIVE))
  {
    flags |= TELEMETRY_FLAG_VALID;
  }
  if (s_app_state == APP_STATE_MUTED || s_app_state == APP_STATE_RECOVERING)
  {
    flags |= TELEMETRY_FLAG_MUTED;
  }
  else if (s_app_state == APP_STATE_ADC_WARMUP)
  {
    flags |= TELEMETRY_FLAG_WARMUP;
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
  if (!s_adc_online || s_app_state == APP_STATE_MUTED || s_app_state == APP_STATE_RECOVERING)
  {
    flags |= TELEMETRY_FLAG_ADC_OFFLINE;
  }
  if (!DAC8564_IsHealthy())
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
#endif
  s_snapshot.flags = flags;
}

static bool App_ExecuteSpiRecovery(void)
{
  bool adc_ok;
  bool dac_ok;

  Hardware_Mute_Assert();
  HAL_NVIC_DisableIRQ(EXTI3_IRQn);
  ADS131M08_EnterRecovery();

  HAL_GPIO_WritePin(ADC_CS_N_GPIO_Port, ADC_CS_N_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(DAC_CS_N_GPIO_Port, DAC_CS_N_Pin, GPIO_PIN_SET);

  HAL_SPI_DeInit(&hspi1);
  MX_SPI1_Init();

  adc_ok = ADS131M08_Init(&hspi1);
  dac_ok = DAC8564_Init(&hspi1);

  Measurement_Init(&s_filter);

  __HAL_GPIO_EXTI_CLEAR_IT(ADC_DRDY_N_Pin);
  ADS131M08_ExitRecovery();

  if (adc_ok)
  {
    ADS131M08_Start();
    HAL_NVIC_EnableIRQ(EXTI3_IRQn);
  }

  s_adc_online = adc_ok;
  s_dac_online = dac_ok;
  return (adc_ok && dac_ok);
}

void App_Init(void)
{
  uint32_t now = HAL_GetTick();

  memset(&s_snapshot, 0, sizeof(s_snapshot));
  memset(&s_heartbeat, 0, sizeof(s_heartbeat));
  s_heartbeat.measurement_tick = now;
  s_heartbeat.network_tick = now;
  s_heartbeat.network_monitoring_enabled = false;

  Measurement_Init(&s_filter);
  SetSafeDacCodes();
  Hardware_Mute_Assert();

  s_app_state = APP_STATE_MUTED;
  s_have_measurement = false;
  s_last_recovery_attempt_ms = now;
  s_last_adc_progress_ms = now;
  s_last_dac_update_ms = now;
  s_last_telemetry_ms = now;
  s_consecutive_crc_errors = 0u;
  s_warmup_frames_count = 0u;

  Telemetry_Init();

  if (App_ExecuteSpiRecovery())
  {
    s_app_state = APP_STATE_ADC_WARMUP;
    s_warmup_start_ms = now;
    s_warmup_frames_count = 0u;
  }
  else
  {
    s_app_state = APP_STATE_MUTED;
  }

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

#if (APP_WATCHDOG_ENABLE != 0u)
  Watchdog_Init();
#endif

  if (s_app_state == APP_STATE_ACTIVE)
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

  /* Record measurement thread heartbeat */
  s_heartbeat.measurement_tick = now;

  /* State Machine: Handle Muted / Recovery Backoff */
  if (s_app_state == APP_STATE_MUTED)
  {
    Hardware_Mute_Assert();
    if (TimeElapsed(now, s_last_recovery_attempt_ms, APP_ADC_RETRY_PERIOD_MS))
    {
      s_last_recovery_attempt_ms = now;
      s_app_state = APP_STATE_RECOVERING;
      if (App_ExecuteSpiRecovery())
      {
        s_have_measurement = false;
        s_last_adc_progress_ms = now;
        s_consecutive_crc_errors = 0u;
        s_warmup_frames_count = 0u;
        s_warmup_start_ms = now;
        s_app_state = APP_STATE_ADC_WARMUP;
      }
      else
      {
        s_app_state = APP_STATE_MUTED;
      }
    }
  }

  /* Consume ADC frames */
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

      /* Warmup Verification */
      if (s_app_state == APP_STATE_ADC_WARMUP)
      {
        if (frame.crc_ok &&
            (HAL_GPIO_ReadPin(SENSOR_FAULT_N_GPIO_Port, SENSOR_FAULT_N_Pin) == GPIO_PIN_SET))
        {
          s_warmup_frames_count++;
          if (s_warmup_frames_count >= APP_WARMUP_REQUIRED_FRAMES && DAC8564_IsHealthy())
          {
            s_app_state = APP_STATE_ACTIVE;
            Hardware_Mute_Release();
            BSP_LED_On(LED_GREEN);
          }
        }
        else
        {
          s_warmup_frames_count = 0u;
        }
      }
    }
  }

  /* Failure Detection in Active or Warmup States */
  if (s_app_state == APP_STATE_ACTIVE || s_app_state == APP_STATE_ADC_WARMUP)
  {
    ADS131M08_Diagnostics diagnostics;
    ADS131M08_GetDiagnostics(&diagnostics);

    bool adc_fault = (diagnostics.spi_errors >= APP_ADC_MAX_SPI_ERRORS_PER_RUN) ||
                     (s_consecutive_crc_errors >= APP_ADC_MAX_CONSECUTIVE_CRC_ERRORS) ||
                     TimeElapsed(now, s_last_adc_progress_ms, APP_ADC_NO_PROGRESS_TIMEOUT_MS);

    bool dac_fault = (s_app_state == APP_STATE_ACTIVE && !DAC8564_IsHealthy()) ||
                     DAC8564_HasFault();

    bool warmup_timeout = (s_app_state == APP_STATE_ADC_WARMUP &&
                           TimeElapsed(now, s_warmup_start_ms, APP_WARMUP_TIMEOUT_MS));

    if (adc_fault || dac_fault || warmup_timeout)
    {
      Hardware_Mute_Assert();
      ADS131M08_EnterRecovery();
      s_adc_online = false;
      s_have_measurement = false;
      s_last_recovery_attempt_ms = now;
      s_app_state = APP_STATE_MUTED;
      SetSafeDacCodes();
      BSP_LED_Off(LED_GREEN);
    }
  }

  RefreshFlags();

  /* Periodic DAC output update */
  if (TimeElapsed(now, s_last_dac_update_ms, APP_DAC_UPDATE_PERIOD_MS))
  {
    s_last_dac_update_ms = now;
    if (s_app_state != APP_STATE_ACTIVE || !s_have_measurement)
    {
      SetSafeDacCodes();
    }

    if (ADS131M08_TryLockBus())
    {
      s_dac_online = DAC8564_WriteAll(s_snapshot.measurement.dac_code);
      ADS131M08_UnlockBus();
    }
  }

  /* Periodic Telemetry */
  if (TimeElapsed(now, s_last_telemetry_ms, APP_TELEMETRY_PERIOD_MS))
  {
    uint8_t frame_buf[TELEMETRY_MAX_FRAME_SIZE];
    size_t length;

    s_last_telemetry_ms = now;
    RefreshFlags();
    length = Telemetry_BuildFrame(&s_snapshot, frame_buf);
    if (length != 0u)
    {
#if (APP_NETWORK_ENABLE != 0u)
      (void)NetXDuo_PublishTelemetry(frame_buf, length);
#endif
#if (APP_TELEMETRY_UART_ENABLE != 0u)
      (void)Telemetry_SendFrame(frame_buf, length);
#endif
    }
    BSP_LED_Toggle(LED_YELLOW);
  }

  /* Status LED */
  if ((s_snapshot.flags & (TELEMETRY_FLAG_AFE_FAULT |
                           TELEMETRY_FLAG_ADC_OFFLINE |
                           TELEMETRY_FLAG_DAC_ERROR |
                           TELEMETRY_FLAG_MUTED)) != 0u)
  {
    BSP_LED_On(LED_RED);
  }
  else
  {
    BSP_LED_Off(LED_RED);
  }

  /* Step watchdog supervisor */
  App_WatchdogSupervisorStep();
}

void App_FeedNetworkHeartbeat(void)
{
  s_heartbeat.network_tick = HAL_GetTick();
}

void App_EnableNetworkWatchdog(bool enable)
{
  s_heartbeat.network_monitoring_enabled = enable;
  s_heartbeat.network_tick = HAL_GetTick();
}

AppState App_GetState(void)
{
  return s_app_state;
}

void App_WatchdogSupervisorStep(void)
{
#if (APP_WATCHDOG_ENABLE != 0u)
  uint32_t now = HAL_GetTick();
  bool meas_alive = ((uint32_t)(now - s_heartbeat.measurement_tick) < APP_WATCHDOG_MEAS_GRACE_MS);
  bool net_alive = true;

  if (s_heartbeat.network_monitoring_enabled)
  {
    net_alive = ((uint32_t)(now - s_heartbeat.network_tick) < APP_WATCHDOG_NET_GRACE_MS);
  }

  if (meas_alive && net_alive)
  {
    Watchdog_Refresh();
  }
#endif
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

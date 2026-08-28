#include "ads131m08.h"
#include "app_config.h"
#include "main.h"
#include <string.h>

#if ((APP_ADC_RING_CAPACITY == 0u) || \
     ((APP_ADC_RING_CAPACITY & (APP_ADC_RING_CAPACITY - 1u)) != 0u))
#error "APP_ADC_RING_CAPACITY must be a non-zero power of two"
#endif

#define ADS131M08_CMD_NULL      (0x0000u)
#define ADS131M08_CMD_LOCK      (0x0555u)
#define ADS131M08_CMD_UNLOCK    (0x0655u)
#define ADS131M08_CMD_RREG      (0xA000u)
#define ADS131M08_CMD_WREG      (0x6000u)

#define ADS131M08_REG_ID        (0x00u)
#define ADS131M08_REG_MODE      (0x02u)
#define ADS131M08_REG_CLOCK     (0x03u)
#define ADS131M08_REG_GAIN1     (0x04u)

typedef struct
{
  uint8_t bytes[ADS131M08_FRAME_BYTES];
  uint32_t sequence;
  uint32_t timestamp_ms;
} RawFrameSlot;

static SPI_HandleTypeDef *s_spi;
static uint8_t s_dma_tx[ADS131M08_FRAME_BYTES];
static uint8_t s_dma_rx[ADS131M08_FRAME_BYTES];
static RawFrameSlot s_ring[APP_ADC_RING_CAPACITY];
static volatile uint32_t s_ring_head;
static volatile uint32_t s_ring_tail;
static volatile bool s_dma_busy;
static volatile bool s_bus_locked;
static volatile bool s_drdy_pending;
static volatile bool s_running;
static volatile bool s_online;
static volatile bool s_recovering;
static volatile uint32_t s_sequence;
static volatile uint32_t s_discard_frames;
static ADS131M08_Diagnostics s_diagnostics;

static uint32_t EnterCritical(void)
{
  uint32_t primask = __get_PRIMASK();
  __disable_irq();
  return primask;
}

static void ExitCritical(uint32_t primask)
{
  if (primask == 0u)
  {
    __enable_irq();
  }
}

static void SetCommandWord(uint8_t frame[ADS131M08_FRAME_BYTES], uint16_t command)
{
  memset(frame, 0, ADS131M08_FRAME_BYTES);
  frame[0] = (uint8_t)(command >> 8);
  frame[1] = (uint8_t)(command & 0xFFu);
}

static bool BlockingExchange(const uint8_t tx[ADS131M08_FRAME_BYTES],
                             uint8_t rx[ADS131M08_FRAME_BYTES])
{
  HAL_StatusTypeDef status;

  HAL_GPIO_WritePin(ADC_CS_N_GPIO_Port, ADC_CS_N_Pin, GPIO_PIN_RESET);
  status = HAL_SPI_TransmitReceive(s_spi, (uint8_t *)tx, rx,
                                   ADS131M08_FRAME_BYTES,
                                   APP_ADC_SPI_TIMEOUT_MS);
  HAL_GPIO_WritePin(ADC_CS_N_GPIO_Port, ADC_CS_N_Pin, GPIO_PIN_SET);
  if (status != HAL_OK)
  {
    s_diagnostics.spi_errors++;
    return false;
  }
  return true;
}

static bool SendCommand(uint16_t command)
{
  uint8_t tx[ADS131M08_FRAME_BYTES];
  uint8_t rx[ADS131M08_FRAME_BYTES];

  SetCommandWord(tx, command);
  return BlockingExchange(tx, rx);
}

static bool WriteRegister(uint8_t address, uint16_t value)
{
  uint8_t tx[ADS131M08_FRAME_BYTES];
  uint8_t rx[ADS131M08_FRAME_BYTES];
  uint16_t command = (uint16_t)(ADS131M08_CMD_WREG | ((uint16_t)address << 7));

  SetCommandWord(tx, command);
  tx[3] = (uint8_t)(value >> 8);
  tx[4] = (uint8_t)(value & 0xFFu);
  return BlockingExchange(tx, rx);
}

static bool ReadRegister(uint8_t address, uint16_t *value)
{
  uint8_t tx[ADS131M08_FRAME_BYTES];
  uint8_t rx[ADS131M08_FRAME_BYTES];
  uint16_t command = (uint16_t)(ADS131M08_CMD_RREG | ((uint16_t)address << 7));

  SetCommandWord(tx, command);
  if (!BlockingExchange(tx, rx))
  {
    return false;
  }

  SetCommandWord(tx, ADS131M08_CMD_NULL);
  if (!BlockingExchange(tx, rx))
  {
    return false;
  }

  *value = (uint16_t)(((uint16_t)rx[0] << 8) | rx[1]);
  return true;
}

static bool VerifyRegister(uint8_t address, uint16_t expected, uint16_t *actual)
{
  if (!ReadRegister(address, actual))
  {
    return false;
  }
  return (*actual == expected);
}

int32_t ADS131M08_Decode24(const uint8_t word[3])
{
  uint32_t value = ((uint32_t)word[0] << 16) |
                   ((uint32_t)word[1] << 8) |
                   (uint32_t)word[2];

  if ((value & 0x00800000u) != 0u)
  {
    value |= 0xFF000000u;
  }
  return (int32_t)value;
}

uint16_t ADS131M08_Crc16Ccitt(const uint8_t *data, size_t length)
{
  uint16_t crc = 0xFFFFu;

  for (size_t i = 0; i < length; ++i)
  {
    crc ^= (uint16_t)data[i] << 8;
    for (uint32_t bit = 0; bit < 8u; ++bit)
    {
      crc = ((crc & 0x8000u) != 0u) ?
            (uint16_t)((crc << 1) ^ 0x1021u) : (uint16_t)(crc << 1);
    }
  }
  return crc;
}

bool ADS131M08_Init(SPI_HandleTypeDef *spi)
{
  uint16_t id = 0u;

  s_spi = spi;
  s_running = false;
  s_online = false;
  s_dma_busy = false;
  s_bus_locked = false;
  s_drdy_pending = false;
  s_ring_head = 0u;
  s_ring_tail = 0u;
  s_sequence = 0u;
  s_discard_frames = 0u;
  memset(&s_diagnostics, 0, sizeof(s_diagnostics));
  memset(s_dma_tx, 0, sizeof(s_dma_tx));

  HAL_GPIO_WritePin(DAC_CS_N_GPIO_Port, DAC_CS_N_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(ADC_CS_N_GPIO_Port, ADC_CS_N_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(ADC_RESET_N_GPIO_Port, ADC_RESET_N_Pin, GPIO_PIN_RESET);
  HAL_Delay(APP_ADC_RESET_LOW_MS);
  HAL_GPIO_WritePin(ADC_RESET_N_GPIO_Port, ADC_RESET_N_Pin, GPIO_PIN_SET);
  HAL_Delay(APP_ADC_RESET_RECOVERY_MS);

  if (!ReadRegister(ADS131M08_REG_ID, &id) || ((id & 0xFF00u) != 0x2800u))
  {
    s_diagnostics.id_register = id;
    return false;
  }
  s_diagnostics.id_register = id;

  if (!SendCommand(ADS131M08_CMD_UNLOCK) ||
      !WriteRegister(ADS131M08_REG_MODE, APP_ADC_MODE_REGISTER) ||
      !WriteRegister(ADS131M08_REG_CLOCK, APP_ADC_CLOCK_REGISTER) ||
      !WriteRegister(ADS131M08_REG_GAIN1, APP_ADC_GAIN1_REGISTER))
  {
    return false;
  }

  if (!VerifyRegister(ADS131M08_REG_MODE, APP_ADC_MODE_REGISTER,
                      &s_diagnostics.mode_register) ||
      !VerifyRegister(ADS131M08_REG_CLOCK, APP_ADC_CLOCK_REGISTER,
                      &s_diagnostics.clock_register) ||
      !VerifyRegister(ADS131M08_REG_GAIN1, APP_ADC_GAIN1_REGISTER,
                      &s_diagnostics.gain1_register))
  {
    return false;
  }

#if (APP_ADC_LOCK_REGISTERS != 0u)
  if (!SendCommand(ADS131M08_CMD_LOCK))
  {
    return false;
  }
#endif

  s_online = true;
  return true;
}

void ADS131M08_Start(void)
{
  uint32_t primask = EnterCritical();
  s_ring_head = 0u;
  s_ring_tail = 0u;
  s_dma_busy = false;
  s_bus_locked = false;
  s_drdy_pending = false;
  s_discard_frames = APP_ADC_STARTUP_DISCARD_FRAMES;
  s_running = s_online;
  ExitCritical(primask);
}

void ADS131M08_Stop(void)
{
  SPI_HandleTypeDef *spi;
  bool abort_dma;
  uint32_t primask = EnterCritical();

  s_running = false;
  spi = s_spi;
  abort_dma = s_dma_busy;
  ExitCritical(primask);

  /*
   * App recovery masks DRDY before calling Stop.  If the previous DRDY
   * already started a DMA exchange, terminate it before reset/register
   * accesses reuse SPI1.  Leaving that DMA live could assert the ADC CS
   * during the subsequent blocking initialization exchange.
   */
  if (abort_dma && spi != NULL)
  {
    (void)HAL_SPI_Abort(spi);
  }
  HAL_GPIO_WritePin(ADC_CS_N_GPIO_Port, ADC_CS_N_Pin, GPIO_PIN_SET);

  primask = EnterCritical();
  s_dma_busy = false;
  s_drdy_pending = false;
  ExitCritical(primask);
}

bool ADS131M08_IsOnline(void)
{
  return s_online;
}

void ADS131M08_EnterRecovery(void)
{
  SPI_HandleTypeDef *spi;
  bool abort_dma;
  uint32_t primask = EnterCritical();

  s_recovering = true;
  s_running = false;
  spi = s_spi;
  abort_dma = s_dma_busy;
  ExitCritical(primask);

  if (abort_dma && spi != NULL)
  {
    (void)HAL_SPI_Abort(spi);
  }
  HAL_GPIO_WritePin(ADC_CS_N_GPIO_Port, ADC_CS_N_Pin, GPIO_PIN_SET);

  primask = EnterCritical();
  s_dma_busy = false;
  s_drdy_pending = false;
  ExitCritical(primask);
}

void ADS131M08_ExitRecovery(void)
{
  uint32_t primask = EnterCritical();
  s_recovering = false;
  ExitCritical(primask);
}

void ADS131M08_OnDrdyInterrupt(void)
{
  HAL_StatusTypeDef status;

  if (s_recovering)
  {
    return;
  }

  s_diagnostics.drdy_count++;
  if (!s_running || !s_online)
  {
    return;
  }

  if (s_dma_busy)
  {
    s_diagnostics.dropped_frames++;
    s_diagnostics.dropped_dma_busy++;
    return;
  }

  if (s_bus_locked)
  {
    /* One deferred DRDY can be serviced after the DAC transaction.  Further
     * DRDY edges overwrite that pending conversion and are actual losses. */
    if (s_drdy_pending)
    {
      s_diagnostics.dropped_frames++;
      s_diagnostics.dropped_bus_locked++;
    }
    s_drdy_pending = true;
    return;
  }

  s_dma_busy = true;
  HAL_GPIO_WritePin(ADC_CS_N_GPIO_Port, ADC_CS_N_Pin, GPIO_PIN_RESET);
  status = HAL_SPI_TransmitReceive_DMA(s_spi, s_dma_tx, s_dma_rx,
                                       ADS131M08_FRAME_BYTES);
  if (status != HAL_OK)
  {
    HAL_GPIO_WritePin(ADC_CS_N_GPIO_Port, ADC_CS_N_Pin, GPIO_PIN_SET);
    s_dma_busy = false;
    s_diagnostics.spi_errors++;
    s_diagnostics.dropped_frames++;
    s_diagnostics.dropped_spi_start++;
  }
}

void ADS131M08_OnSpiCompleteInterrupt(SPI_HandleTypeDef *spi)
{
  uint32_t next_head;
  RawFrameSlot *slot;

  if (spi != s_spi || !s_dma_busy || s_recovering)
  {
    return;
  }

  HAL_GPIO_WritePin(ADC_CS_N_GPIO_Port, ADC_CS_N_Pin, GPIO_PIN_SET);
  s_dma_busy = false;
  s_diagnostics.completed_frames++;

  if (s_discard_frames > 0u)
  {
    s_discard_frames--;
    return;
  }

  next_head = (s_ring_head + 1u) & (APP_ADC_RING_CAPACITY - 1u);
  if (next_head == s_ring_tail)
  {
    s_diagnostics.dropped_frames++;
    s_diagnostics.dropped_ring_full++;
    return;
  }

  slot = &s_ring[s_ring_head];
  memcpy(slot->bytes, s_dma_rx, ADS131M08_FRAME_BYTES);
  slot->sequence = ++s_sequence;
  slot->timestamp_ms = HAL_GetTick();
  __DMB();
  s_ring_head = next_head;
}

void ADS131M08_OnSpiErrorInterrupt(SPI_HandleTypeDef *spi)
{
  if (spi != s_spi || s_recovering)
  {
    return;
  }

  HAL_GPIO_WritePin(ADC_CS_N_GPIO_Port, ADC_CS_N_Pin, GPIO_PIN_SET);
  s_dma_busy = false;
  s_diagnostics.spi_errors++;
  s_diagnostics.dropped_frames++;
  s_diagnostics.dropped_spi_error++;
}

bool ADS131M08_ReadFrame(ADS131M08_Frame *frame)
{
  RawFrameSlot raw;
  uint32_t tail;

  if (frame == NULL || s_recovering)
  {
    return false;
  }

  tail = s_ring_tail;
  if (tail == s_ring_head)
  {
    return false;
  }

  __DMB();
  raw = s_ring[tail];
  s_ring_tail = (tail + 1u) & (APP_ADC_RING_CAPACITY - 1u);

  frame->sequence = raw.sequence;
  frame->timestamp_ms = raw.timestamp_ms;
  frame->response_status = (uint16_t)(((uint16_t)raw.bytes[0] << 8) |
                                      raw.bytes[1]);
  for (uint32_t channel = 0u; channel < ADS131M08_CHANNEL_COUNT; ++channel)
  {
    frame->channel[channel] =
      ADS131M08_Decode24(&raw.bytes[(channel + 1u) * ADS131M08_WORD_BYTES]);
  }
  frame->received_crc =
    (uint16_t)(((uint16_t)raw.bytes[27] << 8) | raw.bytes[28]);
  frame->crc_ok = (ADS131M08_Crc16Ccitt(raw.bytes, 27u) == frame->received_crc);
  if (!frame->crc_ok)
  {
    s_diagnostics.crc_errors++;
  }
  return true;
}

void ADS131M08_GetDiagnostics(ADS131M08_Diagnostics *diagnostics)
{
  if (diagnostics == NULL)
  {
    return;
  }

  uint32_t primask = EnterCritical();
  *diagnostics = s_diagnostics;
  ExitCritical(primask);
}

bool ADS131M08_TryLockBus(void)
{
  bool locked = false;
  uint32_t primask = EnterCritical();

  if (!s_dma_busy && !s_bus_locked)
  {
    s_bus_locked = true;
    locked = true;
  }
  ExitCritical(primask);
  return locked;
}

void ADS131M08_UnlockBus(void)
{
  bool trigger_pending = false;
  uint32_t primask = EnterCritical();

  s_bus_locked = false;
  if (s_drdy_pending && s_running && s_online && !s_dma_busy)
  {
    s_drdy_pending = false;
    s_dma_busy = true;
    trigger_pending = true;
  }
  ExitCritical(primask);

  if (trigger_pending)
  {
    HAL_GPIO_WritePin(ADC_CS_N_GPIO_Port, ADC_CS_N_Pin, GPIO_PIN_RESET);
    if (HAL_SPI_TransmitReceive_DMA(s_spi, s_dma_tx, s_dma_rx,
                                    ADS131M08_FRAME_BYTES) != HAL_OK)
    {
      HAL_GPIO_WritePin(ADC_CS_N_GPIO_Port, ADC_CS_N_Pin, GPIO_PIN_SET);
      uint32_t p = EnterCritical();
      s_dma_busy = false;
      ExitCritical(p);
      s_diagnostics.spi_errors++;
      s_diagnostics.dropped_frames++;
      s_diagnostics.dropped_spi_start++;
    }
  }
}

#ifndef APP_H
#define APP_H

#include "stm32h5xx_hal.h"
#include <stdbool.h>
#include <stdint.h>

typedef enum
{
  APP_STATE_MUTED = 0,
  APP_STATE_RECOVERING = 1,
  APP_STATE_ADC_WARMUP = 2,
  APP_STATE_ACTIVE = 3
} AppState;

typedef struct
{
  volatile uint32_t measurement_tick;
  volatile uint32_t network_tick;
  bool network_monitoring_enabled;
} AppHeartbeat;

void App_Init(void);
void App_Task(void);
AppState App_GetState(void);
void App_FeedNetworkHeartbeat(void);
void App_EnableNetworkWatchdog(bool enable);
void App_WatchdogSupervisorStep(void);

#endif /* APP_H */

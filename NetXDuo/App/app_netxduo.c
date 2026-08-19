/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    app_netxduo.c
  * @author  MCD Application Team
  * @brief   NetXDuo applicative file
  ******************************************************************************
    * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "app_netxduo.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "app.h"
#include "app_config.h"
#include "nxd_dhcp_client.h"
#include "telemetry.h"
#include <string.h>

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define NET_EVENT_TELEMETRY_READY (1UL << 0)
#define NET_SOCKET_QUEUE_DEPTH    (4u)

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN PV */
static NX_PACKET_POOL s_packet_pool;
static NX_IP s_ip;
static NX_UDP_SOCKET s_udp_socket;
#if (APP_NETWORK_USE_DHCP != 0u)
static NX_DHCP s_dhcp;
#endif
static TX_THREAD s_network_thread;
static TX_MUTEX s_telemetry_mutex;
static TX_EVENT_FLAGS_GROUP s_network_events;
static uint8_t s_telemetry_frame[TELEMETRY_MAX_FRAME_SIZE];
static size_t s_telemetry_length;
static volatile NetXDuoStatus s_status;
static bool s_driver_link_enabled;
static bool s_udp_socket_bound;
static ULONG s_next_link_enable_tick;

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN PFP */
static VOID NetworkThreadEntry(ULONG argument);

/* USER CODE END PFP */

/* USER CODE BEGIN 0 */
static UINT AllocateFromPool(TX_BYTE_POOL *pool, VOID **memory, ULONG size)
{
  return tx_byte_allocate(pool, memory, size, TX_NO_WAIT);
}

/*
 * Networking is an optional telemetry path.  Keep its initialization error
 * available to the measurement application, but never let it prevent the
 * USB/VCP measurement path from starting.
 */
static UINT NetInitFailure(UINT status)
{
  s_status.last_error = status;
  return status;
}

static ULONG ConfiguredAddress(uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
  return IP_ADDRESS(a, b, c, d);
}

static void UpdateNetworkStatus(void)
{
  ULONG actual_status = 0u;
  ULONG address = 0u;
  ULONG mask = 0u;

  s_status.link_up =
    (nx_ip_status_check(&s_ip, NX_IP_LINK_ENABLED, &actual_status,
                        NX_NO_WAIT) == NX_SUCCESS);
  actual_status = 0u;
  s_status.address_ready =
    (nx_ip_status_check(&s_ip, NX_IP_ADDRESS_RESOLVED, &actual_status,
                        NX_NO_WAIT) == NX_SUCCESS);

  if (s_status.address_ready &&
      nx_ip_address_get(&s_ip, &address, &mask) == NX_SUCCESS)
  {
    s_status.ip_address = (uint32_t)address;
    s_status.network_mask = (uint32_t)mask;
  }
  else
  {
    s_status.ip_address = 0u;
    s_status.network_mask = 0u;
  }
}

/*
 * The STM32 Ethernet driver reports the physical PHY state for
 * NX_LINK_GET_STATUS, but a boot with no cable can leave the driver itself
 * in the initialized (rather than link-enabled) state.  Retry the enable
 * command after the PHY reports an actual link so cable insertion does not
 * require an MCU reset.
 */
static void EnsureDriverLinkEnabled(void)
{
  ULONG driver_result = 0u;
  ULONG now;
  UINT status;

  if (!s_status.link_up)
  {
    s_driver_link_enabled = false;
    s_next_link_enable_tick = 0u;
    return;
  }

  now = tx_time_get();
  if (s_driver_link_enabled || (LONG)(now - s_next_link_enable_tick) < 0)
  {
    return;
  }
  s_next_link_enable_tick = now + APP_NETWORK_LINK_RETRY_TICKS;

  status = nx_ip_driver_direct_command(&s_ip, NX_LINK_ENABLE,
                                       &driver_result);
  if (status == NX_SUCCESS || status == NX_ALREADY_ENABLED)
  {
    s_driver_link_enabled = true;
    s_status.last_error = NX_SUCCESS;
  }
  else
  {
    s_status.last_error = status;
  }
}

static UINT SendUdpFrame(const uint8_t *frame, size_t length)
{
  NX_PACKET *packet = NX_NULL;
  const ULONG destination = ConfiguredAddress(
    APP_NETWORK_UDP_DESTINATION_0, APP_NETWORK_UDP_DESTINATION_1,
    APP_NETWORK_UDP_DESTINATION_2, APP_NETWORK_UDP_DESTINATION_3);
  UINT status;

  status = nx_packet_allocate(&s_packet_pool, &packet, NX_UDP_PACKET,
                              NX_NO_WAIT);
  if (status != NX_SUCCESS)
  {
    return status;
  }

  status = nx_packet_data_append(packet, (VOID *)frame, (ULONG)length,
                                 &s_packet_pool, NX_NO_WAIT);
  if (status == NX_SUCCESS)
  {
    status = nx_udp_socket_send(&s_udp_socket, packet, destination,
                                APP_NETWORK_UDP_DESTINATION_PORT);
  }

  if (status != NX_SUCCESS && packet != NX_NULL)
  {
    (void)nx_packet_release(packet);
  }
  return status;
}

static VOID NetworkThreadEntry(ULONG argument)
{
  uint8_t frame[TELEMETRY_MAX_FRAME_SIZE];
  ULONG events;

  (void)argument;
  for (;;)
  {
    size_t length = 0u;
    UINT status;

    App_FeedNetworkHeartbeat();

    /* nx_udp_socket_bind is thread-only in NetX Duo.  Calling it from
     * tx_application_define returns NX_CALLER_ERROR (0x11) and used to
     * prevent every application thread, including USB telemetry, from
     * starting. */
    if (!s_udp_socket_bound)
    {
      status = nx_udp_socket_bind(&s_udp_socket, APP_NETWORK_UDP_SOURCE_PORT,
                                  TX_NO_WAIT);
      if (status != NX_SUCCESS)
      {
        s_status.last_error = status;
        tx_thread_sleep(APP_NETWORK_STATUS_PERIOD_TICKS);
        continue;
      }
      s_udp_socket_bound = true;
    }

    UpdateNetworkStatus();
    EnsureDriverLinkEnabled();
    status = tx_event_flags_get(&s_network_events,
                                NET_EVENT_TELEMETRY_READY,
                                TX_OR_CLEAR, &events,
                                APP_NETWORK_STATUS_PERIOD_TICKS);
    if (status != TX_SUCCESS)
    {
      continue;
    }

    if (tx_mutex_get(&s_telemetry_mutex, TX_WAIT_FOREVER) == TX_SUCCESS)
    {
      length = s_telemetry_length;
      if (length <= sizeof(frame))
      {
        memcpy(frame, s_telemetry_frame, length);
      }
      (void)tx_mutex_put(&s_telemetry_mutex);
    }

    UpdateNetworkStatus();
    EnsureDriverLinkEnabled();
    if (length == 0u || !s_status.link_up || !s_driver_link_enabled ||
        !s_status.address_ready)
    {
      continue;
    }

    status = SendUdpFrame(frame, length);
    if (status == NX_SUCCESS)
    {
      s_status.sent_packets++;
      s_status.last_error = NX_SUCCESS;
    }
    else
    {
      s_status.send_errors++;
      s_status.last_error = status;
    }
  }
}

bool NetXDuo_PublishTelemetry(const uint8_t *frame, size_t length)
{
  if (!s_status.initialized || frame == NULL || length == 0u ||
      length > sizeof(s_telemetry_frame))
  {
    return false;
  }

  if (tx_mutex_get(&s_telemetry_mutex, TX_NO_WAIT) != TX_SUCCESS)
  {
    s_status.telemetry_drops++;
    return false;
  }

  memcpy(s_telemetry_frame, frame, length);
  s_telemetry_length = length;
  (void)tx_mutex_put(&s_telemetry_mutex);

  if (tx_event_flags_set(&s_network_events, NET_EVENT_TELEMETRY_READY,
                         TX_OR) != TX_SUCCESS)
  {
    s_status.telemetry_drops++;
    return false;
  }
  return true;
}

void NetXDuo_GetStatus(NetXDuoStatus *status)
{
  if (status == NULL)
  {
    return;
  }

  status->initialized = s_status.initialized;
  status->link_up = s_status.link_up;
  status->address_ready = s_status.address_ready;
  status->ip_address = s_status.ip_address;
  status->network_mask = s_status.network_mask;
  status->sent_packets = s_status.sent_packets;
  status->send_errors = s_status.send_errors;
  status->telemetry_drops = s_status.telemetry_drops;
  status->last_error = s_status.last_error;
}

/* USER CODE END 0 */

/**
  * @brief  Application NetXDuo Initialization.
  * @param memory_ptr: memory pointer
  * @retval int
  */
UINT MX_NetXDuo_Init(VOID *memory_ptr)
{
  UINT ret = NX_SUCCESS;
  TX_BYTE_POOL *byte_pool = (TX_BYTE_POOL*)memory_ptr;

   /* USER CODE BEGIN App_NetXDuo_MEM_POOL */
  /* USER CODE END App_NetXDuo_MEM_POOL */
  /* USER CODE BEGIN 0 */

  /* USER CODE END 0 */

  /* USER CODE BEGIN MX_NetXDuo_Init */
#if (APP_NETWORK_ENABLE != 0u)
  VOID *packet_memory = NX_NULL;
  VOID *ip_stack = NX_NULL;
  VOID *arp_cache = NX_NULL;
  VOID *network_stack = NX_NULL;
  ULONG initial_address;
  ULONG initial_mask;

  memset((void *)&s_status, 0, sizeof(s_status));
  nx_system_initialize();

  if (AllocateFromPool(byte_pool, &packet_memory,
                       APP_NETWORK_PACKET_POOL_SIZE) != TX_SUCCESS ||
      AllocateFromPool(byte_pool, &ip_stack,
                       APP_NETWORK_IP_THREAD_STACK_SIZE) != TX_SUCCESS ||
      AllocateFromPool(byte_pool, &arp_cache,
                       APP_NETWORK_ARP_CACHE_SIZE) != TX_SUCCESS ||
      AllocateFromPool(byte_pool, &network_stack,
                       APP_NETWORK_THREAD_STACK_SIZE) != TX_SUCCESS)
  {
    return NetInitFailure(NX_NOT_SUCCESSFUL);
  }

  ret = nx_packet_pool_create(&s_packet_pool, "COSMOKE packets",
                              APP_NETWORK_PACKET_PAYLOAD_SIZE,
                              packet_memory, APP_NETWORK_PACKET_POOL_SIZE);
  if (ret != NX_SUCCESS)
  {
    return NetInitFailure(ret);
  }

#if (APP_NETWORK_USE_DHCP != 0u)
  initial_address = IP_ADDRESS(0, 0, 0, 0);
  initial_mask = IP_ADDRESS(0, 0, 0, 0);
#else
  initial_address = ConfiguredAddress(
    APP_NETWORK_STATIC_IP_0, APP_NETWORK_STATIC_IP_1,
    APP_NETWORK_STATIC_IP_2, APP_NETWORK_STATIC_IP_3);
  initial_mask = ConfiguredAddress(
    APP_NETWORK_STATIC_MASK_0, APP_NETWORK_STATIC_MASK_1,
    APP_NETWORK_STATIC_MASK_2, APP_NETWORK_STATIC_MASK_3);
#endif

  ret = nx_ip_create(&s_ip, "COSMOKE IPv4", initial_address, initial_mask,
                     &s_packet_pool, nx_stm32_eth_driver,
                     ip_stack, APP_NETWORK_IP_THREAD_STACK_SIZE,
                     APP_NETWORK_IP_THREAD_PRIORITY);
  if (ret != NX_SUCCESS)
  {
    return NetInitFailure(ret);
  }

  ret = nx_arp_enable(&s_ip, arp_cache, APP_NETWORK_ARP_CACHE_SIZE);
  if (ret == NX_SUCCESS)
  {
    ret = nx_icmp_enable(&s_ip);
  }
  if (ret == NX_SUCCESS)
  {
    ret = nx_udp_enable(&s_ip);
  }
  if (ret != NX_SUCCESS)
  {
    return NetInitFailure(ret);
  }

#if (APP_NETWORK_USE_DHCP == 0u)
  ret = nx_ip_gateway_address_set(&s_ip, ConfiguredAddress(
    APP_NETWORK_STATIC_GATEWAY_0, APP_NETWORK_STATIC_GATEWAY_1,
    APP_NETWORK_STATIC_GATEWAY_2, APP_NETWORK_STATIC_GATEWAY_3));
  if (ret != NX_SUCCESS)
  {
    return NetInitFailure(ret);
  }
#endif

  ret = nx_udp_socket_create(&s_ip, &s_udp_socket, "COSMOKE telemetry",
                             NX_IP_NORMAL, NX_DONT_FRAGMENT,
                             NX_IP_TIME_TO_LIVE, NET_SOCKET_QUEUE_DEPTH);
  if (ret != NX_SUCCESS)
  {
    return NetInitFailure(ret);
  }

  if (tx_mutex_create(&s_telemetry_mutex, "Telemetry mailbox",
                      TX_INHERIT) != TX_SUCCESS ||
      tx_event_flags_create(&s_network_events,
                            "Network events") != TX_SUCCESS)
  {
    return NetInitFailure(NX_NOT_SUCCESSFUL);
  }

#if (APP_NETWORK_USE_DHCP != 0u)
  ret = nx_dhcp_create(&s_dhcp, &s_ip, "COSMOKE DHCP");
  if (ret == NX_SUCCESS)
  {
    ret = nx_dhcp_start(&s_dhcp);
  }
  if (ret != NX_SUCCESS)
  {
    return NetInitFailure(ret);
  }
#endif

  if (tx_thread_create(&s_network_thread, "LAN telemetry",
                       NetworkThreadEntry, 0u,
                       network_stack, APP_NETWORK_THREAD_STACK_SIZE,
                       APP_NETWORK_THREAD_PRIORITY,
                       APP_NETWORK_THREAD_PRIORITY,
                       TX_NO_TIME_SLICE, TX_AUTO_START) != TX_SUCCESS)
  {
    return NetInitFailure(NX_NOT_SUCCESSFUL);
  }

  s_status.initialized = true;
  App_EnableNetworkWatchdog(true);
#else
  (void)byte_pool;
#endif
  /* USER CODE END MX_NetXDuo_Init */

  return ret;
}

/* USER CODE BEGIN 1 */

/* USER CODE END 1 */

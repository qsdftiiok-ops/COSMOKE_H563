#ifndef APP_CONFIG_H
#define APP_CONFIG_H

/*
 * COSMOKE voltage/current sensor firmware parameters.
 *
 * This is the first file to edit when calibrating the board.  The remaining
 * application code consumes these values and normally does not need changes.
 */

/* ADS131M08 ---------------------------------------------------------------- */
#define APP_ADC_REFERENCE_V                 (1.200000f)
#define APP_ADC_PGA_GAIN                    (1.000000f)
#define APP_ADC_CLOCK_HZ                    (8192000u)
#define APP_ADC_SAMPLE_RATE_HZ              (2000u)       /* HR, OSR = 2048 */

/* MODE: 24-bit words, input/register CRC disabled, SPI timeout enabled. */
#define APP_ADC_MODE_REGISTER               (0x0110u)

/*
 * CLOCK: CH0..3 enabled, CH4..7 disabled, external CMOS clock, OSR 2048, HR.
 *
 * 2 kSPS is the reliable measurement profile for the shared ADC/DAC SPI bus.
 * It doubles the DRDY service budget compared with the previous 4 kSPS profile
 * and improves input-referred noise.  Restore 0x0F8E (OSR 1024) only after a
 * hardware timing capture proves that the 4 kSPS profile has zero frame drops.
 */
#define APP_ADC_CLOCK_REGISTER              (0x0F92u)
#define APP_ADC_GAIN1_REGISTER              (0x0000u)     /* PGA gain = 1 */

#define APP_ADC_VERIFY_OUTPUT_CRC            (1u)
#define APP_ADC_LOCK_REGISTERS               (1u)
#define APP_ADC_RESET_LOW_MS                 (2u)
#define APP_ADC_RESET_RECOVERY_MS            (10u)
#define APP_ADC_SPI_TIMEOUT_MS               (5u)
#define APP_ADC_RING_CAPACITY                (256u)       /* Must be power of 2 */
#define APP_ADC_STARTUP_DISCARD_FRAMES        (2u)

/* Channel calibration ------------------------------------------------------ */
/*
 * Conversion order: raw -> subtract ZERO_COUNTS -> ADC volts -> engineering
 * units -> multiply SCALE_TRIM -> add OFFSET_UNITS -> IIR filter.
 *
 * VOLTS_PER_UNIT values come from the board schematic.  Replace the trims
 * after calibration with a traceable current/voltage source.
 */
#define APP_CH0_ZERO_COUNTS                  (0)
#define APP_CH0_VOLTS_PER_UNIT               (0.030530f)  /* CH1 current, V/A */
#define APP_CH0_SCALE_TRIM                   (1.000000f)
#define APP_CH0_OFFSET_UNITS                 (0.000000f)
#define APP_CH0_POLARITY                     (1.000000f)
#define APP_CH0_FILTER_ALPHA                 (0.050000f)
#define APP_CH0_FULL_SCALE_UNITS             (30.000000f)

#define APP_CH1_ZERO_COUNTS                  (0)
#define APP_CH1_VOLTS_PER_UNIT               (0.004009f)  /* CH1 voltage, V/V */
#define APP_CH1_SCALE_TRIM                   (1.000000f)
#define APP_CH1_OFFSET_UNITS                 (0.000000f)
#define APP_CH1_POLARITY                     (1.000000f)
#define APP_CH1_FILTER_ALPHA                 (0.050000f)
#define APP_CH1_FULL_SCALE_UNITS             (250.000000f)

#define APP_CH2_ZERO_COUNTS                  (0)
#define APP_CH2_VOLTS_PER_UNIT               (0.030530f)  /* CH2 current, V/A */
#define APP_CH2_SCALE_TRIM                   (1.000000f)
#define APP_CH2_OFFSET_UNITS                 (0.000000f)
#define APP_CH2_POLARITY                     (1.000000f)
#define APP_CH2_FILTER_ALPHA                 (0.050000f)
#define APP_CH2_FULL_SCALE_UNITS             (30.000000f)

#define APP_CH3_ZERO_COUNTS                  (0)
#define APP_CH3_VOLTS_PER_UNIT               (0.004009f)  /* CH2 voltage, V/V */
#define APP_CH3_SCALE_TRIM                   (1.000000f)
#define APP_CH3_OFFSET_UNITS                 (0.000000f)
#define APP_CH3_POLARITY                     (1.000000f)
#define APP_CH3_FILTER_ALPHA                 (0.050000f)
#define APP_CH3_FULL_SCALE_UNITS             (250.000000f)

/* DAC8564 / calibrated 0.5 V to 4.5 V outputs ------------------------------ */
#define APP_DAC_REFERENCE_V                  (2.500000f)
#define APP_DAC_OUTPUT_STAGE_GAIN            (2.000000f)
#define APP_DAC_CENTER_CODE                  (32768u)      /* 1.25 V -> 2.5 V */
#define APP_DAC_SPAN_CODE                    (26214u)      /* 0.8 * 32768, output span +-2.0 V (0.5 V to 4.5 V) */
#define APP_DAC_SAFE_CODE                    (32768u)
#define APP_DAC_UPDATE_PERIOD_MS             (10u)        /* 100 Hz */
#define APP_DAC_SPI_TIMEOUT_MS               (5u)

/* Telemetry --------------------------------------------------------------- */
#define APP_TELEMETRY_FORMAT_CSV             (0u)
#define APP_TELEMETRY_FORMAT_BINARY          (1u)
#define APP_TELEMETRY_FORMAT                 APP_TELEMETRY_FORMAT_CSV
#define APP_TELEMETRY_PERIOD_MS              (100u)       /* 10 Hz */
#define APP_TELEMETRY_UART_ENABLE             (1u)         /* ST-LINK VCP mirror */
#define APP_TELEMETRY_UART_TIMEOUT_MS         (20u)
#define APP_TELEMETRY_BINARY_VERSION          (1u)
#define APP_TELEMETRY_BINARY_TYPE_MEASUREMENT (1u)

/* Ethernet / NetX Duo ----------------------------------------------------- */
/*
 * The default sends one UDP datagram per telemetry period to the local
 * broadcast address.  This makes first-time PC discovery possible without
 * knowing the receiver address.  Change the four destination octets to use
 * a fixed unicast receiver.
 */
#define APP_NETWORK_ENABLE                    (1u)
#define APP_NETWORK_USE_DHCP                  (0u)

#define APP_NETWORK_STATIC_IP_0               (192u)
#define APP_NETWORK_STATIC_IP_1               (168u)
#define APP_NETWORK_STATIC_IP_2               (0u)
#define APP_NETWORK_STATIC_IP_3               (50u)
#define APP_NETWORK_STATIC_MASK_0             (255u)
#define APP_NETWORK_STATIC_MASK_1             (255u)
#define APP_NETWORK_STATIC_MASK_2             (255u)
#define APP_NETWORK_STATIC_MASK_3             (0u)
#define APP_NETWORK_STATIC_GATEWAY_0          (192u)
#define APP_NETWORK_STATIC_GATEWAY_1          (168u)
#define APP_NETWORK_STATIC_GATEWAY_2          (0u)
#define APP_NETWORK_STATIC_GATEWAY_3          (1u)

#define APP_NETWORK_UDP_DESTINATION_0         (255u)
#define APP_NETWORK_UDP_DESTINATION_1         (255u)
#define APP_NETWORK_UDP_DESTINATION_2         (255u)
#define APP_NETWORK_UDP_DESTINATION_3         (255u)
#define APP_NETWORK_UDP_DESTINATION_PORT      (5000u)
#define APP_NETWORK_UDP_SOURCE_PORT           (5001u)

#define APP_NETWORK_PACKET_PAYLOAD_SIZE       (1536u)
#define APP_NETWORK_PACKET_POOL_SIZE          (24576u)
#define APP_NETWORK_ARP_CACHE_SIZE            (1024u)
#define APP_NETWORK_IP_THREAD_STACK_SIZE      (2048u)
#define APP_NETWORK_THREAD_STACK_SIZE         (3072u)
#define APP_NETWORK_IP_THREAD_PRIORITY        (4u)
#define APP_NETWORK_THREAD_PRIORITY           (8u)
#define APP_NETWORK_STATUS_PERIOD_TICKS       (100u)       /* 1 second */
#define APP_NETWORK_LINK_RETRY_TICKS          (100u)       /* 1 second */

/* ThreadX application task ------------------------------------------------ */
#define APP_THREAD_STACK_SIZE                 (4096u)
#define APP_THREAD_PRIORITY                   (5u)         /* Below NetX IP(4), above LAN TX(8) */
#define APP_THREAD_SLEEP_TICKS                (1u)         /* 10 ms at 100 Hz */

/* Recovery / diagnostics -------------------------------------------------- */
#define APP_ADC_RETRY_PERIOD_MS              (1000u)
#define APP_ADC_NO_PROGRESS_TIMEOUT_MS        (500u)
#define APP_ADC_MAX_CONSECUTIVE_CRC_ERRORS    (16u)
#define APP_ADC_MAX_SPI_ERRORS_PER_RUN        (4u)
#define APP_CLIP_MARGIN                      (1.05f)

/* State Machine & Warmup -------------------------------------------------- */
#define APP_WARMUP_REQUIRED_FRAMES            (10u)
#define APP_WARMUP_TIMEOUT_MS                 (2000u)

/* Watchdog / Supervisor --------------------------------------------------- */
#define APP_WATCHDOG_ENABLE                   (1u)
#define APP_WATCHDOG_TIMEOUT_MS               (1000u)
#define APP_WATCHDOG_MEAS_GRACE_MS            (400u)
#define APP_WATCHDOG_NET_GRACE_MS             (2500u)

#endif /* APP_CONFIG_H */

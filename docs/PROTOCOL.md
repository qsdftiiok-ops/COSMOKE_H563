# COSMOKE H563 텔레메트리 프로토콜 규격서 (Telemetry Protocol Specification)

본 문서는 COSMOKE H563 펌웨어에서 출력하는 **CSV 텔레메트리** 및 **64-Byte 바이너리 텔레메트리(v1)**의 통신 규격, 패킷 구조, 바이트 레이아웃, 상태 플래그 및 CRC 검증 알고리즘을 정의합니다.

---

## 1. 전송 계층 개요 (Transport Layer Overview)

| 항목 | 이더넷 UDP | ST-LINK VCP UART |
|---|---|---|
| **기본 전송 방식** | IPv4 UDP Datagram | 비동기 직렬 통신 (RS-232 / CDC VCP) |
| **기본 전송 주기** | 100 ms (10 Hz) | 100 ms (10 Hz) |
| **기본 소스/목적지** | `192.168.0.50:5001` → `255.255.255.255:5000` | ST-LINK Virtual COM Port |
| **전송 속도 / 포맷** | 100 Mbps (RMII LAN8742) | 115200 bps, 8-Bit Data, No Parity, 1 Stop Bit |
| **기본 프로토콜** | 25-필드 확장 CSV (설정에 따라 Binary 전환 가능) | 25-필드 확장 CSV |

---

## 2. 25-필드 확장 CSV 포맷 (Default Format)

펌웨어의 기본 텔레메트리는 쉼표(`,`)로 구분된 ASCII 문자열 형태이며, 줄바꿈은 `\r\n` (CRLF)을 사용합니다.

### 2.1 필드 정의 (25 Columns)

| 순번 | 필드명 | 데이터 타입 | 단위 / 포맷 | 설명 |
|---|---|---|---|---|
| 1 | `sequence` | `uint32` | 카운트 | 2,000 SPS 기준 ADC 샘플 누적 순번 |
| 2 | `timestamp_ms`| `uint32` | ms | MCU 부팅 후 경과 시간 (`HAL_GetTick()`) |
| 3 | `flags` | `hex16` | `0x%04X` | 시스템 상태 비트마스크 (하단 플래그 표 참조) |
| 4 | `adc_status` | `hex16` | `0x%04X` | ADS131M08 STATUS 레지스터 응답값 |
| 5 | `i1_mA` | `int32` | mA | CH1 전류 필터링 측정값 |
| 6 | `v1_mV` | `int32` | mV | CH1 전압 필터링 측정값 |
| 7 | `i2_mA` | `int32` | mA | CH2 전류 필터링 측정값 |
| 8 | `v2_mV` | `int32` | mV | CH2 전압 필터링 측정값 |
| 9 | `raw_i1` | `int32` | ADC Count | CH1 전류 ADC 24-bit 2's complement 원시 카운트 |
| 10 | `raw_v1` | `int32` | ADC Count | CH1 전압 ADC 24-bit 원시 카운트 |
| 11 | `raw_i2` | `int32` | ADC Count | CH2 전류 ADC 24-bit 원시 카운트 |
| 12 | `raw_v2` | `int32` | ADC Count | CH2 전압 ADC 24-bit 원시 카운트 |
| 13 | `dac_i1` | `uint16` | 16-bit Code | DAC CH A 출력 코드 ($0 \sim 65535$) |
| 14 | `dac_v1` | `uint16` | 16-bit Code | DAC CH B 출력 코드 |
| 15 | `dac_i2` | `uint16` | 16-bit Code | DAC CH C 출력 코드 |
| 16 | `dac_v2` | `uint16` | 16-bit Code | DAC CH D 출력 코드 |
| 17 | `dropped` | `uint32` | 누적 카운트 | 총 ADC 드롭 프레임 수 |
| 18 | `dropped_dma_busy` | `uint32` | 누적 카운트 | 이전 DMA 미완료로 인한 드롭 |
| 19 | `dropped_ring_full` | `uint32` | 누적 카운트 | 256 링버퍼 오버플로우로 인한 드롭 |
| 20 | `dropped_spi_start` | `uint32` | 누적 카운트 | SPI DMA 시작 실패로 인한 드롭 |
| 21 | `dropped_spi_error` | `uint32` | 누적 카운트 | SPI 하드웨어 에러로 인한 드롭 |
| 22 | `dropped_bus_locked`| `uint32` | 누적 카운트 | DAC 전송 중 2회 이상 DRDY 중복 드롭 |
| 23 | `adc_crc_errors` | `uint32` | 누적 카운트 | ADS131M08 수신 프레임 CRC 오류 누적 수 |
| 24 | `adc_spi_errors` | `uint32` | 누적 카운트 | SPI 통신 에러 인터럽트 누적 수 |
| 25 | `dac_errors` | `uint32` | 누적 카운트 | DAC8564 전송 실패 주기 누적 수 |

### 2.2 예시 프레임 (ASCII Sample)
```text
14205,710300,0x0001,0x0555,4985,11020,4992,11015,1342177,2949120,1344102,2948950,37112,33924,37118,33923,0,0,0,0,0,0,0,0,0
```

---

## 3. 64-Byte 바이너리 프로토콜 (Binary v1 Format)

대역폭 최적화 및 고속 처리를 위해 고정 64바이트 리틀 엔디언(Little-Endian) 구조체를 지원합니다.

### 3.1 메모리 레이아웃 (Packet Layout)

```text
 0      2   3   4      6      8     12     16     18     20     24          40          56     58     60     62  63
┌──────┬───┬───┬──────┬──────┬──────┬──────┬──────┬──────┬──────┬───────────┬───────────┬──────┬──────┬──────┐
│ SYNC │VER│TYP│ LEN  │FLAGS │ SEQ  │TIME_MS│STATUS│CRC_ERR│RAW0..3│SCALE0..3  │DAC0..3    │ DROP │CRC16 │ Pad  │
└──────┴───┴───┴──────┴──────┴──────┴──────┴──────┴──────┴──────┴───────────┴───────────┴──────┴──────┴──────┘
 2B     1B  1B  2B     2B     4B     4B     2B     2B     16B    16B         8B          2B     2B     2B
```

### 3.2 상세 바이트 맵

| 바이트 오프셋 | 필드명 | 타입 | 바이트 수 | 설명 |
|---|---|---|---|---|
| `0x00 - 0x01` | **Sync Word** | `uint16_t` | 2 | 매직 넘버 `0xA55A` (전송 순서: `0x5A`, `0xA5`) |
| `0x02` | **Version** | `uint8_t` | 1 | 프로토콜 버전 (`1`) |
| `0x03` | **Packet Type** | `uint8_t` | 1 | 패킷 타입 (`1` = Measurement) |
| `0x04 - 0x05` | **Packet Length**| `uint16_t` | 2 | 전체 패킷 크기 (`64`) |
| `0x06 - 0x07` | **Flags** | `uint16_t` | 2 | 시스템 상태 비트마스크 |
| `0x08 - 0x0B` | **Sequence** | `uint32_t` | 4 | ADC 누적 샘플 번호 |
| `0x0C - 0x0F` | **Timestamp** | `uint32_t` | 4 | MCU 부팅 경과 시간 (ms) |
| `0x10 - 0x11` | **ADC Status** | `uint16_t` | 2 | ADS131M08 STATUS 레지스터값 |
| `0x12 - 0x13` | **CRC Errors** | `uint16_t` | 2 | ADC 누적 CRC 오류 수 |
| `0x14 - 0x23` | **Raw Counts** | `int32_t[4]`| 16 | 4개 채널 원시 ADC 카운트 (각 4 Bytes) |
| `0x24 - 0x33` | **Scaled Values**| `int32_t[4]`| 16 | 4개 채널 밀리단위 필터값 (mA, mV) |
| `0x34 - 0x3B` | **DAC Codes** | `uint16_t[4]`| 8 | 4개 채널 DAC 출력 코드 (각 2 Bytes) |
| `0x3C - 0x3D` | **Drop Count** | `uint16_t` | 2 | 총 누적 드롭 프레임 수 |
| `0x3E - 0x3F` | **Frame CRC** | `uint16_t` | 2 | Offset 0~61 데이터에 대한 CRC-16/CCITT |
| `0x40 - 0x41` | *(Reserved)* | `uint16_t` | 2 | 64-byte 정렬용 패딩 (`0x0000`) |

---

## 4. 시스템 상태 플래그 정의 (System Flags Bitmask)

`flags` 필드는 16-bit 정수형 비트마스크로 장치 및 통신 상태를 실시간 보고합니다.

| 비트 번호 | 상수명 | 마스크 값 | 심각도 | 설명 |
|---|---|---|---|---|
| **Bit 0** | `TELEMETRY_FLAG_VALID` | `0x0001` | Info | 계측값이 유효하고 정상 동작 중 (`ACTIVE`) |
| **Bit 1** | `TELEMETRY_FLAG_AFE_FAULT` | `0x0002` | Critical | 아날로그 프론트엔드 하드웨어 결함 감지 (`PA3` Low) |
| **Bit 2** | `TELEMETRY_FLAG_ADC_CRC_ERROR`| `0x0004` | Warning | ADC SPI 프레임 CRC 오류 발생 |
| **Bit 3** | `TELEMETRY_FLAG_ADC_FRAME_DROP`| `0x0008` | Warning | ADC 링버퍼 또는 DMA 지연으로 프레임 드롭 발생 |
| **Bit 4** | `TELEMETRY_FLAG_ADC_SPI_ERROR` | `0x0010` | Error | ADC SPI 하드웨어 전송 오류 |
| **Bit 5** | `TELEMETRY_FLAG_ADC_OFFLINE` | `0x0020` | Critical | ADC 오프라인 또는 통신 두절 |
| **Bit 6** | `TELEMETRY_FLAG_DAC_ERROR` | `0x0040` | Critical | DAC8564 Health 실패 또는 통신 에러 |
| **Bit 7** | `TELEMETRY_FLAG_OUTPUT_CLIPPED`| `0x0080` | Warning | 계측값이 풀스케일($\pm\text{FS}$)을 초과하여 클리핑됨 |
| **Bit 8** | `TELEMETRY_FLAG_LAN_LINK_DOWN` | `0x0100` | Warning | 이더넷 케이블 연결 끊김 (PHY Link Down) |
| **Bit 9** | `TELEMETRY_FLAG_LAN_NO_ADDRESS`| `0x0200` | Warning | IP 주소 미할당 |
| **Bit 10**| `TELEMETRY_FLAG_LAN_TX_ERROR` | `0x0400` | Warning | UDP 패킷 송신 실패 |
| **Bit 11**| `TELEMETRY_FLAG_LAN_FRAME_DROP`| `0x0800` | Warning | 네트워크 메일박스 혼잡으로 인한 드롭 |
| **Bit 12**| `TELEMETRY_FLAG_MUTED` | `0x1000` | Critical | 시스템 MUTE 상태 (DAC 2.5V 안전 출력 고정) |
| **Bit 13**| `TELEMETRY_FLAG_WARMUP` | `0x2000` | Info | 부팅/복구 후 웜업 검증 진행 중 |

---

## 5. CRC-16/CCITT 검증 알고리즘

COSMOKE 펌웨어 및 파서는 통일된 **CRC-16/CCITT (0x1021)** 표준을 적용합니다.

- **Polynomial**: `0x1021` ($x^{16} + x^{12} + x^5 + 1$)
- **Initial Value**: `0xFFFF`
- **Reflected In / Out**: False
- **Xor Out**: `0x0000`

### 5.1 C 언어 구현
```c
uint16_t Telemetry_Crc16Ccitt(const uint8_t *data, size_t length)
{
  uint16_t crc = 0xFFFFu;

  for (size_t i = 0u; i < length; ++i)
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
```

### 5.2 Python 언어 구현
```python
def crc16_ccitt(data: bytes) -> int:
    crc = 0xFFFF
    for value in data:
        crc ^= value << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc
```

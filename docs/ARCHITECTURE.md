# COSMOKE H563 아키텍처

## 데이터 흐름

```text
ADS131M08 DRDY
  → EXTI3 ISR
  → SPI1 DMA 수집
  → 128-frame ring buffer
  → ThreadX Measurement task
  → 보정 · IIR filter · DAC code 생성
  → DAC8564 / UART / UDP telemetry
```

ADC는 4 kHz로 수집하며, 계측 태스크는 10 ms 주기로 프레임을 소비합니다. ADC와 DAC는 SPI1을 공유하므로 ADC DMA가 활성인 동안에는 DAC 전송을 잠그고, DRDY가 들어오면 대기 후 다시 DMA를 시작합니다.

## 주요 모듈

| 경로 | 역할 |
|---|---|
| `Core/Src/ads131m08.c` | ADC 초기화, 레지스터 검증, DRDY/DMA, CRC, 링버퍼, SPI recovery gate |
| `Core/Src/measurement.c` | raw count를 engineering unit으로 변환하고 IIR filter 및 DAC code 생성 |
| `Core/Src/dac8564.c` | 4채널 DAC 출력, 전체 갱신 주기 health 판정, 오류 통계 |
| `Core/Src/app.c` | 상태 머신, fault 처리, shared SPI 복구, watchdog supervisor |
| `NetXDuo/App/app_netxduo.c` | Ethernet/UDP 전송 및 네트워크 heartbeat |
| `Core/Src/telemetry.c` | CSV 및 binary telemetry frame 생성 |

## 상태 머신

```text
MUTED → RECOVERING → ADC_WARMUP → ACTIVE
  ↑                         │        │
  └──── ADC/DAC fault ──────┴────────┘
```

- `MUTED`: safe DAC code를 사용하고 recovery backoff를 적용합니다.
- `RECOVERING`: DRDY를 막고 진행 중인 SPI DMA를 abort한 뒤 SPI1, ADC, DAC를 재초기화합니다.
- `ADC_WARMUP`: CRC 정상 프레임 10개와 DAC health를 기다립니다. timeout은 2초입니다.
- `ACTIVE`: 필터링된 측정값을 DAC와 telemetry로 출력합니다.

## 오류와 복구

다음 상황에서 Mute 및 재복구를 시작합니다.

- ADC SPI 오류 누적이 임계값 도달
- 연속 CRC 오류가 임계값 도달
- ADC 데이터 진행 정지 timeout
- DAC health fault
- warmup timeout

복구는 애플리케이션 태스크에서만 수행하며, recovery gate가 DRDY, DMA 완료, SPI 오류 callback 및 프레임 소비를 차단합니다. 이는 abort callback이 상태를 다시 변경하는 것을 방지합니다.

## Telemetry

기본 CSV는 10 Hz로 sequence, timestamp, flags, ADC status, 4개 측정값, raw data, DAC code, drop/CRC/SPI/DAC 통계를 전송합니다.

Binary v1은 64 bytes이며 sync `0xA55A`, version/type, flags, sequence, timestamp, raw/filter/DAC data 및 CRC-16/CCITT를 포함합니다. binary layout을 변경할 때는 version을 올리고 Python 파서를 함께 갱신해야 합니다.

## Watchdog

측정 태스크와 네트워크 태스크의 heartbeat를 감시합니다. LAN link down이나 주소 미할당은 watchdog reset 조건이 아니며, 태스크 자체가 기한 안에 실행되지 않을 때만 IWDG refresh를 중단합니다.

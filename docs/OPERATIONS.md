# COSMOKE H563 운영 및 시험 가이드

## 사전 조건

- NUCLEO-H563ZI와 ST-LINK USB 연결
- Ethernet 시험 시 RJ45 연결
- `make` 및 STM32CubeCLT/CubeMX 번들 도구 설치
- PC와 보드가 같은 IPv4 서브넷에 있도록 구성

기본 보드 주소는 `192.168.0.50/24`이고, 텔레메트리는 UDP `5000`번 포트로 전송됩니다. 설정은 `Core/Inc/app_config.h`에서 변경합니다.

## 빌드와 플래시

프로젝트 루트에서 실행합니다.

```powershell
make build
make connect
make flash
```

`make build`는 `build/firmware-debug/`에 ELF, HEX, BIN을 생성합니다. `make flash`는 HEX 기록, 검증, 리셋까지 수행합니다.

## 텔레메트리 수신

### 대시보드

```powershell
make gui
```

대시보드는 기본적으로 모든 PC 인터페이스의 UDP `5000` 포트를 수신합니다. 보드 IP, sequence, 보드 시간, 상태 플래그와 오류 카운터를 표시합니다. 실행 즉시 프로젝트의 `logs` 폴더에 `cosmoke_capture_YYYYMMDD_HHMMSS_ffffff.csv` 이름으로 CSV 기록을 시작합니다. `저장 종료`를 누르면 파일을 닫고 프레임 수·추정 UDP 유실·재부팅/역순 sequence 횟수를 표시합니다. 이후 `새 CSV 저장`을 누르면 이름 입력 없이 새 파일을 바로 생성합니다.

### 콘솔 모니터

```powershell
python .\Tools\cosmoke_udp_monitor.py --show-flags
```

펌웨어 기본 포맷은 CSV입니다. binary 포맷을 사용하면 모니터가 64-byte v1 패킷을 자동 감지합니다.

### 성능 시험용 CSV 캡처

PC 수신 시각(UTC), 보드 경과시간, sequence, 상태 플래그와 오류 카운터를 하나의 CSV로 저장하려면 다음처럼 실행합니다.

```powershell
python .\Tools\cosmoke_udp_monitor.py --csv-out .\logs\soak_001.csv --show-flags
```

기존 파일에 이어 기록하려면 `--append`를 함께 지정합니다. 캡처를 `Ctrl+C`로 종료하면 수신 프레임 수, 추정 UDP 유실 수, 재부팅/역순 sequence 감지 횟수를 표시합니다. `received_at`은 PC 수신 시각이고 `device_ms`는 MCU 부팅 후 경과시간이므로, 보드 재부팅 또는 장시간 시험 분석에는 두 열을 함께 사용합니다.

## 상태 확인

| 상태 | 의미 | 기대 동작 |
|---|---|---|
| `WARMUP` | ADC 및 DAC 초기 검증 중 | safe DAC code 유지, 정상 프레임과 DAC health 확인 |
| `ACTIVE` | 정상 계측 및 출력 | 녹색 LED, 유효 측정값 전송 |
| `MUTED` | 오류 또는 복구 대기 | safe DAC code, 적색 LED, 재시도 대기 |
| `ADC_OFFLINE` | ADC 통신 또는 데이터 진행 이상 | 1초 backoff 후 공유 SPI 복구 시도 |
| `DAC_ERROR` | DAC health 실패 | Mute 전이 및 복구 시도 |

## 벤치 시험 순서

실제 부하·전력 장비·액추에이터를 연결하기 전에 다음 순서로 검증합니다.

1. 출력은 오실로스코프 또는 고임피던스 측정기에만 연결합니다.
2. 플래시 후 `WARMUP`에서 `ACTIVE`로 전이하는지 확인합니다.
3. ADC DRDY 또는 SPI 신호를 분리해 `MUTED` 및 재복구를 확인합니다.
4. Ethernet을 분리해도 watchdog 재부팅 반복이 발생하지 않는지 확인합니다.
5. DAC 통신 이상을 재현해 적색 LED, fault flag, 복구 동작을 확인합니다.

> 하드웨어 Mute/CLR 제어선은 회로도에 따라 별도로 구현·검증해야 합니다. 소프트웨어 safe code만으로 실제 제어 출력 차단을 보장하지 않습니다.

## 보정

`Core/Inc/app_config.h`의 채널별 파라미터를 사용합니다.

- `APP_CHx_ZERO_COUNTS`: 입력 0에서의 ADC raw offset
- `APP_CHx_SCALE_TRIM`: 스팬 보정 계수
- `APP_CHx_OFFSET_UNITS`: engineering unit offset

영점과 최소 두 개 이상의 기준점에서 측정하고, 보정 장비·온도·일자·펌웨어 버전을 함께 기록합니다.

## 운영 전환

개발 단계의 UDP broadcast 대신 운영 환경에서는 수신 PC의 유니캐스트 IP를 목적지로 지정하는 것을 권장합니다. VLAN 또는 계측 전용 유선망을 사용하면 데이터 노출과 수신 혼선을 줄일 수 있습니다.

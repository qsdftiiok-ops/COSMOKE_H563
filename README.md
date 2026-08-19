# COSMOKE H563 Firmware

STM32H563 기반 4채널 전류·전압 계측 및 아날로그 출력 펌웨어입니다. ADS131M08 ADC로 계측한 데이터를 보정·필터링하고, DAC8564 출력과 UART/UDP 텔레메트리로 제공합니다.

## 주요 기능

- ADS131M08 4채널 계측: DRDY 인터럽트 및 SPI DMA 기반 수집
- 채널별 영점·스케일·오프셋 보정과 IIR 필터링
- DAC8564 4채널 아날로그 출력 및 안전 중앙값 출력
- ThreadX 및 NetX Duo 기반 UDP 텔레메트리
- ADC CRC/SPI 오류 감시, 공유 SPI 복구, warmup 상태 관리
- DAC 전체 갱신 주기 기반 health 판정 및 오류 진단
- 계측·네트워크 heartbeat를 이용한 IWDG supervisor
- CSV(기본) 및 64-byte binary 텔레메트리, Python UDP 대시보드

## 대상 및 기본 통신 설정

- MCU: STM32H563
- ADC: ADS131M08
- DAC: DAC8564
- 기본 IP: `192.168.0.50/24`
- 기본 UDP 목적지: `255.255.255.255:5000`
- 기본 UDP 소스 포트: `5001`
- 텔레메트리 주기: 100 ms

운영 환경에서는 `Core/Inc/app_config.h`에서 UDP 목적지를 관제 PC의 유니캐스트 IP로 변경하는 것을 권장합니다.

## 빌드

Windows PowerShell에서 STM32CubeCLT/CubeMX 번들 도구 또는 동등한 GNU Arm Toolchain을 준비한 뒤 실행합니다.

```powershell
make build
```

성공하면 `build/firmware-debug/`에 다음 산출물이 생성됩니다.

- `COSMOKE_H563.elf`
- `COSMOKE_H563.hex`
- `COSMOKE_H563.bin`

## 프로그래밍

ST-LINK 연결 및 대상 전원을 확인한 뒤 다음을 실행합니다.

```powershell
make connect
make flash
```

`make flash`는 빌드, 프로그램 기록, 검증 및 리셋을 수행합니다.

## 텔레메트리와 대시보드

PC에서 UDP 5000번 포트를 수신합니다.

```powershell
python .\Tools\cosmoke_udp_monitor.py --show-flags
python .\Tools\cosmoke_dashboard.py
```

대시보드는 CSV와 binary 패킷을 자동으로 해석하며, `MUTED`, `WARMUP`, ADC/DAC/LAN fault 상태를 표시합니다.

## 안전 및 실기 시험

이 펌웨어는 ADC·DAC 통신 이상 시 safe DAC code, 상태 플래그, 복구 절차를 수행합니다. 실제 부하를 연결하기 전에는 다음을 벤치 환경에서 확인하십시오.

1. `WARMUP`에서 `ACTIVE`로 정상 전이되는지 확인합니다.
2. ADC DRDY 또는 SPI 장애 시 `MUTED`와 재복구가 동작하는지 확인합니다.
3. LAN 미연결 상태에서도 watchdog 재부팅 반복이 없는지 확인합니다.
4. 실제 하드웨어 Mute/CLR 제어선이 있는 보드에서는 회로도에 맞는 GPIO 구현과 출력 차단 동작을 검증합니다.

## 보정

`Core/Inc/app_config.h`의 `APP_CHx_ZERO_COUNTS`, `APP_CHx_SCALE_TRIM`, `APP_CHx_OFFSET_UNITS`는 실측값으로 보정해야 합니다. 영점과 최소 두 개의 스팬 지점에서 검증하고, 보정 장비·온도·날짜·펌웨어 버전을 기록하십시오.

## 프로젝트 구조

```text
Core/          애플리케이션, ADC/DAC 드라이버, 보정 및 보드 초기화
NetXDuo/       Ethernet/UDP 텔레메트리
AZURE_RTOS/    ThreadX 메모리 및 커널 설정
Tools/         빌드·플래시·UDP 모니터·대시보드 도구
Tests/host/    호스트 측 측정 변환 테스트
```

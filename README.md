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

## 문서

- [운영 및 시험 가이드](docs/OPERATIONS.md): 빌드, 플래시, LAN 수신, 상태 확인 및 벤치 시험
- [아키텍처](docs/ARCHITECTURE.md): 데이터 경로, ThreadX 구성, 상태 머신, 복구 및 텔레메트리

## 대상 및 기본 통신 설정

- MCU: STM32H563
- ADC: ADS131M08
- DAC: DAC8564
- 기본 IP: `192.168.0.50/24`
- 기본 UDP 목적지: `255.255.255.255:5000`
- 기본 UDP 소스 포트: `5001`
- 텔레메트리 주기: 100 ms

운영 환경에서는 `Core/Inc/app_config.h`에서 UDP 목적지를 관제 PC의 유니캐스트 IP로 변경하는 것을 권장합니다.

## 빠른 시작

Windows PowerShell에서 STM32CubeCLT/CubeMX 번들 도구 또는 동등한 GNU Arm Toolchain을 준비한 뒤 실행합니다.

```powershell
make build
```

성공하면 `build/firmware-debug/`에 다음 산출물이 생성됩니다.

- `COSMOKE_H563.elf`
- `COSMOKE_H563.hex`
- `COSMOKE_H563.bin`

ST-LINK와 대상 전원을 연결한 뒤 다음을 실행합니다.

```powershell
make connect
make flash
make gui
```

`make flash`는 빌드, 기록, 검증 및 MCU 리셋을 수행합니다. 상세 절차와 안전 조건은 [운영 및 시험 가이드](docs/OPERATIONS.md)를 확인하세요.

## 프로젝트 구조

```text
Core/          애플리케이션, ADC/DAC 드라이버, 보정 및 보드 초기화
NetXDuo/       Ethernet/UDP 텔레메트리
AZURE_RTOS/    ThreadX 메모리 및 커널 설정
Tools/         빌드·플래시·UDP 모니터·대시보드 도구
Tests/host/    호스트 측 측정 변환 테스트
docs/          운영·시험 및 아키텍처 문서
```

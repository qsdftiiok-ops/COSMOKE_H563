# Convenience commands for Windows PowerShell users.
#
# Prerequisite: GNU Make must be on PATH.  The project scripts locate the
# bundled STM32 compiler/programmer themselves, so no CubeIDE installation
# path is hard-coded here.

.DEFAULT_GOAL := help
.PHONY: help build flash connect gui clean

help:
	@echo COSMOKE H563 convenience commands:
	@echo   make build    - Build Debug firmware (ELF, HEX, BIN)
	@echo   make flash    - Build, then program, verify, and reset the NUCLEO
	@echo   make connect  - Check ST-LINK/SWD connection only
	@echo   make gui      - Open the LAN live dashboard (UDP 5000)
	@echo   make clean    - Remove the Debug build directory

build:
	powershell -NoProfile -ExecutionPolicy Bypass -File .\Tools\Build-Firmware.ps1 -Configuration Debug

flash: build
	powershell -NoProfile -ExecutionPolicy Bypass -File .\Tools\Flash-Firmware.ps1

connect:
	powershell -NoProfile -ExecutionPolicy Bypass -File .\Tools\Flash-Firmware.ps1 -ConnectOnly

gui:
	python .\Tools\cosmoke_dashboard.py

clean:
	powershell -NoProfile -Command "if (Test-Path '.\build\firmware-debug') { Remove-Item -LiteralPath '.\build\firmware-debug' -Recurse -Force }"

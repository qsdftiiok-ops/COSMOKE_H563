param(
  [string]$Firmware = '',
  [ValidateRange(100, 24000)]
  [int]$FrequencyKHz = 1000,
  [switch]$ConnectOnly
)

$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if ([string]::IsNullOrWhiteSpace($Firmware)) {
  $Firmware = Join-Path $projectRoot 'build\firmware-debug\COSMOKE_H563.hex'
} elseif (-not [IO.Path]::IsPathRooted($Firmware)) {
  $Firmware = Join-Path $projectRoot $Firmware
}

$candidates = @()
$candidates += Get-ChildItem 'C:\stm32\tools\programmer-*\bin\STM32_Programmer_CLI.exe' `
  -ErrorAction SilentlyContinue
$programFilesCandidate = Join-Path $env:ProgramFiles `
  'STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe'
if (Test-Path $programFilesCandidate) { $candidates += Get-Item $programFilesCandidate }
$bundlePattern = Join-Path $env:LOCALAPPDATA `
  'stm32cube\bundles\programmer\*\bin\STM32_Programmer_CLI.exe'
$candidates += Get-ChildItem $bundlePattern -ErrorAction SilentlyContinue

$programmer = $candidates |
  Sort-Object @{ Expression = { if ($_.FullName -match '[^\x00-\x7F]') { 1 } else { 0 } } }, `
              @{ Expression = 'LastWriteTime'; Descending = $true } |
  Select-Object -First 1
if (-not $programmer) {
  throw 'STM32_Programmer_CLI.exe not found. Install STM32CubeProgrammer.'
}

Push-Location (Split-Path $programmer.FullName -Parent)
try {
  Write-Host "Checking SWD connection with $($programmer.FullName)"
  & $programmer.FullName -c port=SWD "freq=$FrequencyKHz"
  if ($LASTEXITCODE -ne 0) {
    throw 'SWD connection failed. Check the ST-LINK cable, target power, and Programmer database path.'
  }

  if ($ConnectOnly) { return }
  if (-not (Test-Path $Firmware)) {
    throw "Firmware image not found: $Firmware. Run Tools\Build-Firmware.ps1 first."
  }

  Write-Host "Programming and verifying $Firmware"
  & $programmer.FullName -c port=SWD "freq=$FrequencyKHz" -w $Firmware -v
  if ($LASTEXITCODE -ne 0) { throw 'Programming or verification failed.' }

  Write-Host 'Resetting MCU...'
  & $programmer.FullName -c port=SWD "freq=$FrequencyKHz" -rst
  if ($LASTEXITCODE -ne 0) { throw 'MCU reset failed.' }
} finally {
  Pop-Location
}


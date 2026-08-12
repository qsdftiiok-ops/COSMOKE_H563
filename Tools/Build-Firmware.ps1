param(
  [ValidateSet('Debug', 'Release')]
  [string]$Configuration = 'Debug',
  [string]$BuildDirectory = ''
)

$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$bundleRoot = Join-Path $env:LOCALAPPDATA 'stm32cube\bundles'

function Find-BundledTool {
  param([string]$Package, [string]$Executable)

  $packageRoot = Join-Path $bundleRoot $Package
  if (Test-Path $packageRoot) {
    $match = Get-ChildItem $packageRoot -Directory |
      Sort-Object @{ Expression = {
        try { [version](($_.Name -split '\+')[0]) }
        catch { [version]'0.0' }
      }; Descending = $true } |
      ForEach-Object {
        $candidate = Join-Path $_.FullName "bin\$Executable"
        if (Test-Path $candidate) { Get-Item $candidate }
      } |
      Select-Object -First 1
    if ($match) { return $match.FullName }
  }

  $fromPath = Get-Command $Executable -ErrorAction SilentlyContinue
  if ($fromPath) { return $fromPath.Source }
  throw "Cannot find $Executable. Install STM32CubeCLT/CubeMX firmware build tools."
}

$cmake = Find-BundledTool 'cmake' 'cmake.exe'
$ninja = Find-BundledTool 'ninja' 'ninja.exe'
$compiler = Find-BundledTool 'gnu-tools-for-stm32' 'arm-none-eabi-gcc.exe'
$compilerBin = Split-Path $compiler -Parent
$ninjaBin = Split-Path $ninja -Parent

if ([string]::IsNullOrWhiteSpace($BuildDirectory)) {
  $BuildDirectory = Join-Path $projectRoot ("build\firmware-" + $Configuration.ToLowerInvariant())
} elseif (-not [IO.Path]::IsPathRooted($BuildDirectory)) {
  $BuildDirectory = Join-Path $projectRoot $BuildDirectory
}

$env:PATH = "$compilerBin;$ninjaBin;$env:PATH"
$toolchain = Join-Path $projectRoot 'cmake\gcc-arm-none-eabi.cmake'

# GNU Arm's linker creates temporary files while linking.  The default Windows
# temp folder for this account contains Korean characters, which older bundled
# GNU tools cannot always open.  Keep temporary build files under the ASCII
# project path so configure, link, and post-build steps are deterministic.
$toolTemp = Join-Path $projectRoot 'build\.tool-tmp'
New-Item -ItemType Directory -Path $toolTemp -Force | Out-Null
$env:TEMP = $toolTemp
$env:TMP = $toolTemp
$env:TMPDIR = $toolTemp

Write-Host "Configuring $Configuration build..."
& $cmake -S $projectRoot -B $BuildDirectory -G Ninja `
  "-DCMAKE_BUILD_TYPE=$Configuration" `
  "-DCMAKE_TOOLCHAIN_FILE=$toolchain" `
  "-DCMAKE_MAKE_PROGRAM=$ninja" `
  '-DCMAKE_C_COMPILER_FORCED=TRUE' `
  '-DCMAKE_C_COMPILER_WORKS=TRUE'
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed ($LASTEXITCODE)." }

Write-Host 'Building firmware...'
& $cmake --build $BuildDirectory
if ($LASTEXITCODE -ne 0) { throw "Firmware build failed ($LASTEXITCODE)." }

$stem = Join-Path $BuildDirectory 'COSMOKE_H563'
foreach ($extension in @('elf', 'hex', 'bin')) {
  $artifact = "$stem.$extension"
  if (-not (Test-Path $artifact)) { throw "Missing build artifact: $artifact" }
  Write-Host "Created: $artifact"
}
